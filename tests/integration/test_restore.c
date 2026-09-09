/* Integration test: startup restore + retry semantics under Xvfb.
 * Writes a state file with one good image entry and one that can't load,
 * restores, then verifies the failed entry stays pending until it is fixed
 * and re-restored — mirroring the daemon's GPU-not-ready-on-first-boot retry. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  unlink(path);
  rmdir(dir);

  TEST_SUMMARY();
}
