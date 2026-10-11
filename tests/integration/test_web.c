/* Integration test: web backend under Xvfb.
 * Verifies webkit actually paints into the wallpaper window: creates the
 * app window, loads a wallpaper *directory* (served via the xw:// scheme,
 * exercising module + fetch + css subresources), pumps the GTK context
 * (the pattern the daemon uses), then samples a pixel. */

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static void test_web_paints(void) {
  TEST_BEGIN("web backend paints non-black pixels");
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
  snprintf(cfg.name, sizeof(cfg.name), "webtest");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/webdir"; /* index.html: module + fetch paint #006699 */

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  /* pump GTK + let webkit render for up to ~3s */
  gint64 deadline = g_get_monotonic_time() + 3 * 1000 * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    XSync(dpy, False);
    g_usleep(10000);
  }
  XSync(dpy, False);

  /* sample the center pixel; page bg is #006699 so blue should dominate */
  XImage *img = XGetImage(dpy, win->win, 200, 150, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XFree(img);
    int r = (px >> 16) & 0xff;
    int g = (px >> 8) & 0xff;
    int b = px & 0xff;
    TEST_CHECK(b > 100 && b > r && b > g); /* blue-dominant = page bg */
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("web backend (Xvfb)");
  test_web_paints();
  TEST_SUMMARY();
}
