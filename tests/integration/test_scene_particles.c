/* Integration test: particle rendering under Xvfb.
 * Drives an emitter at the window center for several frames (red sprite,
 * additive-ish translucent points over a dark clear) and reads back the
 * center pixel. Without GLX the test SKIP-passes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/scene_gl.h"
#include "../../src/scene_model.h"
#include "../../src/scene_particle.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

int main(void) {
  TEST_SUITE("scene particles (Xvfb)");
  TEST_BEGIN("center emitter lights the center pixel, or SKIP without GLX");

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

  XWGl *gl = NULL;
  memset(&err, 0, sizeof(err));
  if (xw_gl_create(&gl, dpy, win->win, (int)w, (int)h, &err) != 0 || !gl) {
    printf("  SKIP: no GLX (%s)\n", err.msg);
    xw_x11_destroy_window(win);
    TEST_SUMMARY();
  }

  XWSceneObject o;
  memset(&o, 0, sizeof(o));
  snprintf(o.id, sizeof(o.id), "puff");
  o.type = XW_OBJ_PARTICLE;
  o.visible = 1;
  o.origin[0] = (float)w / 2.0f;
  o.origin[1] = (float)h / 2.0f;
  o.particle.rate = 240.0f;
  o.particle.max_count = 2000;
  o.particle.life = 1.5f;
  o.particle.speed = 60.0f;
  o.particle.spread = 0.5f;
  o.particle.size_min = 3.0f;
  o.particle.size_max = 6.0f;
  o.particle.color[0] = 1.0f;
  o.particle.color[3] = 1.0f;
  o.particle.gravity = 0.0f;
  snprintf(o.particle.sprite, sizeof(o.particle.sprite), "textures/bg.png");

  XWEmitter e;
  memset(&e, 0, sizeof(e));
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &o.particle), 0);

  memset(&err, 0, sizeof(err));
  for (int f = 0; f < 10; f++) {
    TEST_CHECK_EQ_INT(xw_gl_bind_target(gl, -1), 0);
    TEST_CHECK_EQ_INT(xw_gl_clear(gl, 0.02f, 0.02f, 0.10f), 0);
    if (xw_scene_particle_render(gl, &o, &e, "tests/data/scene/gl", 1.0f / 30.0f,
                                 &err) != 0) {
      TEST_FAIL("render: %s", err.msg);
      break;
    }
  }
  TEST_CHECK(e.alive > 50);
  TEST_CHECK_EQ_INT(xw_gl_present(gl), 0);
  XSync(dpy, False);

  XImage *img = XGetImage(dpy, win->win, w / 2, h / 2, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XDestroyImage(img);
    int r = (px >> 16) & 0xff;
    int g = (px >> 8) & 0xff;
    int b = px & 0xff;
    TEST_CHECK(r >= 80);
    TEST_CHECK(g < 80 && b < 80);
  }

  xw_emitter_free(&e);
  xw_gl_destroy(gl);
  xw_x11_destroy_window(win);
  TEST_SUMMARY();
}
