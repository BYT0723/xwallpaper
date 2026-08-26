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
  double fps;           /* target frame rate, 0 = auto */
  const char *audio_client_name; /* audio API client name, may be NULL */
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
  /* optional: reuse this backend for a different file of the SAME type
   * (video -> video etc) without tearing the window down. Switching wallpapers
   * then only swaps the resource (mpv loadfile / image redraw / webkit load)
   * instead of re-creating the backend, which can block the single-threaded
   * daemon on GL/mpv context setup. May be NULL (falls back to destroy+init). */
  int (*reload)(XWBackend *b, const XWBackendOpt *opt, XWError *err);
  /* optional: pause/resume rendering when the X display is in DPMS
   * power-off (standby/suspend/off). Rendering backends (e.g. mpv) keep
   * presenting frames while DPMS is off, which wakes the display back up.
   * May be NULL (static images need no pause). */
  void (*set_paused)(XWBackend *b, int paused);
  /* optional: 启动早期 GPU 未就绪时, 渲染资源可能建不起来 (mpv 的 Vulkan
   * swapchain 创建失败会丢掉 video track, 窗口在但画面黑)。此方法检测渲染
   * 是否健康, 需要重建时返回 1 并触发自愈 (mpv 重新 loadfile)。0 = 健康,
   * -1 = 不适用。用于 daemon 的启动自愈轮询。 */
  int (*retry)(XWBackend *b);
};

#endif /* XW_BACKEND_H */
