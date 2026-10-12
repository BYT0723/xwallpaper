/* Integration test: scene backend shell under Xvfb.
 * Loads tests/data/scene/full (clearcolor 0.1 gray, 4 objects), mounts it on
 * a desktop window, ticks once, then reads back the center pixel to confirm
 * the clear color painted. No GL is required at this stage. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/scene.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static void test_scene_shell(void) {
  TEST_BEGIN("parses full dir, paints clear color, tick repaints");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

  int x, y;
  unsigned int w, h;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0) {
    TEST_FAIL("cannot resolve primary geometry");
    return;
  }

  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/full";
  opt.fps = 1000.0; /* effectively unthrottled for the test */

  XWBackend *tmpl = xw_scene_backend_new();
  TEST_CHECK(tmpl->tick != NULL);
  TEST_CHECK(tmpl->set_paused != NULL);
  XWBackend backend = *tmpl; /* per-window copy */
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  if (err.code != 0) {
    TEST_FAIL("init: %s", err.msg);
    return;
  }
  TEST_CHECK_EQ_INT(xw_scene_object_count(&backend), 4);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  TEST_CHECK(xw_scene_frames(&backend) >= 2);

  backend.tick(&backend);
  XSync(dpy, False);

  int cx = w / 2, cy = h / 2;
  XImage *img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XFree(img);
    int r = (px >> 16) & 0xff;
    int g = (px >> 8) & 0xff;
    int b = px & 0xff;
    int mx = r > g ? r : g;
    mx = mx > b ? mx : b;
    int mn = r < g ? r : g;
    mn = mn < b ? mn : b;
    TEST_CHECK(mx - mn <= 2);
    TEST_CHECK(mx >= 10 && mx <= 60);
  }

  /* paused tick must not paint */
  unsigned long before = xw_scene_frames(&backend);
  backend.set_paused(&backend, 1);
  usleep(2000);
  backend.tick(&backend);
  TEST_CHECK_EQ_INT((int)(xw_scene_frames(&backend) - before), 0);
  backend.set_paused(&backend, 0);

  /* reload a different dir */
  XWBackendOpt opt2;
  memset(&opt2, 0, sizeof(opt2));
  opt2.type = XW_BACKEND_SCENE;
  opt2.path = "tests/data/scene";
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(backend.reload(&backend, &opt2, &err), 0);
  TEST_CHECK_EQ_INT(xw_scene_object_count(&backend), 2);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_scene_bad_dir(void) {
  TEST_BEGIN("missing dir fails init without crashing");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  int x, y;
  unsigned int w, h;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0) {
    TEST_FAIL("cannot resolve primary geometry");
    return;
  }
  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "/nonexistent/scene-dir";
  XWBackend backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), -1);
  TEST_CHECK(err.code != 0);
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("scene backend (Xvfb)");
  test_scene_shell();
  test_scene_bad_dir();
  TEST_SUMMARY();
}
