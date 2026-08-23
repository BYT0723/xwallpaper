#ifndef XW_BACKEND_H
#define XW_BACKEND_H

#include <X11/Xlib.h>

#include "xw_err.h"

/* Forward decl: x11 module owns the concrete struct (XWWindow defined in x11.h) */
typedef struct XWWindow XWWindow;

/* Backend kinds, matching --image/--video/--web */
enum {
  XW_BACKEND_IMAGE = 0,
  XW_BACKEND_VIDEO,
  XW_BACKEND_WEB,
};

/* Image scale modes */
enum {
  XW_SCALE_STRETCH = 0, /* fit exactly, ignore aspect */
  XW_SCALE_CONTAIN,     /* fit inside, keep aspect, letterbox */
  XW_SCALE_FILL,        /* cover, keep aspect, crop overflow */
  XW_SCALE_TILE,        /* repeat at native size */
};

typedef struct XWBackendOpt {
  int type;             /* one of XW_BACKEND_* */
  const char *path;     /* file path or URL (owned by caller) */
  /* image */
  int scale_mode;       /* one of XW_SCALE_* */
  /* video */
  int mute;
  double volume;        /* 0.0 .. 100.0 */
  int rotate;           /* 0/90/180/270 */
  const char *keybinds; /* mpv input.conf path, may be NULL */
  double fps;           /* forced frame rate, 0 = auto */
} XWBackendOpt;

/* Backend lifecycle. app codes only against this interface.
 * init: create backend resources for one window (may open libs, NOT a new X display).
 * start: begin rendering/playback.
 * stop: pause/tear down runtime work but keep resources.
 * destroy: free everything. Must be safe to call after a failed init. */
typedef struct XWBackend XWBackend;
struct XWBackend {
  const char *name;
  void *priv;
  int uses_gtk; /* backend requires the GTK main context to be pumped */
  int (*init)(XWBackend *b, XWWindow *win, const XWBackendOpt *opt, XWError *err);
  int (*start)(XWBackend *b);
  void (*stop)(XWBackend *b);
  void (*destroy)(XWBackend *b);
  /* optional: receive X events (Expose, ConfigureNotify) for this window.
   * Return 1 if consumed. Called by app's event loop. May be NULL. */
  int (*on_event)(XWBackend *b, XEvent *ev);
};

#endif /* XW_BACKEND_H */
