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

static int read_center_red(Display *dpy, Window win, int w, int h) {
  int cx = w / 2, cy = h / 2;
  XImage *img = XGetImage(dpy, win, cx, cy, 1, 1, AllPlanes, ZPixmap);
  if (!img)
    return 0;
  unsigned long px = XGetPixel(img, 0, 0);
  XFree(img);
  int r = (px >> 16) & 0xff;
  int g = (px >> 8) & 0xff;
  int b = px & 0xff;
  return r > 150 && g < 100 && b < 100;
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
  test_image_missing_file_resize(dpy, x, y, w, h);
  xw_x11_close_display(dpy);
  TEST_SUMMARY();
}
