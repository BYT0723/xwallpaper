/* Integration test: monitor hotplug reconfigure under Xvfb.
 * - output-bound window survives reconfigure with current geometry
 * - -g Screen re-fits the screen bbox, -g grp_* kept, stale -g removed
 * - output with no live window but a keep=1 last restores it;
 *   without a last the output stays blank */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "../../src/app.h"
#include "../../src/cli.h"
#include "../../src/state.h"
#include "../../src/x11.h"

static void set_path(XWConfig *cfg, const char *p) {
  snprintf(cfg->path, sizeof(cfg->path), "%s", p);
}

static void test_reconfigure_drops_geom_keeps_output(XWApp *app) {
  TEST_BEGIN("reconfigure keeps output window, follows Screen, keeps grp_, drops stale -g");
  XWError err;
  memset(&err, 0, sizeof(err));
  TEST_CHECK(app != NULL);
  if (!app)
    return;

  /* 1. output-bound window (primary) */
  XWConfig out;
  memset(&out, 0, sizeof(out));
  out.command = XW_CMD_SET;
  out.target = XW_TARGET_PRIMARY;
  set_path(&out, "tests/data/red.png");
  out.scale_mode = XW_SCALE_FILL;
  out.opacity = 1.0;
  out.backend_explicit = -1;
  TEST_CHECK_EQ_INT(xw_app_set(app, &out, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 1);

  char kept_output[64] = {0};
  int ox = 0, oy = 0;
  unsigned int ow = 0, oh = 0;
  {
    XWWindow *w = xw_app_window(app, 0);
    TEST_CHECK(w != NULL);
    snprintf(kept_output, sizeof(kept_output), "%s", w->output);
    TEST_CHECK(kept_output[0] != '\0');
    ox = w->x;
    oy = w->y;
    ow = w->width;
    oh = w->height;
  }

  /* 2a. stale -g window (matches no output/Screen/grp_) -> removed */
  XWConfig g;
  memset(&g, 0, sizeof(g));
  g.command = XW_CMD_SET;
  g.target = XW_TARGET_GEOM;
  g.have_geom = 1;
  snprintf(g.name, sizeof(g.name), "geomtest");
  g.geom_x = 0;
  g.geom_y = 0;
  g.geom_w = 320;
  g.geom_h = 200;
  set_path(&g, "tests/data/red.png");
  g.scale_mode = XW_SCALE_FILL;
  g.opacity = 1.0;
  g.backend_explicit = -1;
  TEST_CHECK_EQ_INT(xw_app_set(app, &g, &err), 0);

  /* 2b. -g Screen with a wrong rect -> re-fitted to the screen bbox */
  XWConfig s;
  memset(&s, 0, sizeof(s));
  s.command = XW_CMD_SET;
  s.target = XW_TARGET_GEOM;
  s.have_geom = 1;
  snprintf(s.name, sizeof(s.name), "Screen");
  s.geom_x = 0;
  s.geom_y = 0;
  s.geom_w = 320;
  s.geom_h = 200;
  set_path(&s, "tests/data/red.png");
  s.scale_mode = XW_SCALE_FILL;
  s.opacity = 1.0;
  s.backend_explicit = -1;
  TEST_CHECK_EQ_INT(xw_app_set(app, &s, &err), 0);

  /* 2c. -g grp_ window -> kept verbatim */
  XWConfig grp;
  memset(&grp, 0, sizeof(grp));
  grp.command = XW_CMD_SET;
  grp.target = XW_TARGET_GEOM;
  grp.have_geom = 1;
  snprintf(grp.name, sizeof(grp.name), "grp_test");
  grp.geom_x = 10;
  grp.geom_y = 10;
  grp.geom_w = 320;
  grp.geom_h = 200;
  set_path(&grp, "tests/data/red.png");
  grp.scale_mode = XW_SCALE_FILL;
  grp.opacity = 1.0;
  grp.backend_explicit = -1;
  TEST_CHECK_EQ_INT(xw_app_set(app, &grp, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 4);

  /* 3. reconfigure: no layout change on Xvfb, so the output window must
   * survive with identical geometry, Screen must equal the screen bbox
   * (== the single output rect here), grp_ untouched, stale gone */
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(xw_app_reconfigure(app, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 3);
  {
    int seen_out = 0, seen_screen = 0, seen_grp = 0;
    for (int i = 0; i < xw_app_window_count(app); i++) {
      XWWindow *w = xw_app_window(app, i);
      TEST_CHECK(w != NULL);
      if (!w)
        continue;
      if (strcmp(w->output, kept_output) == 0 && !w->is_geometry) {
        seen_out++;
        TEST_CHECK_EQ_INT(w->x, ox);
        TEST_CHECK_EQ_INT(w->y, oy);
        TEST_CHECK_EQ_INT((int)w->width, (int)ow);
        TEST_CHECK_EQ_INT((int)w->height, (int)oh);
      } else if (strcmp(w->output, "Screen") == 0) {
        seen_screen++;
        TEST_CHECK_EQ_INT(w->x, ox);
        TEST_CHECK_EQ_INT(w->y, oy);
        TEST_CHECK_EQ_INT((int)w->width, (int)ow);
        TEST_CHECK_EQ_INT((int)w->height, (int)oh);
      } else if (strcmp(w->output, "grp_test") == 0) {
        seen_grp++;
        TEST_CHECK_EQ_INT(w->x, 10);
        TEST_CHECK_EQ_INT(w->y, 10);
        TEST_CHECK_EQ_INT((int)w->width, 320);
        TEST_CHECK_EQ_INT((int)w->height, 200);
      } else {
        TEST_FAIL("unexpected window '%s'", w->output);
      }
    }
    TEST_CHECK_EQ_INT(seen_out, 1);
    TEST_CHECK_EQ_INT(seen_screen, 1);
    TEST_CHECK_EQ_INT(seen_grp, 1);
  }

  XWConfig clr;
  memset(&clr, 0, sizeof(clr));
  clr.command = XW_CMD_CLEAR;
  clr.target = XW_TARGET_ALL;
  xw_app_clear(app, &clr, &err);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);
}

static void test_reconfigure_restores_kept_last(XWApp *app) {
  TEST_BEGIN("reconfigure restores keep=1 last on output without a window");
  XWError err;
  memset(&err, 0, sizeof(err));
  TEST_CHECK(app != NULL);
  if (!app)
    return;

  /* start blank: the previous case leaves windows behind */
  XWConfig blank;
  memset(&blank, 0, sizeof(blank));
  blank.command = XW_CMD_CLEAR;
  blank.target = XW_TARGET_ALL;
  TEST_CHECK_EQ_INT(xw_app_clear(app, &blank, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);

  XWConfig out;
  memset(&out, 0, sizeof(out));
  out.command = XW_CMD_SET;
  out.target = XW_TARGET_PRIMARY;
  set_path(&out, "tests/data/red.png");
  out.scale_mode = XW_SCALE_FILL;
  out.opacity = 1.0;
  out.backend_explicit = -1;
  TEST_CHECK_EQ_INT(xw_app_set(app, &out, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 1);

  char kept_output[64] = {0};
  snprintf(kept_output, sizeof(kept_output), "%s", xw_app_window(app, 0)->output);
  TEST_CHECK(kept_output[0] != '\0');

  /* keep the last, then wipe all windows: the output is "newly-appeared"
   * as far as reconfigure is concerned */
  TEST_CHECK_EQ_INT(xw_app_keep(app, kept_output), 0);
  XWConfig clr;
  memset(&clr, 0, sizeof(clr));
  clr.command = XW_CMD_CLEAR;
  clr.target = XW_TARGET_ALL;
  TEST_CHECK_EQ_INT(xw_app_clear(app, &clr, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);

  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(xw_app_reconfigure(app, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 1);
  {
    XWWindow *w = xw_app_window(app, 0);
    TEST_CHECK(w != NULL);
    if (w) {
      TEST_CHECK_EQ_STR(w->output, kept_output);
      TEST_CHECK_EQ_INT(w->is_geometry, 0);
      TEST_CHECK_EQ_STR(w->path, "tests/data/red.png");
    }
  }

  /* without a last, reconfigure leaves the output blank */
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(xw_app_clear(app, &clr, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_reconfigure(app, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);
}

int main(void) {
  TEST_SUITE("reconfigure (Xvfb)");

  /* isolate the shared state file: reconfigure saves/restores it */
  char tmp[] = "/tmp/xw-reconf-XXXXXX";
  char *dir = mkdtemp(tmp);
  TEST_CHECK(dir != NULL);
  setenv("XDG_STATE_HOME", dir, 1);

  /* one app for the whole binary: imlib2 keeps process-global X state
   * (stale GC after XCloseDisplay), so two create/destroy cycles in one
   * process break rendering — the daemon opens its display once, same as here. */
  XWError err;
  memset(&err, 0, sizeof(err));
  XWApp *app = xw_app_create(&err);
  TEST_CHECK(app != NULL);
  if (!app)
    return 1;

  test_reconfigure_drops_geom_keeps_output(app);
  test_reconfigure_restores_kept_last(app);

  xw_app_destroy(app);

  /* best-effort cleanup of the isolated state dir */
  {
    char spath[1024];
    if (xw_state_path(spath, sizeof(spath)) == 0) {
      unlink(spath);
      char *slash = strrchr(spath, '/');
      if (slash) {
        *slash = '\0';
        rmdir(spath);
      }
    }
    rmdir(dir);
  }

  TEST_SUMMARY();
}
