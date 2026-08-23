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

/* run a full backend lifecycle (init+start+destroy) with the given opts
 * under Xvfb; returns 0 on success, -1 if init or start fails */
static int run_with_opts(const XWBackendOpt *opt) {
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  if (!dpy)
    return -1;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

  int x, y;
  unsigned int w, h;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0) {
    xw_x11_close_display(dpy);
    return -1;
  }

  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  if (!win) {
    xw_x11_close_display(dpy);
    return -1;
  }

  XWBackend *tmpl = xw_video_backend_new();
  XWBackend backend = *tmpl;
  int rc = 0;
  if (backend.init(&backend, win, opt, &err) != 0 || backend.start(&backend) != 0)
    rc = -1;

  usleep(100000);
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
  xw_x11_close_display(dpy);
  return rc;
}

static void test_video_lifecycle(void) {
  TEST_BEGIN("video backend init/start/destroy");
  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  opt.volume = 50.0;
  opt.rotate = 90;
  opt.keybinds = make_keybinds_file();
  TEST_CHECK_EQ_INT(run_with_opts(&opt), 0);
}

static void test_video_default_opts(void) {
  TEST_BEGIN("video init with default rotate/keybinds/fps");
  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  TEST_CHECK_EQ_INT(run_with_opts(&opt), 0);
}

static void test_video_fps_override(void) {
  TEST_BEGIN("video init with forced fps");
  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  opt.fps = 30.0;
  TEST_CHECK_EQ_INT(run_with_opts(&opt), 0);
}

static void test_video_audio_client_name(void) {
  TEST_BEGIN("video init with custom audio client name");
  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  opt.audio_client_name = "xwallpaper";
  TEST_CHECK_EQ_INT(run_with_opts(&opt), 0);
}

int main(void) {
  TEST_SUITE("video backend (Xvfb)");
  test_video_lifecycle();
  test_video_default_opts();
  test_video_fps_override();
  test_video_audio_client_name();
  TEST_SUMMARY();
}
