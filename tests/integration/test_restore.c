/* Integration test: startup restore + retry semantics under Xvfb.
 * Writes a state file with one good image entry and one that can't load,
 * restores, then verifies the failed entry stays pending until it is fixed
 * and re-restored — mirroring the daemon's GPU-not-ready-on-first-boot retry. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "../harness.h"
#include "../../src/app.h"
#include "../../src/state.h"

static void write_state(const char *path, const char *bad) {
  XWStateEntry entries[2];
  memset(entries, 0, sizeof(entries));

  snprintf(entries[0].name, sizeof(entries[0].name), "testgood");
  entries[0].type = XW_BACKEND_IMAGE;
  snprintf(entries[0].path, sizeof(entries[0].path), "tests/data/red.png");
  entries[0].have_geom = 1;
  entries[0].x = 0;
  entries[0].y = 0;
  entries[0].w = 64;
  entries[0].h = 64;
  entries[0].keep = 0;

  int n = 1;
  if (bad) {
    snprintf(entries[1].name, sizeof(entries[1].name), "testbad");
    entries[1].type = XW_BACKEND_IMAGE;
    snprintf(entries[1].path, sizeof(entries[1].path), "%s", bad);
    entries[1].have_geom = 1;
    entries[1].x = 64;
    entries[1].y = 0;
    entries[1].w = 64;
    entries[1].h = 64;
    entries[1].keep = 0;
    n = 2;
  }
  if (xw_state_save(path, entries, n) != 0)
    TEST_FAIL("cannot write state file");
}

int main(void) {
  TEST_SUITE("restore");

  char tmp[] = "/tmp/xw-restore-XXXXXX";
  char *dir = mkdtemp(tmp);
  TEST_CHECK(dir != NULL);
  setenv("XDG_STATE_HOME", dir, 1);

  char path[1024];
  xw_state_path(path, sizeof(path));

  XWError err;
  memset(&err, 0, sizeof(err));
  XWApp *app = xw_app_create(&err);
  TEST_CHECK(app != NULL);
  if (!app)
    return 1;

  TEST_BEGIN("failed entry stays pending, good entry restored");
  write_state(path, "/nonexistent/img.png");
  xw_app_restore_startup(app);
  TEST_CHECK_EQ_INT(xw_app_restore_pending(app), 1);
  TEST_CHECK(xw_app_window_count(app) >= 1);

  TEST_BEGIN("retry after the entry becomes valid clears pending");
  write_state(path, NULL); /* drop the broken entry */
  xw_app_restore_startup(app);
  TEST_CHECK_EQ_INT(xw_app_restore_pending(app), 0);

  TEST_BEGIN("set captures backend flags into the saved state");
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.command = XW_CMD_SET;
  cfg.backend_explicit = XW_BACKEND_IMAGE;
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  cfg.geom_x = 200;
  cfg.geom_y = 0;
  cfg.geom_w = 64;
  cfg.geom_h = 64;
  snprintf(cfg.name, sizeof(cfg.name), "flgtest");
  snprintf(cfg.path, sizeof(cfg.path), "tests/data/red.png");
  cfg.scale_mode = XW_SCALE_TILE;
  cfg.opacity = 0.5;
  cfg.below = 1;
  cfg.volume = 100.0;
  snprintf(cfg.audio_client_name, sizeof(cfg.audio_client_name), "xwallpaper");
  TEST_CHECK_EQ_INT(xw_app_set(app, &cfg, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_save_state(app), 0);

  XWStateEntry out[XW_STATE_MAX_ENTRIES];
  int count = 0;
  xw_state_load(path, out, XW_STATE_MAX_ENTRIES, &count);
  int found = 0;
  for (int i = 0; i < count; i++) {
    if (strcmp(out[i].name, "flgtest") == 0) {
      found = 1;
      TEST_CHECK_EQ_INT(out[i].scale_mode, XW_SCALE_TILE);
      TEST_CHECK_EQ_INT((int)(out[i].opacity * 10), 5);
      TEST_CHECK_EQ_INT(out[i].below, 1);
    }
  }
  TEST_CHECK(found);

  xw_app_destroy(app);

  /* A runtime mute/volume control is the user's current intent; the saved
   * flags must follow it, otherwise the next restore brings the wallpaper
   * back silent with no visible reason. */
  TEST_BEGIN("runtime mute/volume controls persist into state");
  {
    char tmp2[] = "/tmp/xw-restore-ctl-XXXXXX";
    char *dir2 = mkdtemp(tmp2);
    TEST_CHECK(dir2 != NULL);
    if (dir2) {
      setenv("XDG_STATE_HOME", dir2, 1);
      char path2[1024];
      xw_state_path(path2, sizeof(path2));
      XWError e2;
      memset(&e2, 0, sizeof(e2));
      XWApp *a2 = xw_app_create(&e2);
      TEST_CHECK(a2 != NULL);
      if (a2) {
        XWConfig c2;
        memset(&c2, 0, sizeof(c2));
        c2.command = XW_CMD_SET;
        c2.backend_explicit = XW_BACKEND_WEB;
        c2.target = XW_TARGET_GEOM;
        c2.have_geom = 1;
        c2.geom_w = 400;
        c2.geom_h = 300;
        c2.volume = 100;
        snprintf(c2.name, sizeof(c2.name), "ctlstate");
        snprintf(c2.path, sizeof(c2.path), "tests/data/webmedia");
        TEST_CHECK_EQ_INT(xw_app_set(a2, &c2, &e2), 0);
        for (int i = 0; i < 300; i++)
          g_main_context_iteration(NULL, FALSE);

        XWConfig ctl;
        memset(&ctl, 0, sizeof(ctl));
        ctl.target = XW_TARGET_GEOM;
        ctl.have_geom = 1;
        ctl.geom_w = 400;
        ctl.geom_h = 300;
        snprintf(ctl.name, sizeof(ctl.name), "ctlstate");
        TEST_CHECK_EQ_INT(xw_app_control(a2, &ctl, "mute", "on", &e2), 0);
        XWStateEntry out2[XW_STATE_MAX_ENTRIES];
        int cnt2 = 0;
        xw_state_load(path2, out2, XW_STATE_MAX_ENTRIES, &cnt2);
        int found2 = 0;
        for (int i = 0; i < cnt2; i++)
          if (strcmp(out2[i].name, "ctlstate") == 0) {
            found2 = 1;
            TEST_CHECK_EQ_INT(out2[i].mute, 1);
          }
        TEST_CHECK(found2);

        TEST_CHECK_EQ_INT(xw_app_control(a2, &ctl, "mute", "off", &e2), 0);
        cnt2 = 0;
        xw_state_load(path2, out2, XW_STATE_MAX_ENTRIES, &cnt2);
        for (int i = 0; i < cnt2; i++)
          if (strcmp(out2[i].name, "ctlstate") == 0)
            TEST_CHECK_EQ_INT(out2[i].mute, 0);

        TEST_CHECK_EQ_INT(xw_app_control(a2, &ctl, "volume", "40", &e2), 0);
        cnt2 = 0;
        xw_state_load(path2, out2, XW_STATE_MAX_ENTRIES, &cnt2);
        for (int i = 0; i < cnt2; i++)
          if (strcmp(out2[i].name, "ctlstate") == 0)
            TEST_CHECK_EQ_INT((int)(out2[i].volume + 0.5), 40);
        xw_app_destroy(a2);
      }
      unlink(path2);
      rmdir(dir2);
    }
  }

  unlink(path);
  rmdir(dir);

  TEST_SUMMARY();
}
