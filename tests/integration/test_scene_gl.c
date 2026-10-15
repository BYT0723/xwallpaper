/* Integration test: scene GL rendering under Xvfb.
 * Loads tests/data/scene/gl (red base texture, WE-dialect pulse effect),
 * renders one frame, then reads back the center pixel. Without GLX the
 * backend stays on its clear-color fallback and the test SKIP-passes. */

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

int main(void) {
  TEST_SUITE("scene GL (Xvfb)");
  TEST_BEGIN("renders pulsing red texture, or SKIP without GLX");

  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    TEST_SUMMARY();

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

  int x, y;
  unsigned int w, h;
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL) != 0) {
    TEST_FAIL("cannot resolve primary geometry");
    TEST_SUMMARY();
  }

  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    TEST_SUMMARY();

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/gl";
  opt.fps = 1000.0; /* unthrottled for the test */

  XWBackend backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  if (err.code != 0) {
    TEST_FAIL("init: %s", err.msg);
    TEST_SUMMARY();
  }
  if (!xw_scene_has_gl(&backend)) {
    printf("  SKIP: no GLX, backend on clear-color fallback\n");
    backend.destroy(&backend);
    xw_x11_destroy_window(win);
    TEST_SUMMARY();
  }

  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  usleep(2000);
  backend.tick(&backend);
  XSync(dpy, False);

  int cx = w / 2, cy = h / 2;
  XImage *img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  int r0 = -1;
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XDestroyImage(img);
    int r = (px >> 16) & 0xff;
    int g = (px >> 8) & 0xff;
    int b = px & 0xff;
    /* red texture dimmed by the pulse factor (~0.5 at t=0): mid red */
    TEST_CHECK(r >= 80 && r <= 180);
    TEST_CHECK(g < 60 && b < 60);
    r0 = r;
  }

  /* animation progresses: ~1.6s later the pulse phase nears 1.0 (bright) */
  usleep(1600000);
  backend.tick(&backend);
  XSync(dpy, False);
  img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XDestroyImage(img);
    int r = (px >> 16) & 0xff;
    TEST_CHECK(r > r0 + 60);
  }

  backend.destroy(&backend);

  /* The object tint: WE multiplies the sampled texture by Image::color
   * (genericimage*.frag's g_Color4), which our builtin base copy replaces.
   * Grey 128 through a (0.5, 0.25, 1.0) tint must land near (64, 32, 128);
   * before this was wired the object drew untinted, i.e. (128, 128, 128). */
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/tint";
  opt.fps = 1000.0;
  backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  if (backend.init(&backend, win, &opt, &err) != 0) {
    TEST_FAIL("tint init: %s", err.msg);
  } else if (!xw_scene_has_gl(&backend)) {
    printf("  SKIP: no GLX for the tint check\n");
    backend.destroy(&backend);
  } else {
    TEST_BEGIN("object color tints the base texture");
    TEST_CHECK_EQ_INT(backend.start(&backend), 0);
    usleep(2000);
    backend.tick(&backend);
    XSync(dpy, False);
    img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XDestroyImage(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r > 54 && r < 74);
      TEST_CHECK(g > 22 && g < 42);
      TEST_CHECK(b > 118 && b < 138);
    }
    backend.destroy(&backend);
  }

  /* Two compose layers with a drawn object between them: the second samples
   * the frame as it stands, so the object must survive. Caching the snapshot
   * per frame made the second layer paint the frame from before that object,
   * which erased 2889253135's character. The clear colour is blue and the base
   * white, so a stale snapshot shows up as blue at the centre. */
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/ffb2";
  opt.fps = 1000.0;
  backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  if (backend.init(&backend, win, &opt, &err) != 0) {
    TEST_FAIL("ffb2 init: %s", err.msg);
  } else if (!xw_scene_has_gl(&backend)) {
    printf("  SKIP: no GLX for the compose-layer check\n");
    backend.destroy(&backend);
  } else {
    TEST_BEGIN("a second compose layer keeps what was drawn between them");
    TEST_CHECK_EQ_INT(backend.start(&backend), 0);
    usleep(2000);
    backend.tick(&backend);
    XSync(dpy, False);
    img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XDestroyImage(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r > 180 && g > 180 && b > 180); /* the white base, not blue */
    }
    backend.destroy(&backend);
  }

  /* Photoshop-style blends against the frame so far: screen makes black
   * transparent, multiply makes white transparent. Without them the left
   * box would composite black and the right one white over the blue base. */
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/blend";
  opt.fps = 1000.0;
  backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  if (backend.init(&backend, win, &opt, &err) != 0) {
    TEST_FAIL("blend init: %s", err.msg);
  } else if (!xw_scene_has_gl(&backend)) {
    printf("  SKIP: no GLX for the blend-mode check\n");
    backend.destroy(&backend);
  } else {
    TEST_BEGIN("screen black and multiply white stay transparent");
    TEST_CHECK_EQ_INT(backend.start(&backend), 0);
    usleep(2000);
    backend.tick(&backend);
    XSync(dpy, False);
    img = XGetImage(dpy, win->win, w / 4, h / 2, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XDestroyImage(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r < 100 && g < 100 && b > 150); /* blue, not black */
    }
    img = XGetImage(dpy, win->win, 3 * w / 4, h / 2, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XDestroyImage(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r < 100 && g < 100 && b > 150); /* blue, not white */
    }
    backend.destroy(&backend);
  }

  /* An effect pass that declares an engine-managed render target we do not
   * build (_rt_imageLayerComposite_*, per image object) must keep the base
   * image. Falling back to the sampler's annotated `default` grabbed
   * util/white and painted whole frames white on real scenes. The base here
   * is blue and the layer is transparent, so a white fallback is obvious. */
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_SCENE;
  opt.path = "tests/data/scene/rtfallback";
  opt.fps = 1000.0;
  backend = *xw_scene_backend_new();
  memset(&err, 0, sizeof(err));
  if (backend.init(&backend, win, &opt, &err) != 0) {
    TEST_FAIL("rtfallback init: %s", err.msg);
  } else if (!xw_scene_has_gl(&backend)) {
    printf("  SKIP: no GLX for the render-target fallback check\n");
    backend.destroy(&backend);
  } else {
    TEST_BEGIN("an unbuilt render target keeps the base image");
    TEST_CHECK_EQ_INT(backend.start(&backend), 0);
    usleep(2000);
    backend.tick(&backend);
    XSync(dpy, False);
    img = XGetImage(dpy, win->win, cx, cy, 1, 1, AllPlanes, ZPixmap);
    TEST_CHECK(img != NULL);
    if (img) {
      unsigned long px = XGetPixel(img, 0, 0);
      XDestroyImage(img);
      int r = (px >> 16) & 0xff;
      int g = (px >> 8) & 0xff;
      int b = px & 0xff;
      TEST_CHECK(r < 100 && g < 100 && b > 150); /* blue, not white */
    }
    backend.destroy(&backend);
  }

  xw_x11_destroy_window(win);
  TEST_SUMMARY();
}
