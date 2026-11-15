/* Integration test: image backend under Xvfb.
 * Loads a 64x64 red PNG, mounts it on a desktop window, then reads back a
 * pixel from the window backing store to confirm the image rendered.
 * Also locks in the resize/reload lifecycle: a resize re-scales and stays
 * red, same-type reload with a mode switch still renders, and a resize
 * after the source file was deleted neither crashes nor blanks the window.
 * One shared Display for the whole binary: imlib keeps process-global X
 * state per Display, so a second connection in-process breaks its SHM/GC
 * handling (same reason the daemon uses a single Display). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <Imlib2.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static int read_pixel_rgb(Display *dpy, Window win, int x, int y, int *r,
                          int *g, int *b) {
  XImage *img = XGetImage(dpy, win, x, y, 1, 1, AllPlanes, ZPixmap);
  if (!img)
    return -1;
  unsigned long px = XGetPixel(img, 0, 0);
  XFree(img);
  *r = (int)((px >> 16) & 0xff);
  *g = (int)((px >> 8) & 0xff);
  *b = (int)(px & 0xff);
  return 0;
}

static int read_center_red(Display *dpy, Window win, int w, int h) {
  int r, g, b;
  if (read_pixel_rgb(dpy, win, w / 2, h / 2, &r, &g, &b) != 0)
    return 0;
  return r > 150 && g < 100 && b < 100;
}

static int pixel_is_blue(Display *dpy, Window win, int x, int y) {
  int r, g, b;
  if (read_pixel_rgb(dpy, win, x, y, &r, &g, &b) != 0)
    return 0;
  return b > 150 && r < 100 && g < 100;
}

static int pixel_is_red(Display *dpy, Window win, int x, int y) {
  int r, g, b;
  if (read_pixel_rgb(dpy, win, x, y, &r, &g, &b) != 0)
    return 0;
  return r > 150 && g < 100 && b < 100;
}

/* 2x1 PPM: left red, right blue. A stretched resize must put blue on the
 * right of the new area; stale pixels or tiled background stay red there. */
static int write_two_color_ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  static const unsigned char px[6] = {255, 0, 0, 0, 0, 255};
  if (!f)
    return -1;
  if (fputs("P6\n2 1\n255\n", f) < 0 || fwrite(px, 1, sizeof(px), f) != sizeof(px)) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}

static void send_resize(XWBackend *backend, int w, int h) {
  XEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = ConfigureNotify;
  ev.xconfigure.width = w;
  ev.xconfigure.height = h;
  backend->on_event(backend, &ev);
}

static void test_image_render(Display *dpy, int x, int y, unsigned int w,
                              unsigned int h) {
  TEST_BEGIN("renders red image onto desktop window");
  XWError err;
  memset(&err, 0, sizeof(err));

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

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
  TEST_CHECK(read_center_red(dpy, win->win, (int)w, (int)h));

  /* a resize re-scales and the window stays red */
  unsigned int w2 = w > 200 ? w - 100 : w, h2 = h > 200 ? h - 60 : h;
  send_resize(&backend, (int)w2, (int)h2);
  XSync(dpy, False);
  TEST_CHECK(read_center_red(dpy, win->win, (int)w2, (int)h2));

  /* same-type reload with a mode switch still renders */
  XWBackendOpt ropt = opt;
  ropt.scale_mode = XW_SCALE_TILE;
  TEST_CHECK_EQ_INT(backend.reload(&backend, &ropt, &err), 0);
  XSync(dpy, False);
  TEST_CHECK(read_center_red(dpy, win->win, (int)w2, (int)h2));
  ropt.scale_mode = XW_SCALE_FILL;
  TEST_CHECK_EQ_INT(backend.reload(&backend, &ropt, &err), 0);
  XSync(dpy, False);
  TEST_CHECK(read_center_red(dpy, win->win, (int)w2, (int)h2));

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_image_background_repaint(Display *dpy, int x, int y,
                                          unsigned int w, unsigned int h) {
  TEST_BEGIN("X server repaints an exposed area from the window background");
  XWError err;
  memset(&err, 0, sizeof(err));

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

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
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  XSync(dpy, False);
  TEST_CHECK(read_center_red(dpy, win->win, (int)w, (int)h));

  /* Clear without exposures: the client is not told, so only a window
   * background can put the wallpaper back. */
  XClearArea(dpy, win->win, 0, 0, 0, 0, False);
  XSync(dpy, False);
  TEST_CHECK(read_center_red(dpy, win->win, (int)w, (int)h));

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_image_resize_rescales(Display *dpy, int x, int y) {
  TEST_BEGIN("a real resize re-scales instead of keeping stale pixels");
  XWError err;
  memset(&err, 0, sizeof(err));

  char tmp[64];
  snprintf(tmp, sizeof(tmp), "/tmp/xwtest-2c-%d.ppm", (int)getpid());
  if (write_two_color_ppm(tmp) != 0) {
    TEST_FAIL("cannot stage two-color image");
    return;
  }

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win) {
    unlink(tmp);
    return;
  }

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_IMAGE;
  opt.path = tmp;
  opt.scale_mode = XW_SCALE_STRETCH;

  XWBackend *tmpl = xw_image_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  XSync(dpy, False);
  TEST_CHECK(pixel_is_red(dpy, win->win, 50, 150));
  TEST_CHECK(pixel_is_blue(dpy, win->win, 350, 150));

  /* grow the window for real, then hand the backend the configure event the
   * daemon loop would deliver; (600,500) lies outside the old 400x300 area */
  XResizeWindow(dpy, win->win, 800, 600);
  XSync(dpy, False);
  send_resize(&backend, 800, 600);
  XSync(dpy, False);
  TEST_CHECK(pixel_is_blue(dpy, win->win, 600, 500));

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
  unlink(tmp);
}

static void test_image_missing_file_resize(Display *dpy, int x, int y,
                                           unsigned int w, unsigned int h) {
  TEST_BEGIN("resize with deleted file keeps the last bitmap");
  XWError err;
  memset(&err, 0, sizeof(err));

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  char tmp[64];
  snprintf(tmp, sizeof(tmp), "/tmp/xwtest-red-%d.png", (int)getpid());
  {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "cp tests/data/red.png %s", tmp);
    if (system(cmd) != 0) {
      TEST_FAIL("cannot stage temp image");
      return;
    }
  }

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_IMAGE;
  opt.path = tmp;
  opt.scale_mode = XW_SCALE_STRETCH;

  XWBackend *tmpl = xw_image_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  XSync(dpy, False);
  TEST_CHECK(read_center_red(dpy, win->win, (int)w, (int)h));

  unlink(tmp);
  unsigned int w2 = w > 200 ? w - 100 : w, h2 = h > 200 ? h - 60 : h;
  send_resize(&backend, (int)w2, (int)h2);
  XSync(dpy, False);
  /* no crash, and the stale bitmap is still on screen */
  TEST_CHECK(read_center_red(dpy, win->win, (int)w2, (int)h2));

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("image backend (Xvfb)");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  if (!dpy) {
    TEST_FAIL("cannot open display");
    TEST_SUMMARY();
  }

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  int x, y;
  unsigned int w, h;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0) {
    TEST_FAIL("cannot resolve primary geometry");
    TEST_SUMMARY();
  }

  test_image_render(dpy, x, y, w, h);
  test_image_background_repaint(dpy, x, y, w, h);
  test_image_resize_rescales(dpy, x, y);
  test_image_missing_file_resize(dpy, x, y, w, h);
  xw_x11_close_display(dpy);
  TEST_SUMMARY();
}
