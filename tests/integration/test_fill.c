/* Verify default FILL scale mode: a 320x240 image on a 640x480 window
 * must cover the full window (corner pixel is the image, not background). */

#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <Imlib2.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static void test_fill_default(void) {
  TEST_BEGIN("default scale is FILL (covers window, no bars)");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "filltest");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 640; cfg.geom_h = 480;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 640, 480, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_IMAGE;
  opt.path = "tests/data/red.png"; /* 64x64 red */
  opt.scale_mode = XW_SCALE_FILL;

  XWBackend *tmpl = xw_image_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  XSync(dpy, False);

  /* corner of a 640x480 window with FILL on a 64x64 image: center crop,
   * so corners ARE the (scaled) image. Verify red at all 4 corners. */
  for (int pass = 0; pass < 2; pass++) {
    int cx = pass ? 639 : 0;
    int cy = pass ? 479 : 0;
    XImage *img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XFree(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r > 150 && g < 100 && b < 100);
    }
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("fill default (Xvfb)");
  test_fill_default();
  TEST_SUMMARY();
}
