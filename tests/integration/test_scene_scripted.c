/* Integration test: scripted scene backend under Xvfb.
 * A scripted dir inits with a live engine that keeps ticking across
 * frames; a syntax-broken script fails init fast. */

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

static XWWindow *open_window(Display *dpy, XWError *err) {
  XWConfig cfg;
  int x, y;
  unsigned int w, h;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0)
    return NULL;
  return xw_x11_create_window(dpy, &cfg, x, y, w, h, err);
}

static void test_scripted_runs(void) {
  TEST_BEGIN("scripted dir inits live engine, ticks advance frames");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  XWWindow *win = open_window(dpy, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;
  XWConfig cfg0;
  int gx, gy;
  unsigned int w, h;
  memset(&cfg0, 0, sizeof(cfg0));
  cfg0.target = XW_TARGET_PRIMARY;
  TEST_CHECK_EQ_INT(
      xw_x11_resolve_geometry(dpy, &cfg0, &gx, &gy, &w, &h, NULL), 0);

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/scripted";
  opt.fps = 1000.0;

  XWBackend backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  if (err.code != 0) {
    TEST_FAIL("init: %s", err.msg);
    return;
  }
  TEST_CHECK_EQ_INT(xw_scene_script_active(&backend), 1);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  unsigned long f0 = xw_scene_frames(&backend);
  usleep(5000);
  backend.tick(&backend);
  usleep(5000);
  backend.tick(&backend);
  XSync(dpy, False);
  TEST_CHECK(xw_scene_frames(&backend) > f0);
  {
    /* script overrode opacity to 0.5: dimmed red over the dark clear */
    int cx = w / 2, cy = h / 2;
    XImage *img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XDestroyImage(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r >= 90 && r <= 170);
      TEST_CHECK(g < 40 && b < 60);
    }
  }
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_unscripted_idle(void) {
  TEST_BEGIN("scriptless dir has no engine but still paints");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  XWWindow *win = open_window(dpy, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/gl";
  opt.fps = 1000.0;

  XWBackend backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  if (err.code != 0) {
    TEST_FAIL("init: %s", err.msg);
    return;
  }
  TEST_CHECK_EQ_INT(xw_scene_script_active(&backend), 0);
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_bad_script_fails(void) {
  TEST_BEGIN("syntax-broken script fails init fast");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  XWWindow *win = open_window(dpy, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/scripted-bad";

  XWBackend backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), -1);
  TEST_CHECK(err.code != 0);
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("scene scripting (Xvfb)");
  test_scripted_runs();
  test_unscripted_idle();
  test_bad_script_fails();
  TEST_SUMMARY();
}
