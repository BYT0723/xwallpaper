#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>
#include <glib.h>

#include "app.h"
#include "backends.h"
#include "state.h"
#include "web.h"
#include "wl.h"
#include "x11.h"

struct XWApp {
  Display *dpy; /* X11 only (NULL on Wayland) */
  XWWindow **wins;
  XWBackend **backends;
  int nwins;
  int stop_requested;
  int uses_gtk;
  int use_wayland;
#ifdef XW_HAVE_WAYLAND
  xw_wl_ctx *wl;
  XWWlWindow **wlwins;
#endif
  /* DPMS state (lazily probed; -1 power level = never polled yet) */
  int dpms_available;
  int dpms_queried;
  int dpms_power_level;
  uint64_t dpms_last_poll_ms;
};

static XWApp *the_app = NULL;

static void apply_dpms_to_backend(XWApp *app, XWBackend *b);

static XWBackend *make_backend(const XWConfig *cfg, XWError *err) {
  int k = cfg->backend_explicit;
  if (k < 0) {
    k = xw_cli_detect_backend(cfg->path);
    if (k < 0) {
      XW_ERROR_SET(err, 1, "cannot determine backend for '%.100s' "
                   "(use --image/--video/--web/--scene)", cfg->path);
      return NULL;
    }
  }
  switch (k) {
  case XW_BACKEND_IMAGE: return xw_image_backend_new();
  case XW_BACKEND_VIDEO: return xw_video_backend_new();
  case XW_BACKEND_WEB:   return xw_web_backend_new();
  case XW_BACKEND_SCENE: return xw_scene_backend_new();
  default:
    XW_ERROR_SET(err, 1, "unknown backend kind %d", k);
    return NULL;
  }
}

static void fill_backend_opt(XWBackendOpt *opt, const XWConfig *cfg) {
  memset(opt, 0, sizeof(*opt));
  opt->type = cfg->backend_explicit < 0
                ? xw_cli_detect_backend(cfg->path)
                : cfg->backend_explicit;
  opt->path = cfg->path;
  opt->scale_mode = cfg->scale_mode;
  opt->mute = cfg->mute;
  opt->volume = cfg->volume;
  opt->rotate = cfg->rotate;
  opt->keybinds = cfg->keybinds[0] ? cfg->keybinds : NULL;
  opt->fps = cfg->fps;
  opt->audio_client_name = cfg->audio_client_name[0] ? cfg->audio_client_name : NULL;
  opt->span = 0;
  opt->span_w = opt->span_h = 0;
  opt->span_ox = opt->span_oy = 0;
  if (cfg->target == XW_TARGET_SPAN && cfg->have_span) {
    opt->span = 1;
    opt->span_w = cfg->span_w;
    opt->span_h = cfg->span_h;
  }
}

static void capture_x11_flags(XWWindow *win, const XWConfig *cfg) {
  win->mute = cfg->mute;
  win->volume = cfg->volume;
  win->fps = cfg->fps;
  win->scale_mode = cfg->scale_mode;
  win->opacity = cfg->opacity;
  win->below = cfg->below;
  win->sticky = cfg->sticky;
  win->skip_taskbar = cfg->skip_taskbar;
  win->skip_pager = cfg->skip_pager;
  win->no_focus = cfg->no_focus;
  snprintf(win->keybinds, sizeof(win->keybinds), "%s", cfg->keybinds);
  snprintf(win->audio_client_name, sizeof(win->audio_client_name), "%s",
           cfg->audio_client_name);
}

#ifdef XW_HAVE_WAYLAND
static void capture_wl_flags(XWWlWindow *win, const XWConfig *cfg) {
  win->mute = cfg->mute;
  win->volume = cfg->volume;
  win->fps = cfg->fps;
  win->scale_mode = cfg->scale_mode;
  win->opacity = cfg->opacity;
  win->below = cfg->below;
  win->sticky = cfg->sticky;
  win->skip_taskbar = cfg->skip_taskbar;
  win->skip_pager = cfg->skip_pager;
  win->no_focus = cfg->no_focus;
  snprintf(win->keybinds, sizeof(win->keybinds), "%s", cfg->keybinds);
  snprintf(win->audio_client_name, sizeof(win->audio_client_name), "%s",
           cfg->audio_client_name);
}
#endif

/* Backend lifecycle dispatch: Wayland instances use the _wl slots.
 * stop/set_paused/retry need no dispatch (NULL-safe shared impls:
 * video touches only VideoBase, web/image ops are toolkit-level). */
static int backend_init(XWApp *app, XWBackend *b, void *win,
                        const XWBackendOpt *opt, XWError *err) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland) {
    if (!b->init_wl) {
      XW_ERROR_SET(err, 1, "%s backend not supported on Wayland", b->name);
      return -1;
    }
    return b->init_wl(b, (XWWlWindow *)win, opt, err);
  }
#else
  (void)app;
#endif
  return b->init(b, (XWWindow *)win, opt, err);
}

static int backend_start(XWApp *app, XWBackend *b) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland) {
    if (!b->start_wl)
      return -1;
    return b->start_wl(b);
  }
#else
  (void)app;
#endif
  return b->start(b);
}

static void backend_destroy(XWApp *app, XWBackend *b) {
  if (!b)
    return;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland) {
    if (b->destroy_wl)
      b->destroy_wl(b);
    return;
  }
#else
  (void)app;
#endif
  b->destroy(b);
}

static int backend_reload(XWApp *app, XWBackend *b, const XWBackendOpt *opt,
                          XWError *err) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return b->reload_wl ? b->reload_wl(b, opt, err) : -1;
#else
  (void)app;
#endif
  return b->reload ? b->reload(b, opt, err) : -1;
}

static int backend_has_reload(XWApp *app, XWBackend *b) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return b->reload_wl != NULL;
#else
  (void)app;
#endif
  return b->reload != NULL;
}

/* Identity key of window i (output name or -g name). */
static const char *window_key_at(XWApp *app, int i) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return app->wlwins[i] ? app->wlwins[i]->output : "";
#endif
  return app->wins[i] ? app->wins[i]->output : "";
}
/* Find a window by its identity key. Windows are identified by output:
 * real outputs use the output name, -g windows use their --name.
 * Returns index or -1. */
static int find_window_by_key(XWApp *app, const char *key) {
  if (!key || !key[0])
    return -1;
  for (int i = 0; i < app->nwins; i++) {
    const char *output = window_key_at(app, i);
    if (output && output[0] && strcmp(output, key) == 0)
      return i;
  }
  return -1;
}

/* Resolve the identity key a config addresses: real outputs use the xrandr
 * name, -g windows use their --name. Returns "" if no key. */
static void window_key(const XWConfig *cfg, const char *resolved_output,
                       char *out, size_t n) {
  if (cfg->have_geom)
    snprintf(out, n, "%s", cfg->name);
  else if (cfg->target == XW_TARGET_OUTPUT || cfg->target == XW_TARGET_PRIMARY)
    snprintf(out, n, "%s", resolved_output[0] ? resolved_output : cfg->output);
  else
    out[0] = '\0';
}

/* remove window index i (destroy backend + window, compact arrays) */
static void remove_window(XWApp *app, int i) {
  backend_destroy(app, app->backends[i]);
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland) {
    if (app->wlwins[i])
      xw_wl_destroy_window(app->wlwins[i]);
    app->wlwins[i] = app->wlwins[app->nwins - 1];
    app->backends[i] = app->backends[app->nwins - 1];
    app->nwins--;
    return;
  }
#endif
  if (app->wins[i])
    xw_x11_destroy_window(app->wins[i]);
  app->wins[i] = app->wins[app->nwins - 1];
  app->backends[i] = app->backends[app->nwins - 1];
  app->nwins--;
}

#ifdef XW_HAVE_WAYLAND
/* resolve geometry on the active platform */
static int resolve_geometry(XWApp *app, const XWConfig *cfg,
                            int *x, int *y, unsigned int *w, unsigned int *h,
                            char *out_name) {
  if (app->use_wayland)
    return xw_wl_resolve_geometry(xw_wl_outputs(app->wl),
                                  xw_wl_output_count(app->wl), cfg,
                                  x, y, w, h, out_name);
  return xw_x11_resolve_geometry(app->dpy, cfg, x, y, w, h, out_name);
}

/* attach a backend to a fresh layer-shell window: make/init the backend,
 * fill window metadata, append to the window list. Destroys the window
 * on failure. Starting the backend is the caller's job. */
static int attach_backend_wl(XWApp *app, const XWConfig *cfg, XWWlWindow *win,
                             const XWBackendOpt *opt, XWError *err) {
  XWBackend *tmpl = make_backend(cfg, err);
  if (!tmpl) {
    xw_wl_destroy_window(win);
    return -1;
  }
  XWBackend *b = malloc(sizeof(*b));
  if (!b) {
    xw_wl_destroy_window(win);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  *b = *tmpl;
  b->priv = NULL;

  if (backend_init(app, b, win, opt, err) != 0) {
    free(b);
    xw_wl_destroy_window(win);
    return -1;
  }
  if (b->uses_gtk)
    app->uses_gtk = 1;
  win->type = opt->type;
  snprintf(win->path, sizeof(win->path), "%s", opt->path ? opt->path : "");
  win->rotation = cfg->rotate;
  capture_wl_flags(win, cfg);

  XWWlWindow **wlwins =
      realloc(app->wlwins, sizeof(XWWlWindow *) * (app->nwins + 1));
  XWBackend **backends =
      realloc(app->backends, sizeof(XWBackend *) * (app->nwins + 1));
  if (!wlwins || !backends) {
    backend_destroy(app, b);
    xw_wl_destroy_window(win);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  app->wlwins = wlwins;
  app->backends = backends;
  app->wlwins[app->nwins] = win;
  app->backends[app->nwins] = b;
  app->nwins++;
  return 0;
}

/* create a fresh layer-shell window + backend for the given rect/output */
static int create_window_wl(XWApp *app, const XWConfig *cfg,
                            int x, int y, unsigned int w, unsigned int h,
                            const char *output, int is_geom, XWError *err) {
  XWWlWindow *win = xw_wl_create_window(app->wl, cfg, x, y, w, h, err);
  if (!win)
    return -1;
  if (output)
    snprintf(win->output, sizeof(win->output), "%s", output);
  win->is_geometry = is_geom;

  XWBackendOpt opt;
  fill_backend_opt(&opt, cfg);
  return attach_backend_wl(app, cfg, win, &opt, err);
}
#else
static int resolve_geometry(XWApp *app, const XWConfig *cfg,
                            int *x, int *y, unsigned int *w, unsigned int *h,
                            char *out_name) {
  return xw_x11_resolve_geometry(app->dpy, cfg, x, y, w, h, out_name);
}
#endif

/* create a fresh window + backend for the given rect/output */
static int create_window(XWApp *app, const XWConfig *cfg,
                         int x, int y, unsigned int w, unsigned int h,
                         const char *output, int is_geom, XWError *err) {
  XWWindow *win = xw_x11_create_window(app->dpy, cfg, x, y, w, h, err);
  if (!win)
    return -1;
  if (output)
    snprintf(win->output, sizeof(win->output), "%s", output);
  win->is_geometry = is_geom;

  XWBackend *tmpl = make_backend(cfg, err);
  if (!tmpl) {
    xw_x11_destroy_window(win);
    return -1;
  }
  XWBackend *b = malloc(sizeof(*b));
  if (!b) {
    xw_x11_destroy_window(win);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  *b = *tmpl;
  b->priv = NULL;

  XWBackendOpt opt;
  fill_backend_opt(&opt, cfg);
  if (backend_init(app, b, win, &opt, err) != 0) {
    free(b);
    xw_x11_destroy_window(win);
    return -1;
  }
  if (b->uses_gtk)
    app->uses_gtk = 1;
  win->type = opt.type;
  snprintf(win->path, sizeof(win->path), "%s", opt.path ? opt.path : "");
  win->rotation = cfg->rotate;
  capture_x11_flags(win, cfg);

  app->wins = realloc(app->wins, sizeof(XWWindow *) * (app->nwins + 1));
  app->backends = realloc(app->backends, sizeof(XWBackend *) * (app->nwins + 1));
  if (!app->wins || !app->backends) {
    backend_destroy(app, b);
    xw_x11_destroy_window(win);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  app->wins[app->nwins] = win;
  app->backends[app->nwins] = b;
  app->nwins++;
  return 0;
}

XWApp *xw_app_create(XWError *err) {
#ifdef XW_HAVE_WAYLAND
  /* WAYLAND_DISPLAY set means a Wayland session: commit to the layer-shell
   * path. Failures are fatal readable errors — never silently fall back
   * to X11 (an XWayland window would be invisible behind the compositor). */
  if (getenv("WAYLAND_DISPLAY")) {
    XWApp *app = calloc(1, sizeof(*app));
    if (!app) {
      XW_ERROR_SET(err, 1, "out of memory");
      return NULL;
    }
    app->wl = xw_wl_connect(err);
    if (!app->wl) {
      free(app);
      return NULL;
    }
    app->use_wayland = 1;
    app->dpms_power_level = -1;
    the_app = app;
    return app;
  }
#endif
  Display *dpy = xw_x11_open_display(err);
  if (!dpy)
    return NULL;
  XWApp *app = calloc(1, sizeof(*app));
  if (!app) {
    XW_ERROR_SET(err, 1, "out of memory");
    xw_x11_close_display(dpy);
    return NULL;
  }
  app->dpy = dpy;
  app->dpms_power_level = -1;
  the_app = app;
  return app;
}

#ifdef XW_HAVE_WAYLAND
/* one span piece: positioned surface + image backend rendering the crop.
 * Pieces never reuse surfaces (a fullscreen surface can't become a crop). */
static int create_span_piece(XWApp *app, const XWConfig *cfg,
                             const char *output, int rx, int ry,
                             unsigned int w, unsigned int h,
                             int span_ox, int span_oy, XWError *err) {
  int idx = find_window_by_key(app, output);
  if (idx >= 0)
    remove_window(app, idx);

  XWWlWindow *win = xw_wl_create_window_at(app->wl, output, rx, ry, w, h, err);
  if (!win)
    return -1;

  XWBackendOpt opt;
  fill_backend_opt(&opt, cfg);
  opt.span_ox = span_ox;
  opt.span_oy = span_oy;
  if (attach_backend_wl(app, cfg, win, &opt, err) != 0)
    return -1;
  XWBackend *b = app->backends[app->nwins - 1];
  if (backend_start(app, b) != 0) {
    remove_window(app, app->nwins - 1);
    XW_ERROR_SET(err, 1, "backend failed to start");
    return -1;
  }
  apply_dpms_to_backend(app, b);
  return 0;
}

/* --span: intersect the layout rect with each output, one positioned
 * image piece per intersection. Identity is the output name, so
 * clear/-m/--list/state keep working unchanged. */
static int app_set_span(XWApp *app, const XWConfig *cfg, XWError *err) {
  XWBackendOpt probe;
  fill_backend_opt(&probe, cfg);
  if (probe.type != XW_BACKEND_IMAGE) {
    XW_ERROR_SET(err, 1, "--span only supports image wallpapers");
    return -1;
  }
  if (!cfg->have_span || cfg->span_w == 0 || cfg->span_h == 0) {
    XW_ERROR_SET(err, 1, "--span requires a geometry (want WxH[+X+Y])");
    return -1;
  }
  int sx0 = cfg->span_x, sy0 = cfg->span_y;
  int sx1 = sx0 + (int)cfg->span_w, sy1 = sy0 + (int)cfg->span_h;
  int n = xw_wl_output_count(app->wl);
  const XWWlOutput *outs = xw_wl_outputs(app->wl);
  int made = 0;
  for (int i = 0; i < n; i++) {
    int ix0 = sx0 > outs[i].x ? sx0 : outs[i].x;
    int iy0 = sy0 > outs[i].y ? sy0 : outs[i].y;
    int ix1 = sx1 < outs[i].x + (int)outs[i].width
                  ? sx1
                  : outs[i].x + (int)outs[i].width;
    int iy1 = sy1 < outs[i].y + (int)outs[i].height
                  ? sy1
                  : outs[i].y + (int)outs[i].height;
    if (ix1 <= ix0 || iy1 <= iy0)
      continue;
    if (create_span_piece(app, cfg, outs[i].name, ix0 - outs[i].x,
                          iy0 - outs[i].y, (unsigned int)(ix1 - ix0),
                          (unsigned int)(iy1 - iy0), ix0 - sx0, iy0 - sy0,
                          err) != 0)
      return -1;
    made++;
  }
  if (made == 0) {
    XW_ERROR_SET(err, 1, "--span rect intersects no output "
                 "(use --list to see available outputs)");
    return -1;
  }
  return 0;
}

/* Wayland set: one layer-shell surface per output (ALL) or a single
 * target. Mirrors the X11 flow: same-type reload in place, else rebuild. */
static int app_set_wl(XWApp *app, const XWConfig *cfg, XWError *err) {
  if (cfg->target == XW_TARGET_SPAN)
    return app_set_span(app, cfg, err);
  if (cfg->target == XW_TARGET_ALL) {
    xw_app_clear_all(app);
    int n = xw_wl_output_count(app->wl);
    for (int i = 0; i < n; i++) {
      const XWWlOutput *o = &xw_wl_outputs(app->wl)[i];
      if (create_window_wl(app, cfg, o->x, o->y, o->width, o->height,
                           o->name, 0, err) != 0)
        return -1;
    }
    if (app->nwins == 0) {
      XW_ERROR_SET(err, 1, "no connected outputs found");
      return -1;
    }
    for (int i = 0; i < app->nwins; i++) {
      if (backend_start(app, app->backends[i]) != 0) {
        XW_ERROR_SET(err, 1, "backend failed to start");
        return -1;
      }
      apply_dpms_to_backend(app, app->backends[i]);
    }
    return 0;
  }

  int x, y;
  unsigned int w, h;
  char name[XW_WL_OUTPUT_NAME_LEN] = {0};
  if (resolve_geometry(app, cfg, &x, &y, &w, &h, name) != 0) {
    XW_ERROR_SET(err, 1, "cannot resolve output '%s' "
                 "(use --list to see available outputs)", cfg->output);
    return -1;
  }

  char lookup[XW_WL_OUTPUT_NAME_LEN] = {0};
  window_key(cfg, name, lookup, sizeof(lookup));
  int idx = find_window_by_key(app, lookup);

  if (idx >= 0) {
    XWBackendOpt opt;
    fill_backend_opt(&opt, cfg);
    XWBackend *old = app->backends[idx];
    if (old && backend_has_reload(app, old) &&
        app->wlwins[idx]->type == opt.type) {
      if (backend_reload(app, old, &opt, err) != 0)
        return -1;
      app->wlwins[idx]->type = opt.type;
      snprintf(app->wlwins[idx]->path, sizeof(app->wlwins[idx]->path), "%s",
               opt.path ? opt.path : "");
      app->wlwins[idx]->rotation = cfg->rotate;
      capture_wl_flags(app->wlwins[idx], cfg);
      apply_dpms_to_backend(app, old);
      return 0;
    }
    backend_destroy(app, old);

    XWBackend *tmpl = make_backend(cfg, err);
    if (!tmpl)
      return -1;
    XWBackend *b = malloc(sizeof(*b));
    if (!b) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    *b = *tmpl;
    b->priv = NULL;
    fill_backend_opt(&opt, cfg);
    if (backend_init(app, b, app->wlwins[idx], &opt, err) != 0) {
      free(b);
      return -1;
    }
    if (b->uses_gtk)
      app->uses_gtk = 1;
    app->backends[idx] = b;
    app->wlwins[idx]->type = opt.type;
    snprintf(app->wlwins[idx]->path, sizeof(app->wlwins[idx]->path), "%s",
             opt.path ? opt.path : "");
    app->wlwins[idx]->rotation = cfg->rotate;
    capture_wl_flags(app->wlwins[idx], cfg);
    if (backend_start(app, b) != 0) {
      backend_destroy(app, b);
      app->backends[idx] = NULL;
      XW_ERROR_SET(err, 1, "backend failed to start");
      return -1;
    }
    apply_dpms_to_backend(app, b);
    return 0;
  }

  const char *out;
  if (cfg->have_geom)
    out = cfg->name[0] ? cfg->name : NULL;
  else
    out = name[0] ? name : NULL;
  if (create_window_wl(app, cfg, x, y, w, h, out, cfg->have_geom, err) != 0)
    return -1;
  if (backend_start(app, app->backends[app->nwins - 1]) != 0) {
    remove_window(app, app->nwins - 1);
    XW_ERROR_SET(err, 1, "backend failed to start");
    return -1;
  }
  apply_dpms_to_backend(app, app->backends[app->nwins - 1]);
  return 0;
}
#endif

int xw_app_set(XWApp *app, const XWConfig *cfg, XWError *err) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return app_set_wl(app, cfg, err);
#endif
  if (cfg->target == XW_TARGET_SPAN) {
    XW_ERROR_SET(err, 1, "--span is only supported on Wayland");
    return -1;
  }
  if (cfg->target == XW_TARGET_ALL) {
    /* replace all windows: one per connected output */
    xw_app_clear_all(app);
    Window root = RootWindow(app->dpy, DefaultScreen(app->dpy));
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(app->dpy, root);
    if (!res) {
      XW_ERROR_SET(err, 1, "xrandr: no outputs");
      return -1;
    }
    for (int i = 0; i < res->noutput; i++) {
      XRROutputInfo *oi = XRRGetOutputInfo(app->dpy, res, res->outputs[i]);
      if (!oi)
        continue;
      if (oi->crtc && oi->connection == RR_Connected) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo(app->dpy, res, oi->crtc);
        if (ci) {
          if (create_window(app, cfg, ci->x, ci->y, ci->width, ci->height,
                            oi->name, 0, err) != 0) {
            XRRFreeCrtcInfo(ci);
            XRRFreeOutputInfo(oi);
            XRRFreeScreenResources(res);
            return -1;
          }
          XRRFreeCrtcInfo(ci);
        }
      }
      XRRFreeOutputInfo(oi);
    }
    XRRFreeScreenResources(res);
    if (app->nwins == 0) {
      XW_ERROR_SET(err, 1, "no connected outputs found");
      return -1;
    }
    for (int i = 0; i < app->nwins; i++) {
      if (backend_start(app, app->backends[i]) != 0) {
        XW_ERROR_SET(err, 1, "backend failed to start");
        return -1;
      }
      apply_dpms_to_backend(app, app->backends[i]);
    }
    return 0;
  }

  /* single window: output or geom (primary resolves to its output name) */
  int x, y;
  unsigned int w, h;
  char name[XW_OUTPUT_NAME_LEN] = {0};
  if (xw_x11_resolve_geometry(app->dpy, cfg, &x, &y, &w, &h, name) != 0) {
    XW_ERROR_SET(err, 1, "cannot resolve output '%s' "
                 "(use --list to see available outputs)", cfg->output);
    return -1;
  }

  char lookup[XW_OUTPUT_NAME_LEN] = {0};
  window_key(cfg, name, lookup, sizeof(lookup));
  int idx = find_window_by_key(app, lookup);

  if (idx >= 0) {
    /* 同类型切换: 复用 backend 只换资源 (mpv loadfile / 图片重绘 / webkit 加载),
     * 避免 destroy+init 重建 GL/mpv 上下文 — 后者在单线程 daemon 里可能阻塞
     * 整个事件循环 (表现为切换壁纸卡死)。 */
    XWBackendOpt opt;
    fill_backend_opt(&opt, cfg);
    XWBackend *old = app->backends[idx];
    if (old && backend_has_reload(app, old) &&
        app->wins[idx]->type == opt.type) {
      if (backend_reload(app, old, &opt, err) != 0)
        return -1; /* 失败保留旧壁纸, 窗口元数据也保持旧值 */
      app->wins[idx]->type = opt.type;
      snprintf(app->wins[idx]->path, sizeof(app->wins[idx]->path), "%s",
               opt.path ? opt.path : "");
      app->wins[idx]->rotation = cfg->rotate;
      capture_x11_flags(app->wins[idx], cfg);
      apply_dpms_to_backend(app, old);
      return 0;
    }
    /* 不同类型才销毁重建 */
    backend_destroy(app, old);

    XWBackend *tmpl = make_backend(cfg, err);
    if (!tmpl)
      return -1;
    XWBackend *b = malloc(sizeof(*b));
    if (!b) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    *b = *tmpl;
    b->priv = NULL;
    fill_backend_opt(&opt, cfg);
    if (backend_init(app, b, app->wins[idx], &opt, err) != 0) {
      free(b);
      return -1;
    }
    if (b->uses_gtk)
      app->uses_gtk = 1;
    app->backends[idx] = b;
    app->wins[idx]->type = opt.type;
    snprintf(app->wins[idx]->path, sizeof(app->wins[idx]->path), "%s",
             opt.path ? opt.path : "");
    app->wins[idx]->rotation = cfg->rotate;
    capture_x11_flags(app->wins[idx], cfg);
    if (backend_start(app, b) != 0) {
      backend_destroy(app, b);
      app->backends[idx] = NULL;
      XW_ERROR_SET(err, 1, "backend failed to start");
      return -1;
    }
    apply_dpms_to_backend(app, b);
    return 0;
  }

  /* no existing window: create one. -g windows carry their --name as the
   * identity (win->output); real outputs carry the xrandr name. */
  const char *out;
  if (cfg->have_geom)
    out = cfg->name[0] ? cfg->name : NULL;
  else
    out = name[0] ? name : NULL;
  if (create_window(app, cfg, x, y, w, h, out, cfg->have_geom, err) != 0)
    return -1;
  if (backend_start(app, app->backends[app->nwins - 1]) != 0) {
    remove_window(app, app->nwins - 1);
    XW_ERROR_SET(err, 1, "backend failed to start");
    return -1;
  }
  apply_dpms_to_backend(app, app->backends[app->nwins - 1]);
  return 0;
}

int xw_app_clear(XWApp *app, const XWConfig *cfg, XWError *err) {
  (void)err;
  if (cfg->target == XW_TARGET_ALL) {
    xw_app_clear_all(app);
    return 0;
  }
  /* resolve the output name so PRIMARY matches the actual primary output */
  char key[XW_OUTPUT_NAME_LEN] = {0};
  if (cfg->have_geom) {
    snprintf(key, sizeof(key), "%s", cfg->name);
  } else {
    int x, y;
    unsigned int w, h;
    char name[XW_OUTPUT_NAME_LEN] = {0};
    if (resolve_geometry(app, cfg, &x, &y, &w, &h, name) == 0)
      window_key(cfg, name, key, sizeof(key));
    else
      snprintf(key, sizeof(key), "%s", cfg->output);
  }
  int idx = find_window_by_key(app, key);
  if (idx >= 0)
    remove_window(app, idx);
  return 0;
}

int xw_app_clear_all(XWApp *app) {
  while (app->nwins > 0)
    remove_window(app, 0);
  return 0;
}

void xw_app_stop(XWApp *app) {
  app->stop_requested = 1;
}

int xw_app_stopped(XWApp *app) {
  return app->stop_requested;
}

int xw_app_window_count(XWApp *app) {
  return app->nwins;
}

XWWindow *xw_app_window(XWApp *app, int i) {
  if (!app || i < 0 || i >= app->nwins)
    return NULL;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return NULL;
#endif
  return app->wins[i];
}

int xw_app_use_wayland(XWApp *app) {
  if (!app)
    return 0;
#ifdef XW_HAVE_WAYLAND
  return app->use_wayland;
#else
  return 0;
#endif
}

int xw_app_wayland_fd(XWApp *app) {
  if (!app)
    return -1;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return xw_wl_get_fd(app->wl);
#endif
  return -1;
}

void xw_app_wayland_dispatch(XWApp *app) {
  if (!app)
    return;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    xw_wl_dispatch(app->wl);
#endif
}

XWWlWindow *xw_app_wl_window(XWApp *app, int i) {
  if (!app || i < 0 || i >= app->nwins)
    return NULL;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return app->wlwins[i];
#endif
  return NULL;
}

Display *xw_app_display(XWApp *app) {
  return app->dpy;
}

int xw_app_window_info(XWApp *app, int i, XWWindowInfo *info) {
  if (!app || !info || i < 0 || i >= app->nwins)
    return -1;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland) {
    XWWlWindow *win = app->wlwins[i];
    if (!win)
      return -1;
    info->type = win->type;
    info->output = win->output;
    info->path = win->path;
    info->rotation = win->rotation;
    info->x = win->x;
    info->y = win->y;
    info->w = win->width;
    info->h = win->height;
    info->mute = win->mute;
    info->volume = win->volume;
    info->fps = win->fps;
    info->scale_mode = win->scale_mode;
    info->opacity = win->opacity;
    info->below = win->below;
    info->sticky = win->sticky;
    info->skip_taskbar = win->skip_taskbar;
    info->skip_pager = win->skip_pager;
    info->no_focus = win->no_focus;
    info->keybinds = win->keybinds;
    info->audio_client_name = win->audio_client_name;
    return 0;
  }
#endif
  XWWindow *win = app->wins[i];
  if (!win)
    return -1;
  info->type = win->type;
  info->output = win->output;
  info->path = win->path;
  info->rotation = win->rotation;
  info->x = win->x;
  info->y = win->y;
  info->w = win->width;
  info->h = win->height;
  info->mute = win->mute;
  info->volume = win->volume;
  info->fps = win->fps;
  info->scale_mode = win->scale_mode;
  info->opacity = win->opacity;
  info->below = win->below;
  info->sticky = win->sticky;
  info->skip_taskbar = win->skip_taskbar;
  info->skip_pager = win->skip_pager;
  info->no_focus = win->no_focus;
  info->keybinds = win->keybinds;
  info->audio_client_name = win->audio_client_name;
  return 0;
}

/* 是否存在会持续出帧的渲染后端 (实现了 set_paused, 如 video)。有则 daemon
 * 事件循环必须保持 500ms 节拍轮询 DPMS, 才能在熄屏时暂停它防止点亮显示器。 */
int xw_app_dpms_watch_required(XWApp *app) {
  for (int i = 0; i < app->nwins; i++)
    if (app->backends[i] && app->backends[i]->set_paused)
      return 1;
  return 0;
}

int xw_app_scene_tick_required(XWApp *app) {
  if (!app)
    return 0;
  for (int i = 0; i < app->nwins; i++)
    if (app->backends[i] && app->backends[i]->tick)
      return 1;
  return 0;
}

void xw_app_tick_backends(XWApp *app) {
  if (!app)
    return;
  for (int i = 0; i < app->nwins; i++)
    if (app->backends[i] && app->backends[i]->tick)
      app->backends[i]->tick(app->backends[i]);
}

/* Feed a key press to the focused wallpaper's bindings: the key is
 * normalised the same way the file spells it, looked up, and turned into the
 * backend control action it names. Returns 1 when a binding consumed it.
 * An action this backend cannot do is silently ignored — a keypress must
 * never surface an error. */
static int dispatch_key(XWWindow *win, XWBackend *b, XEvent *ev) {
  char key[XW_BIND_KEY_MAX], name[32], value[64];
  const XWBind *bind;
  const char *sym;
  KeySym ks = NoSymbol;
  XComposeStatus cs;
  if (!win || win->nbinds <= 0)
    return 0;
  memset(&cs, 0, sizeof(cs));
  XLookupString(&ev->xkey, NULL, 0, &ks, &cs);
  sym = ks != NoSymbol ? XKeysymToString(ks) : NULL;
  if (!sym || xw_keybinds_norm_key(sym, key, sizeof(key)) != 0)
    return 0;
  bind = xw_keybinds_find(win->binds, win->nbinds, key);
  if (!bind ||
      xw_keybinds_to_action(bind, name, sizeof(name), value, sizeof(value)) != 0)
    return 0;
  if (b && b->control) {
    XWError err;
    memset(&err, 0, sizeof(err));
    (void)b->control(b, name, value, &err);
  }
  return 1;
}

int xw_app_handle_event(XWApp *app, XEvent *ev) {
  for (int i = 0; i < app->nwins; i++) {
    XWWindow *win = app->wins[i];
    if (win && ev->xany.window == win->win) {
      if (ev->type == KeyPress)
        dispatch_key(win, app->backends[i], ev);
      xw_x11_handle_event(win, ev);
      if (app->backends[i] && app->backends[i]->on_event)
        app->backends[i]->on_event(app->backends[i], ev);
      return 1;
    }
  }
  return 0;
}

/* Resolve the backend addressed by a props command (primary resolves to
 * the primary output, -m names an output or -g name). */
static int props_backend(XWApp *app, const XWConfig *cfg, XWError *err) {
  int x, y;
  unsigned int w, h;
  char resolved[XW_OUTPUT_NAME_LEN] = {0};
  char key[XW_OUTPUT_NAME_LEN] = {0};
  if (!app || !cfg) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
#ifdef XW_HAVE_WAYLAND
  if (resolve_geometry(app, cfg, &x, &y, &w, &h, resolved) != 0) {
#else
  if (xw_x11_resolve_geometry(app->dpy, cfg, &x, &y, &w, &h, resolved) != 0) {
#endif
    XW_ERROR_SET(err, 1, "cannot resolve output (use --list to see outputs)");
    return -1;
  }
  window_key(cfg, resolved, key, sizeof(key));
  return find_window_by_key(app, key);
}

int xw_app_list_props(XWApp *app, const XWConfig *cfg, char *out, size_t n,
                      XWError *err) {
  int idx = props_backend(app, cfg, err);
  XWBackend *b;
  if (idx < 0) {
    if (err && !err->code)
      XW_ERROR_SET(err, 1, "no wallpaper on that output");
    return -1;
  }
  b = app->backends[idx];
  if (!b || !b->list_props) {
    XW_ERROR_SET(err, 1, "that wallpaper has no user properties");
    return -1;
  }
  return b->list_props(b, out, n);
}

int xw_app_set_prop(XWApp *app, const XWConfig *cfg, const char *name,
                    const char *value, XWError *err) {
  int idx = props_backend(app, cfg, err);
  XWBackend *b;
  if (idx < 0) {
    if (err && !err->code)
      XW_ERROR_SET(err, 1, "no wallpaper on that output");
    return -1;
  }
  b = app->backends[idx];
  if (!b || !b->set_prop) {
    XW_ERROR_SET(err, 1, "that wallpaper has no user properties");
    return -1;
  }
  return b->set_prop(b, name, value, err);
}

int xw_app_clear_props(XWApp *app, const XWConfig *cfg, XWError *err) {
  int idx = props_backend(app, cfg, err);
  XWBackend *b;
  if (idx < 0) {
    if (err && !err->code)
      XW_ERROR_SET(err, 1, "no wallpaper on that output");
    return -1;
  }
  b = app->backends[idx];
  if (!b || !b->clear_props) {
    XW_ERROR_SET(err, 1, "that wallpaper has no user properties");
    return -1;
  }
  return b->clear_props(b, err);
}

int xw_app_control(XWApp *app, const XWConfig *cfg, const char *name,
                   const char *value, XWError *err) {
  int idx;
  XWBackend *b;
  if (!app || !cfg || !name || !name[0]) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (cfg->target == XW_TARGET_ALL) {
    XWError first;
    int i, hit = 0, ok = 0;
    memset(&first, 0, sizeof(first));
    for (i = 0; i < app->nwins; i++) {
      XWError e;
      memset(&e, 0, sizeof(e));
      b = app->backends[i];
      if (!b)
        continue;
      hit++;
      if (b->control && b->control(b, name, value, &e) == 0) {
        ok++;
      } else if (!first.msg[0]) {
        first = e;
      }
    }
    if (ok > 0)
      return 0;
    if (!hit)
      XW_ERROR_SET(err, 1, "no wallpapers to control");
    else if (first.msg[0])
      XW_ERROR_SET(err, 1, "%s", first.msg);
    else
      XW_ERROR_SET(err, 1, "no wallpaper supports '%s'", name);
    return -1;
  }
  idx = props_backend(app, cfg, err);
  if (idx < 0) {
    if (err && !err->code)
      XW_ERROR_SET(err, 1, "no wallpaper on that output");
    return -1;
  }
  b = app->backends[idx];
  if (!b || !b->control) {
    XW_ERROR_SET(err, 1, "that wallpaper does not support '%s'", name);
    return -1;
  }
  return b->control(b, name, value, err);
}

#define DPMS_POLL_INTERVAL_MS 500

static uint64_t now_monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* Distribute the current DPMS state to one backend. Newly created backends
 * must start paused if the display is already powered off, otherwise they
 * keep presenting frames until the next state change. */
static void apply_dpms_to_backend(XWApp *app, XWBackend *b) {
  if (app->dpms_power_level >= 0 && b && b->set_paused)
    b->set_paused(b, app->dpms_power_level != DPMSModeOn);
}

void xw_app_dpms_poll(XWApp *app) {
  if (!app)
    return;
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland)
    return; /* no DPMS on Wayland (ext-idle-notify is future work) */
#endif
  if (!app->dpy)
    return;
  if (!app->dpms_queried) {
    app->dpms_queried = 1;
    /* libXext's DPMSQueryExtension writes both bases unconditionally —
     * pass real locals, never NULL */
    int ev_base, err_base;
    app->dpms_available =
        DPMSQueryExtension(app->dpy, &ev_base, &err_base);
  }
  if (!app->dpms_available)
    return;
  uint64_t now = now_monotonic_ms();
  if (now - app->dpms_last_poll_ms < DPMS_POLL_INTERVAL_MS)
    return;
  app->dpms_last_poll_ms = now;

  CARD16 power_level;
  BOOL state;
  if (!DPMSInfo(app->dpy, &power_level, &state))
    return;
  if (power_level == app->dpms_power_level)
    return;
  app->dpms_power_level = power_level;

  /* every powered-off mode (standby/suspend/off) must stop presenting */
  for (int i = 0; i < app->nwins; i++)
    apply_dpms_to_backend(app, app->backends[i]);
}

static void pump_gtk(void) {
  /* non-blocking: drain sources that are ready now */
  g_main_context_iteration(NULL, FALSE);
}

/* WE audio wallpapers need their 20Hz fan-out tick; scene wallpapers
 * animate on tick (~30Hz); idle loops poll at 100ms to stay
 * signal-responsive. */
static int app_poll_timeout_ms(XWApp *app) {
  if (xw_web_we_want())
    return 50;
  if (xw_app_scene_tick_required(app))
    return 33;
  return 100;
}

int xw_app_run(XWApp *app) {
#ifdef XW_HAVE_WAYLAND
  if (app->use_wayland) {
    int wfd = xw_wl_get_fd(app->wl);
    while (!app->stop_requested) {
      if (app->uses_gtk)
        pump_gtk();
      xw_web_we_tick();
      xw_app_tick_backends(app);

      struct pollfd pfd = {wfd, POLLIN, 0};
      int r = poll(&pfd, 1, app_poll_timeout_ms(app));

      if (r < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      /* non-blocking: collects frame/configure events, flushes commits */
      xw_wl_dispatch(app->wl);
    }
    return 0;
  }
#endif
  int xfd = ConnectionNumber(app->dpy);

  while (!app->stop_requested) {
    if (app->uses_gtk)
      pump_gtk();
    xw_app_dpms_poll(app);
    xw_web_we_tick();
    xw_app_tick_backends(app);

    struct pollfd pfd = {xfd, POLLIN, 0};
    int r = poll(&pfd, 1, app_poll_timeout_ms(app));

    if (r < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (!(pfd.revents & POLLIN))
      continue;

    while (XPending(app->dpy) > 0 && !app->stop_requested) {
      XEvent ev;
      XNextEvent(app->dpy, &ev);
      xw_app_handle_event(app, &ev);
    }
  }
  return 0;
}

XWApp *xw_app_get(void) {
  return the_app;
}

void xw_app_destroy(XWApp *app) {
  if (!app)
    return;
  xw_app_clear_all(app);
  free(app->backends);
  free(app->wins);
#ifdef XW_HAVE_WAYLAND
  free(app->wlwins);
  if (app->wl)
    xw_wl_disconnect(app->wl);
#endif
  if (app->dpy)
    xw_x11_close_display(app->dpy);
  if (the_app == app)
    the_app = NULL;
  free(app);
}

/* ------------------------------------------------------------------ *
 *  Persistent per-name state
 * ------------------------------------------------------------------ */

/* snapshot entry i regardless of platform */
static void entry_from_index(XWApp *app, XWStateEntry *e, int i, int keep) {
  XWWindowInfo wi;
  memset(e, 0, sizeof(*e));
  if (xw_app_window_info(app, i, &wi) != 0)
    return;
  snprintf(e->name, sizeof(e->name), "%s",
           wi.output[0] ? wi.output : "unnamed");
  e->type = wi.type;
  snprintf(e->path, sizeof(e->path), "%s", wi.path);
  e->rotation = wi.rotation;
  /* 总是保存几何; 恢复时若 name 命中当前 output 则重解析 */
  e->have_geom = 1;
  e->x = wi.x;
  e->y = wi.y;
  e->w = wi.w;
  e->h = wi.h;
  e->keep = keep;
  e->mute = wi.mute;
  e->volume = wi.volume;
  e->fps = wi.fps;
  e->scale_mode = wi.scale_mode;
  e->opacity = wi.opacity;
  e->below = wi.below;
  e->sticky = wi.sticky;
  e->skip_taskbar = wi.skip_taskbar;
  e->skip_pager = wi.skip_pager;
  e->no_focus = wi.no_focus;
  snprintf(e->keybinds, sizeof(e->keybinds), "%s",
           wi.keybinds ? wi.keybinds : "");
  snprintf(e->audio_client_name, sizeof(e->audio_client_name), "%s",
           wi.audio_client_name ? wi.audio_client_name : "");
}

/* 平台相关的状态文件: Wayland 用 state-wayland (monitor 命名空间与
 * xrandr 不一致，共用会贴错屏)。X11-only 构建下恒走旧路径。 */
static int app_state_path(XWApp *app, char *out, size_t n) {
#ifdef XW_HAVE_WAYLAND
  if (app && app->use_wayland)
    return xw_state_path_for(1, out, n);
#else
  (void)app;
#endif
  return xw_state_path(out, n);
}

int xw_app_save_state(XWApp *app) {
  char path[1024];
  if (app_state_path(app, path, sizeof(path)) != 0)
    return -1;

  /* keep=1 且无活跃窗口的旧条目 (clear --keep 保留的 last) 继续保留 */
  XWStateEntry old[XW_STATE_MAX_ENTRIES];
  int on = 0;
  xw_state_load(path, old, XW_STATE_MAX_ENTRIES, &on);

  XWStateEntry out[XW_STATE_MAX_ENTRIES];
  int n = 0;
  for (int i = 0; i < on && n < XW_STATE_MAX_ENTRIES; i++) {
    if (old[i].keep && find_window_by_key(app, old[i].name) < 0)
      out[n++] = old[i];
  }
  for (int i = 0; i < app->nwins && n < XW_STATE_MAX_ENTRIES; i++) {
    entry_from_index(app, &out[n], i, 0);
    n++;
  }
  return xw_state_save(path, out, n);
}

int xw_app_keep(XWApp *app, const char *name) {
  char path[1024];
  if (app_state_path(app, path, sizeof(path)) != 0)
    return -1;
  int idx = find_window_by_key(app, name);
  if (idx < 0)
    return 0; /* nothing to keep */
  XWStateEntry e;
  entry_from_index(app, &e, idx, 1);
  return xw_state_upsert(path, &e);
}

int xw_app_keep_all(XWApp *app) {
  int r = 0;
  for (int i = 0; i < app->nwins; i++) {
    const char *output = window_key_at(app, i);
    if (output[0] && xw_app_keep(app, output) != 0)
      r = -1;
  }
  return r;
}

/* 重建一个状态条目为活跃窗口。
 * 优先把 name 当作 xrandr output 解析 (几何实时), 失败再回退保存的 geom
 * (grp_* / Screen 这类 -g 窗口)。返回 0 成功或跳过, -1 失败。 */
static int restore_entry(XWApp *app, const XWStateEntry *e) {
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: restore: %s type=%d path=%s\n", e->name,
            e->type, e->path);
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.command = XW_CMD_SET;
  cfg.backend_explicit = e->type;
  /* 与 CLI set 默认一致: memset(0) 会漏掉 opacity/scale_mode/volume 等,
   * 尤其 opacity=0 会让窗口被设 _NET_WM_WINDOW_OPACITY=0 → 合成器下全透明。
   * 回填状态文件里保存的 flags; 旧 6 字段行缺失时 entry 已由 parse 端补
   * 默认 (mute 经哨兵 -1 由 xw_state_effective_mute 规范为 video→1)。 */
  cfg.scale_mode = e->scale_mode;
  cfg.volume = e->volume;
  cfg.opacity = e->opacity;
  cfg.fps = e->fps;
  cfg.mute = xw_state_effective_mute(e);
  cfg.below = e->below;
  cfg.sticky = e->sticky;
  cfg.skip_taskbar = e->skip_taskbar;
  cfg.skip_pager = e->skip_pager;
  cfg.no_focus = e->no_focus;
  snprintf(cfg.keybinds, sizeof(cfg.keybinds), "%s", e->keybinds);
  snprintf(cfg.audio_client_name, sizeof(cfg.audio_client_name), "%s",
           e->audio_client_name[0] ? e->audio_client_name : "xwallpaper");
  snprintf(cfg.path, sizeof(cfg.path), "%s", e->path);
  cfg.rotate = e->rotation;

  cfg.target = XW_TARGET_OUTPUT;
  /* %.*s: 显式边界 (最多读 sizeof(e->name)-1), 避免 %s 触发
   * -Wformat-truncation 保守告警 (目标 output[64] 恰好容纳) */
  snprintf(cfg.output, sizeof(cfg.output), "%.*s", (int)sizeof(e->name) - 1,
           e->name);
  int x, y;
  unsigned int w, h;
  char oname[XW_OUTPUT_NAME_LEN];
  const char *mode = "output";
  if (resolve_geometry(app, &cfg, &x, &y, &w, &h, oname) != 0 ||
      !oname[0]) {
    if (!e->have_geom) {
      if (getenv("XW_DEBUG_DAEMON"))
        fprintf(stderr,
                "xwallpaperd: restore: %s skip (not an output, no saved geom)\n",
                e->name);
      return 0; /* 既非 output 也无保存几何: 跳过 */
    }
    mode = "geom";
    cfg.target = XW_TARGET_GEOM;
    cfg.have_geom = 1;
    cfg.geom_x = e->x;
    cfg.geom_y = e->y;
    cfg.geom_w = e->w;
    cfg.geom_h = e->h;
    snprintf(cfg.name, sizeof(cfg.name), "%.*s", (int)sizeof(e->name) - 1,
             e->name);
  }
  XWError err;
  memset(&err, 0, sizeof(err));
  int r = xw_app_set(app, &cfg, &err);
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: restore: %s %s %s (%s)\n", e->name, mode,
            r == 0 ? "ok" : "FAIL", r == 0 ? "" : err.msg);
  return r == 0 ? 0 : -1;
}

/* keep_flag 为 0 时重建当前窗口 (启动恢复), 为 1 时重建 kept last (restore
 * 命令)。两者共享同一恢复流程, 仅过滤条件与是否回写快照不同。 */
static int restore_entries(XWApp *app, int keep_flag) {
  char path[1024];
  if (app_state_path(app, path, sizeof(path)) != 0)
    return -1;
  XWStateEntry entries[XW_STATE_MAX_ENTRIES];
  int count = 0;
  if (xw_state_load(path, entries, XW_STATE_MAX_ENTRIES, &count) != 0) {
    if (getenv("XW_DEBUG_DAEMON"))
      fprintf(stderr, "xwallpaperd: restore: no usable state file (%s)\n",
              path);
    return 0; /* 无可用状态 */
  }
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: restore: keep=%d path=%s entries=%d\n",
            keep_flag, path, count);
  for (int i = 0; i < count; i++) {
    if (entries[i].keep != keep_flag)
      continue;
    if (find_window_by_key(app, entries[i].name) >= 0) {
      if (getenv("XW_DEBUG_DAEMON"))
        fprintf(stderr,
                "xwallpaperd: restore: %s skip (already an active window)\n",
                entries[i].name);
      continue; /* 已是活跃窗口 */
    }
    restore_entry(app, &entries[i]);
  }
  if (keep_flag)
    return xw_app_save_state(
        app); /* 活跃窗口变 keep=0, 未恢复的 keep 条目保持 keep=1 */
  return 0;
}

int xw_app_restore_startup(XWApp *app) { return restore_entries(app, 0); }

int xw_app_restore_pending(XWApp *app) {
  char path[1024];
  if (app_state_path(app, path, sizeof(path)) != 0)
    return 0;
  XWStateEntry entries[XW_STATE_MAX_ENTRIES];
  int count = 0;
  if (xw_state_load(path, entries, XW_STATE_MAX_ENTRIES, &count) != 0)
    return 0;
  int pending = 0;
  for (int i = 0; i < count; i++)
    if (entries[i].keep == 0 && find_window_by_key(app, entries[i].name) < 0)
      pending++;
  return pending;
}

int xw_app_restore_kept(XWApp *app) { return restore_entries(app, 1); }

int xw_app_retry_backends(XWApp *app) {
  if (!app)
    return 0;
  int n = 0;
  for (int i = 0; i < app->nwins; i++) {
    XWBackend *b = app->backends[i];
    if (b && b->retry && b->retry(b) > 0)
      n++;
  }
  return n;
}
