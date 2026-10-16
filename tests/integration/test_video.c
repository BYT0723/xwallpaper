/* Integration test: video backend under Xvfb.
 * mpv's GL renderer may not initialize under Xvfb (no GLX) — that's fine.
 * This test verifies the lifecycle contract: init + start succeed and destroy
 * is clean, i.e. the backend integrates with app without crashing. Real visual
 * verification happens on a real X session. */

#include <locale.h>
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

/* Runtime control: mpv owns volume/mute/pause/rotate, so a live instance is
 * what proves the wiring. Unsupported actions must fail loudly instead of
 * pretending to work. */
static void test_video_control(void) {
  XWError err;
  Display *dpy;
  XWConfig cfg;
  XWWindow *win;
  XWBackendOpt opt;
  XWBackend backend;
  int x, y;
  unsigned int w, h;

  TEST_BEGIN("video control drives mpv properties, rejects the unknown");
  memset(&err, 0, sizeof(err));
  dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0) {
    TEST_FAIL("cannot resolve geometry");
    xw_x11_close_display(dpy);
    return;
  }
  win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win) {
    xw_x11_close_display(dpy);
    return;
  }
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  opt.volume = 50.0;
  backend = *xw_video_backend_new();
  if (backend.init(&backend, win, &opt, &err) != 0 || !backend.control) {
    TEST_FAIL("video init: %s", err.msg);
  } else if (backend.start(&backend) != 0) {
    TEST_FAIL("video start failed");
  } else {
    /* seek needs a loaded file, so let mpv get that far first */
    usleep(300000);
    TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "toggle", &err), 0);
    TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "off", &err), 0);
    TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "on", &err), 0);
    TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "+2", &err), 0);
    TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "-2", &err), 0);
    TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "50", &err), 0);
    TEST_CHECK_EQ_INT(backend.control(&backend, "pause", "toggle", &err), 0);
    /* Under Xvfb mpv often never gets a loaded file, so seek can report a
     * runtime failure; either way it must have reached mpv rather than been
     * rejected as an unsupported action. */
    if (backend.control(&backend, "seek", "5", &err) != 0)
      TEST_CHECK(strstr(err.msg, "seek failed") != NULL);
    TEST_CHECK_EQ_INT(backend.control(&backend, "rotate", "cycle", &err), 0);
    /* bad input and unknown actions must not look like success */
    TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "abc", &err), -1);
    TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "bogus", &err), -1);
    TEST_CHECK_EQ_INT(backend.control(&backend, "wobble", "", &err), -1);
  }
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
  xw_x11_close_display(dpy);
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

/* Startup self-heal: retry() reloads when mpv has no video track, but must
 * not pile up loadfile calls — two calls right after each other must not both
 * reload (the first records last_load_ms, the second is <1.5s later). Whether
 * the first call reloads depends on mpv/Xvfb state; the anti-pileup contract
 * (second call != reload) is environment-independent. */
static void test_video_retry_antipileup(void) {
  TEST_BEGIN("retry() does not pile up reloads");
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
    xw_x11_close_display(dpy);
    return;
  }
  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win) {
    xw_x11_close_display(dpy);
    return;
  }

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  opt.volume = 50.0;

  XWBackend *tmpl = xw_video_backend_new();
  XWBackend backend = *tmpl;
  int ok = backend.init(&backend, win, &opt, &err) == 0 &&
           backend.start(&backend) == 0;
  TEST_CHECK(ok);
  if (ok) {
    backend.retry(&backend); /* first call may reload */
    int r2 = backend.retry(&backend);
    TEST_CHECK(r2 != 1); /* anti-pileup: second call never reloads */
  }
  backend.destroy(&backend);
  xw_x11_destroy_window(win);
  xw_x11_close_display(dpy);
}

/* Regression: the web backend calls gtk_init_check() in this process and GTK
 * runs setlocale(LC_ALL, ""), putting the process back into the environment's
 * numeric locale. libmpv's mpv_create() then returns NULL for any LC_NUMERIC
 * other than "C"/"C.UTF-8", so every video wallpaper set after a web wallpaper
 * failed with "mpv_create failed" and nothing was displayed. The web backend
 * must restore the C numeric locale the daemon establishes at startup. */
static void test_video_after_web_gtk_init(void) {
  TEST_BEGIN("video backend works after a web wallpaper initialized GTK");
  /* gtk_init_check() runs setlocale(LC_ALL, ""), so pin the environment it
   * reads. The daemon invariant (daemon.c) is LC_NUMERIC == "C", which is also
   * what mpv_create() demands — it merely tolerates C.UTF-8, so this asserts
   * the invariant, not only the mpv failure. */
  setenv("LC_ALL", "C.UTF-8", 1);
  setlocale(LC_ALL, "");
  /* a C runner would leave the regression below unobservable */
  TEST_CHECK(strcmp(setlocale(LC_NUMERIC, NULL), "C") != 0);
  setlocale(LC_NUMERIC, "C");

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
  snprintf(cfg.name, sizeof(cfg.name), "weblocale");
  cfg.geom_x = 0;
  cfg.geom_y = 0;
  cfg.geom_w = 400;
  cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (win) {
    XWBackendOpt wopt;
    memset(&wopt, 0, sizeof(wopt));
    wopt.type = XW_BACKEND_WEB;
    wopt.path = "tests/data/webdir";
    XWBackend *wtmpl = xw_web_backend_new();
    XWBackend web = *wtmpl;
    TEST_CHECK_EQ_INT(web.init(&web, win, &wopt, &err), 0); /* gtk_init_check() */
    TEST_CHECK_EQ_STR(setlocale(LC_NUMERIC, NULL), "C");
    web.destroy(&web);
    xw_x11_destroy_window(win);
  }
  xw_x11_close_display(dpy);

  /* video init must survive whatever the web backend did to the process */
  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_VIDEO;
  opt.path = "tests/data/bounce.mp4";
  opt.mute = 1;
  TEST_CHECK_EQ_INT(run_with_opts(&opt), 0);
}

int main(void) {
  TEST_SUITE("video backend (Xvfb)");
  test_video_lifecycle();
  test_video_default_opts();
  test_video_fps_override();
  test_video_audio_client_name();
  test_video_control();
  test_video_retry_antipileup();
  /* keep last: the only GTK user here, and it pins the process locale */
  test_video_after_web_gtk_init();
  TEST_SUMMARY();
}
