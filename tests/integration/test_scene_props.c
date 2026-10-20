/* Integration test: scene user-property hooks under Xvfb.
 * full/ carries three parsed props plus a script engine; list shows them
 * with overrides applied and set rejects unknown names. gl/ has neither
 * props nor script: list is empty, set fails. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/props_store.h"
#include "../../src/scene.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static void isolate_props_state(void) {
  static int seq = 0;
  char dir[256], cmd[512];
  snprintf(dir, sizeof(dir), "/tmp/opencode/xw-scene-integ-%d-%d", (int)getpid(), seq++);
  snprintf(cmd, sizeof(cmd), "rm -rf %s; mkdir -p %s", dir, dir);
  (void)system(cmd);
  setenv("XDG_STATE_HOME", dir, 1);
}

static XWBackend *open_scene(Display *dpy, const char *dir, XWWindow **winp) {
  static XWBackend backend;
  XWConfig cfg;
  XWError err;
  int x, y;
  unsigned int w, h;
  XWBackendOpt opt;
  memset(&err, 0, sizeof(err));
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0)
    return NULL;
  *winp = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  if (!*winp)
    return NULL;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = dir;
  opt.fps = 1000.0;
  backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  if (backend.init(&backend, *winp, &opt, &err) != 0)
    return NULL;
  return &backend;
}

static void test_list_and_set(void) {
  TEST_BEGIN("lists parsed props, set overrides one, unknown fails");
  XWError err;
  memset(&err, 0, sizeof(err));
  isolate_props_state();
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  XWWindow *win = NULL;
  XWBackend *b = open_scene(dpy, "tests/data/scene/full", &win);
  TEST_CHECK(b != NULL && win != NULL);
  if (!b || !win)
    return;

  char out[2048];
  memset(out, 0, sizeof(out));
  TEST_CHECK_EQ_INT(b->list_props(b, out, sizeof(out)), 3);
  TEST_CHECK(strstr(out, "opacity\tslider\t") != NULL);
  TEST_CHECK(strstr(out, "rain\tbool\ttrue") != NULL);
  TEST_CHECK(strstr(out, "schemecolor\tcolor\t0.15 0.23 0.4") != NULL);

  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(b->set_prop(b, "rain", "0", &err), 0);
  memset(out, 0, sizeof(out));
  TEST_CHECK_EQ_INT(b->list_props(b, out, sizeof(out)), 3);
  TEST_CHECK(strstr(out, "rain\tbool\t0") != NULL);

  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(b->set_prop(b, "ghost", "1", &err), -1);
  TEST_CHECK(err.code != 0);
  TEST_CHECK_EQ_INT(b->set_prop(NULL, "rain", "1", &err), -1);
  TEST_CHECK_EQ_INT(b->list_props(NULL, out, sizeof(out)), -1);

  b->destroy(b);
  xw_x11_destroy_window(win);
}

static void test_reload_keeps_and_disk_restores(void) {
  TEST_BEGIN("reload keeps override, destroy+init restores via disk, clear resets");
  XWError err;
  memset(&err, 0, sizeof(err));
  isolate_props_state();
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  XWWindow *win = NULL;
  XWBackend *b = open_scene(dpy, "tests/data/scene/full", &win);
  TEST_CHECK(b != NULL && win != NULL);
  if (!b || !win)
    return;

  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(b->set_prop(b, "rain", "0", &err), 0);

  /* same-wallpaper reload must keep the override (used to drop it) */
  {
    XWBackendOpt opt;
    memset(&opt, 0, sizeof(opt));
    opt.type = XW_BACKEND_SCENE;
    opt.path = "tests/data/scene/full";
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(b->reload(b, &opt, &err), 0);
  }
  char out[2048];
  memset(out, 0, sizeof(out));
  TEST_CHECK_EQ_INT(b->list_props(b, out, sizeof(out)), 3);
  TEST_CHECK(strstr(out, "rain\tbool\t0") != NULL);

  /* destroy+init restores from disk (not just memory) */
  b->destroy(b);
  XWWindow *win2 = NULL;
  XWBackend *b2 = open_scene(dpy, "tests/data/scene/full", &win2);
  TEST_CHECK(b2 != NULL && win2 != NULL);
  if (!b2 || !win2) {
    xw_x11_destroy_window(win);
    return;
  }
  xw_x11_destroy_window(win);
  memset(out, 0, sizeof(out));
  TEST_CHECK_EQ_INT(b2->list_props(b2, out, sizeof(out)), 3);
  TEST_CHECK(strstr(out, "rain\tbool\t0") != NULL);

  /* clear drops memory+disk and returns to parsed default (true) */
  memset(&err, 0, sizeof(err));
  TEST_CHECK(b2->clear_props != NULL);
  TEST_CHECK_EQ_INT(b2->clear_props(b2, &err), 0);
  memset(out, 0, sizeof(out));
  TEST_CHECK_EQ_INT(b2->list_props(b2, out, sizeof(out)), 3);
  TEST_CHECK(strstr(out, "rain\tbool\ttrue") != NULL);

  b2->destroy(b2);
  xw_x11_destroy_window(win2);
  XWWindow *win3 = NULL;
  XWBackend *b3 = open_scene(dpy, "tests/data/scene/full", &win3);
  TEST_CHECK(b3 != NULL && win3 != NULL);
  if (b3 && win3) {
    memset(out, 0, sizeof(out));
    TEST_CHECK_EQ_INT(b3->list_props(b3, out, sizeof(out)), 3);
    TEST_CHECK(strstr(out, "rain\tbool\ttrue") != NULL);
    b3->destroy(b3);
    xw_x11_destroy_window(win3);
  }
}

static void test_propless(void) {
  TEST_BEGIN("propless wall: empty list, set fails");
  XWError err;
  memset(&err, 0, sizeof(err));
  isolate_props_state();
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  XWWindow *win = NULL;
  XWBackend *b = open_scene(dpy, "tests/data/scene/gl", &win);
  TEST_CHECK(b != NULL && win != NULL);
  if (!b || !win)
    return;

  char out[2048];
  memset(out, 0, sizeof(out));
  TEST_CHECK_EQ_INT(b->list_props(b, out, sizeof(out)), 0);
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(b->set_prop(b, "rain", "1", &err), -1);
  TEST_CHECK(err.code != 0);

  b->destroy(b);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("scene props (Xvfb)");
  test_list_and_set();
  test_reload_keeps_and_disk_restores();
  test_propless();
  TEST_SUMMARY();
}
