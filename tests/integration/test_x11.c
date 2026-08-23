/* Integration test for the x11 module, run under Xvfb.
 * Verifies: display opens, geometry resolves, desktop-type window is created
 * with the right size and props, and destruction is clean. */

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>

#include "../harness.h"
#include "../../src/cli.h"
#include "../../src/x11.h"

static void test_open_display(void) {
  TEST_BEGIN("opens display and lists outputs");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;
  xw_x11_list_outputs(dpy, stdout);
}

static void test_resolve_primary(void) {
  TEST_BEGIN("resolves primary output geometry");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;

  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_OUTPUT_NAME_LEN] = {0};
  TEST_CHECK_EQ_INT(xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK(w > 0 && h > 0);
  TEST_CHECK(name[0] != '\0');
}

static void test_resolve_geometry_override(void) {
  TEST_BEGIN("geometry override wins over output");
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
  cfg.geom_x = 50;
  cfg.geom_y = 60;
  cfg.geom_w = 800;
  cfg.geom_h = 600;

  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  TEST_CHECK_EQ_INT(xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, NULL), 0);
  TEST_CHECK_EQ_INT(x, 50);
  TEST_CHECK_EQ_INT(y, 60);
  TEST_CHECK_EQ_INT(w, 800);
  TEST_CHECK_EQ_INT(h, 600);
}

static void test_create_window(void) {
  TEST_BEGIN("creates desktop-type window at geometry");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  cfg.below = 1;
  cfg.sticky = 1;
  cfg.opacity = 1.0;

  int x, y;
  unsigned int w, h;
  char name[XW_OUTPUT_NAME_LEN] = {0};
  if (xw_x11_resolve_geometry(dpy, &cfg, &x, &y, &w, &h, name) != 0) {
    TEST_FAIL("cannot resolve primary geometry");
    return;
  }

  XWWindow *win = xw_x11_create_window(dpy, &cfg, x, y, w, h, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  /* verify window props */
  XWindowAttributes attr;
  TEST_CHECK(XGetWindowAttributes(dpy, win->win, &attr) != 0);
  TEST_CHECK_EQ_INT((int)attr.width, (int)w);
  TEST_CHECK_EQ_INT((int)attr.height, (int)h);
  TEST_CHECK(attr.override_redirect != 0);

  /* verify _NET_WM_WINDOW_TYPE == DESKTOP */
  Atom type;
  int fmt;
  unsigned long nitems, bytes;
  unsigned char *buf = NULL;
  Atom desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
  Atom wtype = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  int ok = XGetWindowProperty(dpy, win->win, wtype, 0, 1, False, XA_ATOM,
                              &type, &fmt, &nitems, &bytes, &buf) == Success &&
           type == XA_ATOM && nitems > 0 &&
           *(Atom *)buf == desktop;
  if (buf)
    XFree(buf);
  TEST_CHECK(ok);

  xw_x11_destroy_window(win);
  XSync(dpy, False);
}

/* Regression test: the second wallpaper window must be a sibling of the
 * first (under root), never a child of it. Otherwise the second window is
 * hidden inside the first. (Was caused by find_desktop_layer matching our
 * own DESKTOP-type window.) */
static void test_two_windows_siblings(void) {
  TEST_BEGIN("two wallpaper windows are siblings, not nested");
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
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *w1 = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(w1 != NULL);
  cfg.geom_x = 500;
  XWWindow *w2 = xw_x11_create_window(dpy, &cfg, 500, 0, 400, 300, &err);
  TEST_CHECK(w2 != NULL);
  if (!w1 || !w2) {
    if (w1) xw_x11_destroy_window(w1);
    if (w2) xw_x11_destroy_window(w2);
    return;
  }
  XSync(dpy, False);

  Window troot, parent1, parent2, *children = NULL;
  unsigned int n = 0;
  TEST_CHECK(XQueryTree(dpy, w1->win, &troot, &parent1, &children, &n) != 0);
  TEST_CHECK(XQueryTree(dpy, w2->win, &troot, &parent2, &children, &n) != 0);
  TEST_CHECK_EQ_INT((int)parent1, (int)parent2);
  if (children)
    XFree(children);

  xw_x11_destroy_window(w1);
  xw_x11_destroy_window(w2);
  XSync(dpy, False);
}

int main(void) {
  TEST_SUITE("x11 integration (Xvfb)");
  test_open_display();
  test_resolve_primary();
  test_resolve_geometry_override();
  test_create_window();
  test_two_windows_siblings();
  TEST_SUMMARY();
}
