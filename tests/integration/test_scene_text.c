/* Integration test: text drawing under Xvfb.
 * Renders "Hi" with the system DejaVu font at a fixed origin over a dark
 * clear, then scans the text region for bright pixels (and asserts a far
 * corner stays dark). Skips without a usable font or GLX. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/scene_gl.h"
#include "../../src/scene_model.h"
#include "../../src/scene_text.h"
#include "../../src/scene_text_gl.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static const char *find_font(void) {
  static const char *paths[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    NULL,
  };
  for (int i = 0; paths[i]; i++)
    if (access(paths[i], R_OK) == 0)
      return paths[i];
  return NULL;
}

int main(void) {
  TEST_SUITE("scene text (Xvfb)");
  TEST_BEGIN("drawn glyphs light the text region, or SKIP");

  const char *sysfont = find_font();
  if (!sysfont) {
    printf("  SKIP: no test font found\n");
    TEST_SUMMARY();
  }

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

  /* stage the font inside a temp scene dir (font paths resolve via dir) */
  if (xw_gl_bind_target(gl, -1) != 0 || xw_gl_clear(gl, 0.02f, 0.02f, 0.10f) != 0) {
    TEST_FAIL("cannot bind/clear");
    TEST_SUMMARY();
  }

  XWSceneObject o;
  memset(&o, 0, sizeof(o));
  snprintf(o.id, sizeof(o.id), "title");
  o.type = XW_OBJ_TEXT;
  o.visible = 1;
  o.origin[0] = 200.0f;
  o.origin[1] = 150.0f;
  snprintf(o.text.content, sizeof(o.text.content), "Hi");
  snprintf(o.text.font, sizeof(o.text.font), "font.ttf");
  o.text.size = 64.0f;

  char dir[] = "/tmp/xw-text-XXXXXX";
  if (!mkdtemp(dir)) {
    TEST_FAIL("cannot mkdtemp");
    TEST_SUMMARY();
  }
  char fp[1024];
  snprintf(fp, sizeof(fp), "%s/font.ttf", dir);
  FILE *src = fopen(sysfont, "rb");
  FILE *dst = fopen(fp, "wb");
  if (!src || !dst) {
    TEST_FAIL("cannot stage font");
    TEST_SUMMARY();
  }
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
    fwrite(buf, 1, n, dst);
  fclose(src);
  fclose(dst);

  XWTextItem it;
  memset(&it, 0, sizeof(it));
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(xw_scene_text_draw(gl, &o, dir, &it, 1.0f, &err), 0);
  if (err.code != 0)
    TEST_FAIL("draw: %s", err.msg);
  TEST_CHECK_EQ_INT(xw_gl_present(gl), 0);
  XSync(dpy, False);

  XImage *img = XGetImage(dpy, win->win, 200, 150, 200, 100, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    int bright = 0;
    for (int j = 0; j < 100; j++)
      for (int i = 0; i < 200; i++) {
        unsigned long px = XGetPixel(img, i, j);
        int r = (px >> 16) & 0xff;
        int g = (px >> 8) & 0xff;
        int b = px & 0xff;
        if (r > 128 && g > 128 && b > 128)
          bright++;
      }
    XDestroyImage(img);
    TEST_CHECK(bright > 10);
  }
  XImage *far =
      XGetImage(dpy, win->win, w - 50, h - 50, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(far != NULL);
  if (far) {
    unsigned long px = XGetPixel(far, 0, 0);
    XDestroyImage(far);
    TEST_CHECK(((px >> 16) & 0xff) < 60);
  }

  xw_text_item_free(gl, &it);
  xw_gl_destroy(gl);
  xw_x11_destroy_window(win);
  unlink(fp);
  rmdir(dir);
  TEST_SUMMARY();
}
