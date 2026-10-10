#ifndef XW_WL_H
#define XW_WL_H

#include <stdio.h>

#include "cli.h"
#include "xw_err.h"

/* One Wayland output (logical geometry). Filled by the wl module's
 * registry listener (Task 4); pure data, no wl_* handles, so geometry
 * resolution stays unit-testable without a compositor. */
#define XW_WL_OUTPUT_NAME_LEN 64

typedef struct XWWlOutput {
  char name[XW_WL_OUTPUT_NAME_LEN];
  int x, y;
  unsigned int width, height;
} XWWlOutput;

/* Resolve the geometry for a wallpaper window from config + output list.
 * Mirrors xw_x11_resolve_geometry semantics:
 *   - TARGET_GEOM (have_geom): use cfg geometry verbatim, out_name = "".
 *   - TARGET_OUTPUT: look up output name; error (-1) if unknown.
 *   - TARGET_PRIMARY / TARGET_ALL: largest-area output (first wins ties);
 *     error (-1) if none.
 * Returns 0 on success, -1 on error. */
int xw_wl_resolve_geometry(const XWWlOutput *outs, int nouts,
                           const XWConfig *cfg,
                           int *x, int *y, unsigned int *w, unsigned int *h,
                           char *out_name);

/* Enumerate outputs: prints "name WxH+X+Y" lines to out (same format as
 * xw_x11_list_outputs). Used by --list and error messages. */
void xw_wl_list_outputs(const XWWlOutput *outs, int nouts, FILE *out);

/* One layer-shell wallpaper window. Handles are opaque here so this header
 * (and the unit test) never needs wayland headers; wl.c casts them. */
typedef struct XWWlWindow {
  void *surface;        /* struct wl_surface */
  void *layer;          /* struct zwlr_layer_surface_v1 */
  void *wl_ctx;         /* xw_wl_ctx*, set by create (backends use accessors) */
  char output[XW_WL_OUTPUT_NAME_LEN]; /* output name, "" if geometry-only */
  int is_geometry;      /* placed by explicit -g, not bound to an output */

  unsigned int width, height;
  int x, y;             /* logical position (for monitor matching) */

  /* current backend info (for --list), filled by app */
  int type;             /* XW_BACKEND_* */
  char path[1024];
  int rotation;

  /* backend flags snapshot (from cfg at set), persisted to state so
   * restore rebuilds the wallpaper exactly as it was set (X11/wayland
   * windows share this shape; see XWWindow in x11.h) */
  int mute;
  double volume;
  double fps;
  int scale_mode;
  double opacity;
  int below, sticky, skip_taskbar, skip_pager, no_focus;
  char keybinds[1024];
  char audio_client_name[64];

  /* layer-shell configure state: the configure listener stays registered
   * for the window lifetime (compositor may re-configure on resize), so
   * it must live in the window, never on window_new's stack. */
  int configured;
  int closed;
} XWWlWindow;

/* Wayland connection + enumerated outputs. Opaque: build it with
 * xw_wl_connect, which round-trips the registry. NULL without
 * XW_HAVE_WAYLAND (X11-only build). */
typedef struct xw_wl_ctx xw_wl_ctx;

/* Connect to the compositor and enumerate outputs. Returns NULL with err
 * set when WAYLAND_DISPLAY is missing, the compositor lacks
 * wlr-layer-shell, or no outputs exist. */
xw_wl_ctx *xw_wl_connect(XWError *err);

/* Number of enumerated outputs. */
int xw_wl_output_count(xw_wl_ctx *ctx);

/* Enumerated outputs (owned by ctx, valid until disconnect). */
const XWWlOutput *xw_wl_outputs(xw_wl_ctx *ctx);

/* Create a background layer-shell surface for cfg's target (output bound,
 * or unbound for -g). Waits for the first configure. Returns NULL with
 * err set on failure. */
XWWlWindow *xw_wl_create_window(xw_wl_ctx *ctx, const XWConfig *cfg,
                               int x, int y, unsigned int w, unsigned int h,
                               XWError *err);

/* Positioned surface for span pieces: TOP|LEFT-anchored at (rx, ry)
 * relative to the bound output's origin. Returns NULL with err set. */
XWWlWindow *xw_wl_create_window_at(xw_wl_ctx *ctx, const char *output,
                                   int rx, int ry,
                                   unsigned int w, unsigned int h,
                                   XWError *err);

/* Destroy a window (not the connection). */
void xw_wl_destroy_window(XWWlWindow *win);

/* Tear down the connection. */
void xw_wl_disconnect(xw_wl_ctx *ctx);

/* The connection's wl_shm (for backend shm pools). NULL without a display.
 * Opaque struct tag keeps wayland headers out of this header. */
struct wl_shm;
struct wl_shm *xw_wl_get_shm(xw_wl_ctx *ctx);

/* The raw wl_display (for EGL setup in backends). NULL if not connected. */
struct wl_display;
struct wl_display *xw_wl_get_display(xw_wl_ctx *ctx);

/* Poll fd for the daemon event loop (-1 if not connected). */
int xw_wl_get_fd(xw_wl_ctx *ctx);

/* Dispatch pending events + flush requests (for the daemon poll loop). */
void xw_wl_dispatch(xw_wl_ctx *ctx);

#endif /* XW_WL_H */
