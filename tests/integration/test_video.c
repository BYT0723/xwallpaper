/* Integration test: video backend under Xvfb.
 * mpv's GL renderer may not initialize under Xvfb (no GLX) — that's fine.
 * This test verifies the lifecycle contract: init + start succeed and destroy
 * is clean, i.e. the backend integrates with app without crashing. Real visual
 * verification happens on a real X session. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

/* create a temp input.conf-style keybind file, return a writable static path */
static const char *make_keybinds_file(void) {
  static const char *path = "/tmp/xw_test_keys.conf";
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "p cycle pause\n");
    fprintf(f, "R cycle-values video-rotate 0 90 180 270 0\n");
    fclose(f);
  }
  return path;
}

static void test_video_lifecycle(void) {
  TEST_BEGIN("video backend init/start/destroy");
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
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  opt.volume = 50.0;
  opt.rotate = 90;
  opt.keybinds = make_keybinds_file();

  XWBackend *tmpl = xw_video_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  /* let mpv run briefly */
  usleep(200000);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_video_default_opts(void) {
  TEST_BEGIN("video init with default rotate/keybinds");
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
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;

  XWBackend *tmpl = xw_video_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  usleep(100000);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("video backend (Xvfb)");
  test_video_lifecycle();
  test_video_default_opts();
  TEST_SUMMARY();
}
