#ifndef XW_X11_H
#define XW_X11_H

#include <X11/Xlib.h>

#include "backend.h"
#include "cli.h"
#include "xw_err.h"

/* One desktop wallpaper window. Owned by the x11 module, shared with backends.
 * The x11 module holds a single X connection for all windows. */
typedef struct XWWindow {
  Display *dpy;           /* shared X connection (owned by x11 module) */
  Window root;            /* root or vroot/desktop-layer parent */
  Window desktop_layer;   /* 0 if none found */
  Window win;             /* the wallpaper window */
  int screen;

  int x, y;               /* window geometry */
  unsigned int width, height;
  int has_argb;
  Visual *visual;
  Colormap colormap;

  char output[64];        /* xrandr output name, "" if geometry-only */
  int is_geometry;        /* window placed by explicit -g, not an output */

  /* current backend info (for --list), filled by app */
  int type;               /* XW_BACKEND_* */
  char path[1024];
  int rotation;           /* video rotation 0/90/180/270 (for --list & state) */

  /* backend flags snapshot (from cfg at set), persisted to state so
   * restore rebuilds the wallpaper exactly as it was set */
  int mute;
  double volume;          /* 0..100 */
  double fps;             /* 0 = auto */
  int scale_mode;         /* XW_SCALE_* */
  double opacity;         /* 0..1 */
  int below, sticky, skip_taskbar, skip_pager, no_focus;
  char keybinds[1024];        /* mpv input.conf path, "" = none */
  char audio_client_name[64]; /* audio API client name */

  /* callback: pointer events forwarded to root (installed by x11) */
  void *user_data;
} XWWindow;

/* Resolve the geometry for a wallpaper window from config + current XRandR state.
 *   - TARGET_GEOM: use cfg geometry verbatim.
 *   - TARGET_OUTPUT: look up output name; on success fill rect; error if unknown.
 *   - TARGET_ALL: (handled by caller) this fills the primary output.
 *   - TARGET_PRIMARY: primary output.
 * Returns 0 on success, -1 on error (unknown output, no outputs).
 * On success writes rect into *x,*y,*w,*h and copies the output name into
 * out_name (caller provides XW_OUTPUT_NAME_LEN bytes). */
#define XW_OUTPUT_NAME_LEN 64
int xw_x11_resolve_geometry(Display *dpy, const XWConfig *cfg,
                            int *x, int *y, unsigned int *w, unsigned int *h,
                            char *out_name);

/* Create a desktop wallpaper window at the given geometry.
 * Applies desktop-layer placement, window-type/state props, opacity, flags.
 * Returns the XWWindow or NULL with err set. */
XWWindow *xw_x11_create_window(Display *dpy, const XWConfig *cfg,
                               int x, int y, unsigned int w, unsigned int h,
                               XWError *err);

/* Destroy a window (not the shared display). */
void xw_x11_destroy_window(XWWindow *win);

/* Open the shared X display. Call once per process. */
Display *xw_x11_open_display(XWError *err);

/* Close the shared X display (call at process exit). */
void xw_x11_close_display(Display *dpy);

/* Enumerate xrandr outputs: prints "name WxH+X+Y" lines to out.
 * Used by --list and error messages. */
void xw_x11_list_outputs(Display *dpy, FILE *out);

/* Handle an X event for this window. Returns 1 if the event was consumed. */
int xw_x11_handle_event(XWWindow *win, XEvent *ev);

#endif /* XW_X11_H */
