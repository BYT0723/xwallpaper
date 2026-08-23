/* Integration test: app module geometry set under Xvfb.
 * Creates the app, sets a wallpaper at -g geometry, runs briefly, clears.
 * One display connection (matches real daemon usage). */

#include <stdio.h>
#include <string.h>

#include "../harness.h"
#include "../../src/app.h"
#include "../../src/cli.h"
#include "../../src/x11.h"

static void set_path(XWConfig *cfg, const char *p) {
  snprintf(cfg->path, sizeof(cfg->path), "%s", p);
}

static void test_app_geom_set(void) {
  TEST_BEGIN("set -g creates a window at geometry, clear removes it");
  XWError err;
  memset(&err, 0, sizeof(err));

  XWApp *app = xw_app_create(&err);
  TEST_CHECK(app != NULL);
  if (!app)
    return;
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "geomtest");
  cfg.geom_x = 10;
  cfg.geom_y = 20;
  cfg.geom_w = 640;
  cfg.geom_h = 480;
  set_path(&cfg, "tests/data/red.png");
  cfg.scale_mode = XW_SCALE_CONTAIN;
  cfg.opacity = 1.0;
  cfg.backend_explicit = -1;

  TEST_CHECK_EQ_INT(xw_app_set(app, &cfg, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 1);
  XWWindow *win = xw_app_window(app, 0);
  TEST_CHECK(win != NULL);
  TEST_CHECK_EQ_INT((int)win->width, 640);
  TEST_CHECK_EQ_INT((int)win->height, 480);

  xw_app_stop(app);
  TEST_CHECK_EQ_INT(xw_app_run(app), 0);

  cfg.command = XW_CMD_CLEAR;
  cfg.target = XW_TARGET_ALL;
  TEST_CHECK_EQ_INT(xw_app_clear(app, &cfg, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);

  xw_app_destroy(app);
}

int main(void) {
  TEST_SUITE("app geom (Xvfb)");
  test_app_geom_set();
  TEST_SUMMARY();
}
