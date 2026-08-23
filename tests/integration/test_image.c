/* Integration test: image backend under Xvfb.
 * Loads a 64x64 red PNG, mounts it on a desktop window, then reads back a
 * pixel from the window backing store to confirm the image rendered. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <Imlib2.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static void test_image_render(void) {
  TEST_BEGIN("renders red image onto desktop window");
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
  opt.type = XW_BACKEND_IMAGE;
  opt.path = "tests/data/red.png";
  opt.scale_mode = XW_SCALE_STRETCH;

  XWBackend *tmpl = xw_image_backend_new();
  XWBackend backend = *tmpl; /* per-window copy */
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  XSync(dpy, False);

  /* read a pixel from the center of the window via XGetImage */
  int cx = w / 2, cy = h / 2;
  XImage *img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XFree(img);
    int r = (px >> 16) & 0xff;
    int g = (px >> 8) & 0xff;
    int b = px & 0xff;
    /* red pixel: R high, G/B low */
    TEST_CHECK(r > 150 && g < 100 && b < 100);
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("image backend (Xvfb)");
  test_image_render();
  TEST_SUMMARY();
}
