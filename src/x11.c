#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include "x11.h"
#include "cli.h"

#define ATOM(a) XInternAtom(dpy, #a, False)

static Display *shared_dpy = NULL;

Display *xw_x11_open_display(XWError *err) {
  if (shared_dpy)
    return shared_dpy;
  shared_dpy = XOpenDisplay(NULL);
  if (!shared_dpy) {
    XW_ERROR_SET(err, 1, "could not open X display (is DISPLAY set?)");
    return NULL;
  }
  return shared_dpy;
}

void xw_x11_close_display(Display *dpy) {
  if (dpy) {
    XCloseDisplay(dpy);
    if (dpy == shared_dpy)
      shared_dpy = NULL;
  }
}

/* ---- xrandr monitor resolution ---- */

static int xrandr_lookup(Display *dpy, Window root, const char *name,
                         int *x, int *y, unsigned int *w, unsigned int *h) {
  XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
  if (!res)
    return -1;

  int found = 0;
  for (int i = 0; i < res->noutput && !found; i++) {
    XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
    if (!oi)
      continue;
    if (oi->crtc && oi->connection == RR_Connected &&
        strcmp(oi->name, name) == 0) {
      XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
      if (ci) {
        *x = ci->x;
        *y = ci->y;
        *w = ci->width;
        *h = ci->height;
        found = 1;
        XRRFreeCrtcInfo(ci);
      }
    }
    XRRFreeOutputInfo(oi);
  }
  XRRFreeScreenResources(res);
  return found ? 0 : -1;
}

static int xrandr_primary(Display *dpy, Window root,
                          int *x, int *y, unsigned int *w, unsigned int *h,
                          char *out_name) {
  RROutput prim = XRRGetOutputPrimary(dpy, root);
  XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
  if (!res)
    return -1;

  int found = 0;
  /* prefer the primary output if it has a crtc */
  if (prim) {
    for (int i = 0; i < res->noutput; i++) {
      if (res->outputs[i] == prim) {
        XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, prim);
        if (oi && oi->crtc) {
          XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
          if (ci) {
            *x = ci->x; *y = ci->y; *w = ci->width; *h = ci->height;
            if (out_name)
              snprintf(out_name, XW_OUTPUT_NAME_LEN, "%s", oi->name);
            found = 1;
            XRRFreeCrtcInfo(ci);
          }
        }
        if (oi)
          XRRFreeOutputInfo(oi);
        break;
      }
    }
  }
  /* fallback: first connected output with a crtc */
  if (!found) {
    for (int i = 0; i < res->noutput && !found; i++) {
      XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
      if (!oi)
        continue;
      if (oi->crtc && oi->connection == RR_Connected) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
        if (ci) {
          *x = ci->x; *y = ci->y; *w = ci->width; *h = ci->height;
          if (out_name)
            snprintf(out_name, XW_OUTPUT_NAME_LEN, "%s", oi->name);
          found = 1;
          XRRFreeCrtcInfo(ci);
        }
      }
      XRRFreeOutputInfo(oi);
    }
  }
  XRRFreeScreenResources(res);
  return found ? 0 : -1;
}

int xw_x11_resolve_geometry(Display *dpy, const XWConfig *cfg,
                            int *x, int *y, unsigned int *w, unsigned int *h,
                            char *out_name) {
  Window root = RootWindow(dpy, DefaultScreen(dpy));

  if (cfg->have_geom) {
    *x = cfg->geom_x;
    *y = cfg->geom_y;
    *w = cfg->geom_w;
    *h = cfg->geom_h;
    if (out_name)
      out_name[0] = '\0';
    return 0;
  }

  if (cfg->target == XW_TARGET_OUTPUT) {
    if (xrandr_lookup(dpy, root, cfg->output, x, y, w, h) == 0) {
      if (out_name)
        snprintf(out_name, XW_OUTPUT_NAME_LEN, "%s", cfg->output);
      return 0;
    }
    return -1; /* unknown output */
  }

  /* primary or all (all handled by caller loop) */
  return xrandr_primary(dpy, root, x, y, w, h, out_name);
}

void xw_x11_list_outputs(Display *dpy, FILE *out) {
  Window root = RootWindow(dpy, DefaultScreen(dpy));
  XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
  if (!res) {
    fprintf(out, "  (no xrandr outputs available)\n");
    return;
  }
  for (int i = 0; i < res->noutput; i++) {
    XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
    if (!oi)
      continue;
    if (oi->connection == RR_Connected && oi->crtc) {
      XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
      if (ci) {
        fprintf(out, "  %s %ux%u+%d+%d\n",
                oi->name, ci->width, ci->height, ci->x, ci->y);
        XRRFreeCrtcInfo(ci);
      }
    } else {
      fprintf(out, "  %s (disconnected)\n", oi->name);
    }
    XRRFreeOutputInfo(oi);
  }
  XRRFreeScreenResources(res);
}

/* ---- desktop layer discovery ---- */

static Window find_desktop_layer(Display *dpy, Window root) {
  Window troot, parent, *children = NULL;
  unsigned int n = 0;
  Atom type = None;
  int format;
  unsigned long nitems, bytes;
  unsigned char *buf = NULL;
  Window desktop = 0;

  if (!XQueryTree(dpy, root, &troot, &parent, &children, &n) || n == 0) {
    if (children)
      XFree(children);
    return 0;
  }
  for (unsigned int i = 0; i < n; i++) {
    /* skip our own wallpaper windows: they are also DESKTOP type but must
     * never be treated as the layer to nest under (would nest window 2
     * inside window 1 and hide it). Identified by WM_CLASS. */
    XClassHint hint;
    if (XGetClassHint(dpy, children[i], &hint)) {
      int is_ours = hint.res_class &&
                    strcmp(hint.res_class, "xwallpaper") == 0;
      if (hint.res_name)
        XFree(hint.res_name);
      if (hint.res_class)
        XFree(hint.res_class);
      if (is_ours)
        continue;
    }

    if (XGetWindowProperty(dpy, children[i], ATOM(_NET_WM_WINDOW_TYPE),
                           0, 1, False, XA_ATOM,
                           &type, &format, &nitems, &bytes, &buf) == Success &&
        type == XA_ATOM && nitems > 0) {
      if (*(Atom *)buf == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP)) {
        desktop = children[i];
        XFree(buf);
        break;
      }
    }
    if (buf) {
      XFree(buf);
      buf = NULL;
    }
  }
  if (children)
    XFree(children);
  return desktop;
}

static Window resolve_root_for_vroot(Display *dpy, int screen) {
  /* find __SWM_VROOT (used by some WMs) and use it as the parent root */
  Window root = RootWindow(dpy, screen);
  Window troot, parent, *children = NULL;
  unsigned int n = 0;
  Atom type = None;
  int format;
  unsigned long nitems, bytes;
  unsigned char *buf = NULL;

  if (!XQueryTree(dpy, root, &troot, &parent, &children, &n) || n == 0) {
    if (children)
      XFree(children);
    return root;
  }
  for (unsigned int i = 0; i < n; i++) {
    if (XGetWindowProperty(dpy, children[i], ATOM(__SWM_VROOT),
                           0, 1, False, XA_WINDOW,
                           &type, &format, &nitems, &bytes, &buf) == Success &&
        type == XA_WINDOW && nitems > 0) {
      Window vroot = *(Window *)buf;
      XFree(buf);
      if (children)
        XFree(children);
      return vroot;
    }
    if (buf) {
      XFree(buf);
      buf = NULL;
    }
  }
  if (children)
    XFree(children);
  return root;
}

/* ---- window creation ---- */

static void set_cardinal_prop(Display *dpy, Window w, Atom prop, long value) {
  XChangeProperty(dpy, w, prop, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&value, 1);
}

static void append_atom_prop(Display *dpy, Window w, Atom prop, Atom value) {
  XChangeProperty(dpy, w, prop, XA_ATOM, 32, PropModeAppend,
                  (unsigned char *)&value, 1);
}

XWWindow *xw_x11_create_window(Display *dpy, const XWConfig *cfg,
                               int x, int y, unsigned int w, unsigned int h,
                               XWError *err) {
  int screen = DefaultScreen(dpy);
  Window root = RootWindow(dpy, screen);

  XWWindow *win = calloc(1, sizeof(*win));
  if (!win) {
    XW_ERROR_SET(err, 1, "out of memory");
    return NULL;
  }
  win->dpy = dpy;
  win->screen = screen;
  win->root = root;
  win->desktop_layer = find_desktop_layer(dpy, root);
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;

  root = resolve_root_for_vroot(dpy, screen);
  win->root = root;

  /* attach under the desktop layer so we sit just above desktop icons */
  Window parent = win->desktop_layer ? win->desktop_layer : root;

  /* use the default visual; we do not need ARGB for opaque wallpaper */
  Visual *visual = DefaultVisual(dpy, screen);
  Colormap cmap = DefaultColormap(dpy, screen);
  int depth = DefaultDepth(dpy, screen);
  win->visual = visual;
  win->colormap = cmap;
  win->has_argb = 0;

  XSetWindowAttributes attrs;
  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  attrs.background_pixel = BlackPixel(dpy, screen);
  attrs.event_mask = StructureNotifyMask | ExposureMask |
                     EnterWindowMask | LeaveWindowMask | PointerMotionMask;

  unsigned long mask = CWOverrideRedirect | CWBackPixel | CWEventMask;

  win->win = XCreateWindow(dpy, parent, x, y, w, h, 0, depth,
                           InputOutput, visual, mask, &attrs);
  if (!win->win) {
    XW_ERROR_SET(err, 1, "XCreateWindow failed");
    free(win);
    return NULL;
  }

  /* desktop window type */
  Atom wt = ATOM(_NET_WM_WINDOW_TYPE_DESKTOP);
  XChangeProperty(dpy, win->win, ATOM(_NET_WM_WINDOW_TYPE), XA_ATOM, 32,
                  PropModeReplace, (unsigned char *)&wt, 1);

  if (cfg->below) {
    append_atom_prop(dpy, win->win, ATOM(_NET_WM_STATE), ATOM(_NET_WM_STATE_BELOW));
    set_cardinal_prop(dpy, win->win, ATOM(_WIN_LAYER), 0);
  }
  if (cfg->sticky) {
    set_cardinal_prop(dpy, win->win, ATOM(_NET_WM_DESKTOP), 0xFFFFFFFFL);
    append_atom_prop(dpy, win->win, ATOM(_NET_WM_STATE), ATOM(_NET_WM_STATE_STICKY));
  }
  if (cfg->skip_taskbar)
    append_atom_prop(dpy, win->win, ATOM(_NET_WM_STATE), ATOM(_NET_WM_STATE_SKIP_TASKBAR));
  if (cfg->skip_pager)
    append_atom_prop(dpy, win->win, ATOM(_NET_WM_STATE), ATOM(_NET_WM_STATE_SKIP_PAGER));

  if (cfg->opacity < 1.0) {
    CARD32 o = (CARD32)(cfg->opacity * 0xffffffffU);
    XChangeProperty(dpy, win->win, ATOM(_NET_WM_WINDOW_OPACITY), XA_CARDINAL,
                    32, PropModeReplace, (unsigned char *)&o, 1);
  }

  if (cfg->no_focus) {
    XWMHints hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = InputHint;
    hints.input = False;
    XSetWMHints(dpy, win->win, &hints);
  }

  XClassHint ch = {"xwallpaper", "xwallpaper"};
  XSetClassHint(dpy, win->win, &ch);

  /* move below any existing siblings on the desktop layer */
  XLowerWindow(dpy, win->win);
  XMapWindow(dpy, win->win);
  XFlush(dpy);

  return win;
}

void xw_x11_destroy_window(XWWindow *win) {
  if (!win)
    return;
  if (win->win)
    XDestroyWindow(win->dpy, win->win);
  free(win);
}

/* ---- pointer forwarding ---- */

static void forward_motion(Display *dpy, Window root,
                           int x_root, int y_root, unsigned int state,
                           unsigned long time) {
  XEvent fake;
  memset(&fake, 0, sizeof(fake));
  fake.xmotion.type = MotionNotify;
  fake.xmotion.window = root;
  fake.xmotion.root = root;
  fake.xmotion.subwindow = None;
  fake.xmotion.time = time;
  fake.xmotion.x_root = x_root;
  fake.xmotion.y_root = y_root;
  fake.xmotion.state = state;
  fake.xmotion.is_hint = NotifyNormal;
  fake.xmotion.same_screen = True;
  XSendEvent(dpy, root, True, PointerMotionMask, &fake);
}

int xw_x11_handle_event(XWWindow *win, XEvent *ev) {
  switch (ev->type) {
  case EnterNotify:
  case LeaveNotify: {
    XCrossingEvent *cross = &ev->xcrossing;
    forward_motion(win->dpy, win->root, cross->x_root, cross->y_root,
                   cross->state, cross->time);
    return 1;
  }
  case MotionNotify: {
    XMotionEvent *motion = &ev->xmotion;
    forward_motion(win->dpy, win->root, motion->x_root, motion->y_root,
                   motion->state, motion->time);
    return 1;
  }
  default:
    return 0;
  }
}
