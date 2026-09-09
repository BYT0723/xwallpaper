#include <poll.h>
#include <string.h>

#include "wl.h"

/* Primary output: largest area (Wayland has no user-designated primary;
 * first enumerated wins ties). Better default than outs[0], which is
 * often a side monitor. */
static int primary_index(const XWWlOutput *outs, int nouts) {
  int best = 0;
  unsigned long long best_area = 0;
  for (int i = 0; i < nouts; i++) {
    unsigned long long a =
        (unsigned long long)outs[i].width * outs[i].height;
    if (i == 0 || a > best_area) {
      best = i;
      best_area = a;
    }
  }
  return best;
}

int xw_wl_resolve_geometry(const XWWlOutput *outs, int nouts,
                           const XWConfig *cfg,
                           int *x, int *y, unsigned int *w, unsigned int *h,
                           char *out_name) {
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
    for (int i = 0; i < nouts; i++) {
      if (strcmp(outs[i].name, cfg->output) == 0) {
        *x = outs[i].x;
        *y = outs[i].y;
        *w = outs[i].width;
        *h = outs[i].height;
        if (out_name)
          snprintf(out_name, XW_WL_OUTPUT_NAME_LEN, "%s", cfg->output);
        return 0;
      }
    }
    return -1;
  }

  if (nouts <= 0 || !outs)
    return -1;
  const XWWlOutput *p = &outs[primary_index(outs, nouts)];
  *x = p->x;
  *y = p->y;
  *w = p->width;
  *h = p->height;
  if (out_name)
    snprintf(out_name, XW_WL_OUTPUT_NAME_LEN, "%s", p->name);
  return 0;
}

void xw_wl_list_outputs(const XWWlOutput *outs, int nouts, FILE *out) {
  if (!outs || nouts <= 0) {
    fprintf(out, "  (no wayland outputs available)\n");
    return;
  }
  for (int i = 0; i < nouts; i++)
    fprintf(out, "  %s %ux%u+%d+%d\n",
            outs[i].name, outs[i].width, outs[i].height, outs[i].x, outs[i].y);
}

#ifdef XW_HAVE_WAYLAND
#include <stdlib.h>

#include <wayland-client.h>

#include "proto/wlr-layer-shell-unstable-v1-client-protocol.h"
#include "proto/xdg-output-unstable-v1-client-protocol.h"

#define XW_WL_MAX_OUTPUTS 32

struct XWWlOut {
  struct wl_output *wl_out;
  struct zxdg_output_v1 *xdg_out;
  char name[XW_WL_OUTPUT_NAME_LEN];
  int have_name;
  int x, y;
  unsigned int w, h;
  int have_logical;
  int mode_w, mode_h;
  int scale;
  int have_mode;
};

struct xw_wl_ctx {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct zwlr_layer_shell_v1 *shell;
  struct zxdg_output_manager_v1 *xdg_mgr;
  struct XWWlOut *sinks;
  int nsinks;
  XWWlOutput *outs;
  int nouts;
};

static void out_handle_geometry(void *data, struct wl_output *o, int32_t x,
                                int32_t y, int32_t pw, int32_t ph, int32_t sub,
                                const char *make, const char *model,
                                int32_t transform) {
  (void)o; (void)pw; (void)ph; (void)sub; (void)make; (void)model;
  (void)transform;
  struct XWWlOut *s = data;
  s->x = x;
  s->y = y;
}

static void out_handle_mode(void *data, struct wl_output *o, uint32_t flags,
                            int32_t w, int32_t h, int32_t refresh) {
  (void)o; (void)refresh;
  struct XWWlOut *s = data;
  if (flags & WL_OUTPUT_MODE_CURRENT) {
    s->mode_w = w;
    s->mode_h = h;
    s->have_mode = 1;
  }
}

static void out_handle_done(void *data, struct wl_output *o) {
  (void)data; (void)o;
}

static void out_handle_scale(void *data, struct wl_output *o, int32_t scale) {
  (void)o;
  struct XWWlOut *s = data;
  if (scale > 0)
    s->scale = scale;
}

static void out_handle_name(void *data, struct wl_output *o, const char *name) {
  (void)o;
  struct XWWlOut *s = data;
  if (name && !s->have_name) {
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->have_name = 1;
  }
}

static void out_handle_description(void *data, struct wl_output *o,
                                   const char *desc) {
  (void)data; (void)o; (void)desc;
}

static const struct wl_output_listener out_listener = {
  out_handle_geometry, out_handle_mode, out_handle_done,
  out_handle_scale, out_handle_name, out_handle_description,
};

static void xdg_handle_logical_position(void *data, struct zxdg_output_v1 *o,
                                        int32_t x, int32_t y) {
  (void)o;
  struct XWWlOut *s = data;
  s->x = x;
  s->y = y;
  s->have_logical = 1;
}

static void xdg_handle_logical_size(void *data, struct zxdg_output_v1 *o,
                                    int32_t w, int32_t h) {
  (void)o;
  struct XWWlOut *s = data;
  if (w > 0 && h > 0) {
    s->w = (unsigned int)w;
    s->h = (unsigned int)h;
  }
}

static void xdg_handle_done(void *data, struct zxdg_output_v1 *o) {
  (void)data; (void)o;
}

static void xdg_handle_name(void *data, struct zxdg_output_v1 *o,
                            const char *name) {
  struct XWWlOut *s = data;
  (void)o;
  if (name && name[0]) {
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->have_name = 1;
  }
}

static void xdg_handle_description(void *data, struct zxdg_output_v1 *o,
                                   const char *desc) {
  (void)data; (void)o; (void)desc;
}

static const struct zxdg_output_v1_listener xdg_listener = {
  xdg_handle_logical_position, xdg_handle_logical_size, xdg_handle_done,
  xdg_handle_name, xdg_handle_description,
};

static uint32_t ver_min(uint32_t have, uint32_t want) {
  return have < want ? have : want;
}

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *interface, uint32_t version) {
  xw_wl_ctx *ctx = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    ctx->compositor =
        wl_registry_bind(reg, name, &wl_compositor_interface, ver_min(version, 6));
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    ctx->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    if (ctx->nsinks >= XW_WL_MAX_OUTPUTS)
      return;
    struct XWWlOut *s = &ctx->sinks[ctx->nsinks++];
    memset(s, 0, sizeof(*s));
    s->scale = 1;
    s->wl_out =
        wl_registry_bind(reg, name, &wl_output_interface, ver_min(version, 2));
    wl_output_add_listener(s->wl_out, &out_listener, s);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    ctx->shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface,
                                  ver_min(version, 3));
  } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
    ctx->xdg_mgr = wl_registry_bind(reg, name, &zxdg_output_manager_v1_interface,
                                    ver_min(version, 3));
  }
}

static void registry_global_remove(void *data, struct wl_registry *reg,
                                   uint32_t name) {
  (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
  registry_global, registry_global_remove,
};

xw_wl_ctx *xw_wl_connect(XWError *err) {
  xw_wl_ctx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return NULL;
  }
  ctx->sinks = calloc(XW_WL_MAX_OUTPUTS, sizeof(*ctx->sinks));
  if (!ctx->sinks) {
    XW_ERROR_SET(err, 1, "out of memory");
    free(ctx);
    return NULL;
  }

  ctx->display = wl_display_connect(NULL);
  if (!ctx->display) {
    XW_ERROR_SET(err, 1, "cannot connect to Wayland display "
                         "(is WAYLAND_DISPLAY set?)");
    goto fail;
  }
  ctx->registry = wl_display_get_registry(ctx->display);
  wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
  if (wl_display_roundtrip(ctx->display) < 0)
    goto fail_roundtrip;

  if (!ctx->shell) {
    XW_ERROR_SET(err, 1, "compositor does not support wlr-layer-shell");
    goto fail;
  }
  if (ctx->nsinks == 0) {
    XW_ERROR_SET(err, 1, "no wayland outputs found");
    goto fail;
  }

  for (int i = 0; i < ctx->nsinks; i++) {
    struct XWWlOut *s = &ctx->sinks[i];
    if (ctx->xdg_mgr) {
      s->xdg_out = zxdg_output_manager_v1_get_xdg_output(ctx->xdg_mgr, s->wl_out);
      zxdg_output_v1_add_listener(s->xdg_out, &xdg_listener, s);
    }
  }
  if (wl_display_roundtrip(ctx->display) < 0 ||
      wl_display_roundtrip(ctx->display) < 0)
    goto fail_roundtrip;

  ctx->outs = calloc((size_t)ctx->nsinks, sizeof(*ctx->outs));
  if (!ctx->outs) {
    XW_ERROR_SET(err, 1, "out of memory");
    goto fail;
  }
  for (int i = 0; i < ctx->nsinks; i++) {
    struct XWWlOut *s = &ctx->sinks[i];
    XWWlOutput *o = &ctx->outs[ctx->nouts];
    if (!s->have_logical) {
      if (s->have_mode && s->scale > 0) {
        s->w = (unsigned int)(s->mode_w / s->scale);
        s->h = (unsigned int)(s->mode_h / s->scale);
      } else {
        continue;
      }
    }
    if (!s->have_name)
      snprintf(s->name, sizeof(s->name), "WL-%d", i + 1);
    snprintf(o->name, sizeof(o->name), "%s", s->name);
    o->x = s->x;
    o->y = s->y;
    o->width = s->w;
    o->height = s->h;
    ctx->nouts++;
  }
  if (ctx->nouts == 0) {
    XW_ERROR_SET(err, 1, "no wayland outputs found");
    goto fail;
  }
  return ctx;

fail_roundtrip:
  XW_ERROR_SET(err, 1, "wayland compositor roundtrip failed");
fail:
  xw_wl_disconnect(ctx);
  return NULL;
}

int xw_wl_output_count(xw_wl_ctx *ctx) {
  return ctx ? ctx->nouts : 0;
}

const XWWlOutput *xw_wl_outputs(xw_wl_ctx *ctx) {
  return ctx ? ctx->outs : NULL;
}

struct XWWlWinCb {
  XWWlWindow *win;
  int configured;
  int closed;
};

static void layer_handle_configure(void *data, struct zwlr_layer_surface_v1 *l,
                                   uint32_t serial, uint32_t w, uint32_t h) {
  struct XWWlWinCb *cb = data;
  (void)l;
  if (w > 0 && h > 0) {
    cb->win->width = w;
    cb->win->height = h;
  }
  zwlr_layer_surface_v1_ack_configure(l, serial);
  wl_surface_commit((struct wl_surface *)cb->win->surface);
  cb->configured = 1;
}

static void layer_handle_closed(void *data, struct zwlr_layer_surface_v1 *l) {
  struct XWWlWinCb *cb = data;
  (void)l;
  cb->closed = 1;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
  layer_handle_configure, layer_handle_closed,
};

/* Shared surface+layer construction. Placement is explicit: fullscreen
 * callers pass all-edges anchor + exclusive -1, positioned callers pass
 * TOP|LEFT anchor + margins + exclusive 0. */
struct XWWlPlace {
  struct wl_output *output; /* may be NULL (compositor picks) */
  uint32_t anchor;
  int32_t margin_top, margin_right, margin_bottom, margin_left;
  int32_t exclusive;
};

static XWWlWindow *window_new(xw_wl_ctx *ctx, const struct XWWlPlace *pl,
                              unsigned int w, unsigned int h, XWError *err) {
  if (!ctx || !ctx->compositor || !ctx->shell) {
    XW_ERROR_SET(err, 1, "wayland not connected");
    return NULL;
  }
  if (w == 0 || h == 0) {
    XW_ERROR_SET(err, 1, "invalid window size %ux%u", w, h);
    return NULL;
  }

  XWWlWindow *win = calloc(1, sizeof(*win));
  if (!win) {
    XW_ERROR_SET(err, 1, "out of memory");
    return NULL;
  }
  win->width = w;
  win->height = h;

  struct wl_surface *surf = wl_compositor_create_surface(ctx->compositor);
  if (!surf) {
    XW_ERROR_SET(err, 1, "cannot create wayland surface");
    free(win);
    return NULL;
  }
  win->surface = surf;

  struct zwlr_layer_surface_v1 *layer =
      zwlr_layer_shell_v1_get_layer_surface(ctx->shell, surf, pl->output,
                                            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
                                            "xwallpaper");
  if (!layer) {
    XW_ERROR_SET(err, 1, "compositor rejected the layer surface");
    wl_surface_destroy(surf);
    free(win);
    return NULL;
  }
  win->layer = layer;

  zwlr_layer_surface_v1_set_size(layer, w, h);
  zwlr_layer_surface_v1_set_anchor(layer, pl->anchor);
  zwlr_layer_surface_v1_set_margin(layer, pl->margin_top, pl->margin_right,
                                   pl->margin_bottom, pl->margin_left);
  zwlr_layer_surface_v1_set_exclusive_zone(layer, pl->exclusive);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

  struct XWWlWinCb cb = {.win = win, .configured = 0, .closed = 0};
  zwlr_layer_surface_v1_add_listener(layer, &layer_listener, &cb);
  wl_surface_commit(surf);

  for (int i = 0; i < 200 && !cb.configured && !cb.closed; i++) {
    if (wl_display_dispatch(ctx->display) < 0)
      break;
  }
  if (!cb.configured) {
    XW_ERROR_SET(err, 1, cb.closed ? "compositor closed the layer surface"
                                   : "no configure from compositor");
    zwlr_layer_surface_v1_destroy(layer);
    wl_surface_destroy(surf);
    free(win);
    return NULL;
  }
  return win;
}

/* compositor output handle by config/output name (NULL if unknown) */
static struct wl_output *find_sink(xw_wl_ctx *ctx, const char *name) {
  if (!ctx || !name || !name[0])
    return NULL;
  for (int i = 0; i < ctx->nsinks; i++) {
    if (strcmp(ctx->sinks[i].name, name) == 0)
      return ctx->sinks[i].wl_out;
  }
  return NULL;
}

XWWlWindow *xw_wl_create_window(xw_wl_ctx *ctx, const XWConfig *cfg,
                               int x, int y, unsigned int w, unsigned int h,
                               XWError *err) {
  struct wl_output *wl_out = NULL;
  const char *want = NULL;
  if (!cfg->have_geom) {
    if (cfg->target == XW_TARGET_OUTPUT && cfg->output[0])
      want = cfg->output;
    else if (ctx->nouts > 0)
      want = ctx->outs[primary_index(ctx->outs, ctx->nouts)].name;
    wl_out = find_sink(ctx, want);
    if (want && !wl_out) {
      XW_ERROR_SET(err, 1, "unknown output '%.64s'", want);
      return NULL;
    }
  }

  struct XWWlPlace pl = {
    .output = wl_out,
    .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
    .margin_top = 0, .margin_right = 0,
    .margin_bottom = 0, .margin_left = 0,
    .exclusive = -1,
  };
  XWWlWindow *win = window_new(ctx, &pl, w, h, err);
  if (!win)
    return NULL;

  if (want)
    snprintf(win->output, sizeof(win->output), "%s", want);
  win->is_geometry = cfg->have_geom ? 1 : 0;
  win->wl_ctx = ctx;
  win->x = x;
  win->y = y;
  return win;
}

/* Positioned surface for span pieces: TOP|LEFT-anchored at (rx, ry)
 * relative to the bound output's origin, exclusive zone 0. Used when a
 * span rect covers only part of an output. */
XWWlWindow *xw_wl_create_window_at(xw_wl_ctx *ctx, const char *output,
                                   int rx, int ry,
                                   unsigned int w, unsigned int h,
                                   XWError *err) {
  if (!ctx || !output || !output[0]) {
    XW_ERROR_SET(err, 1, "span piece requires an output");
    return NULL;
  }
  struct wl_output *wl_out = find_sink(ctx, output);
  if (!wl_out) {
    XW_ERROR_SET(err, 1, "unknown output '%.64s'", output);
    return NULL;
  }
  if (rx < 0 || ry < 0) {
    XW_ERROR_SET(err, 1, "invalid piece offset %d+%d", rx, ry);
    return NULL;
  }

  struct XWWlPlace pl = {
    .output = wl_out,
    .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
    .margin_top = ry,
    .margin_right = 0,
    .margin_bottom = 0,
    .margin_left = rx,
    .exclusive = 0,
  };
  XWWlWindow *win = window_new(ctx, &pl, w, h, err);
  if (!win)
    return NULL;

  snprintf(win->output, sizeof(win->output), "%s", output);
  win->is_geometry = 0;
  win->wl_ctx = ctx;
  win->x = rx;
  win->y = ry;
  return win;
}

void xw_wl_destroy_window(XWWlWindow *win) {
  if (!win)
    return;
  if (win->layer)
    zwlr_layer_surface_v1_destroy((struct zwlr_layer_surface_v1 *)win->layer);
  if (win->surface)
    wl_surface_destroy((struct wl_surface *)win->surface);
  free(win);
}

void xw_wl_disconnect(xw_wl_ctx *ctx) {
  if (!ctx)
    return;
  for (int i = 0; i < ctx->nsinks; i++) {
    if (ctx->sinks[i].xdg_out)
      zxdg_output_v1_destroy(ctx->sinks[i].xdg_out);
    if (ctx->sinks[i].wl_out)
      wl_output_destroy(ctx->sinks[i].wl_out);
  }
  free(ctx->sinks);
  free(ctx->outs);
  if (ctx->xdg_mgr)
    zxdg_output_manager_v1_destroy(ctx->xdg_mgr);
  if (ctx->shell)
    zwlr_layer_shell_v1_destroy(ctx->shell);
  if (ctx->shm)
    wl_shm_destroy(ctx->shm);
  if (ctx->compositor)
    wl_compositor_destroy(ctx->compositor);
  if (ctx->registry)
    wl_registry_destroy(ctx->registry);
  if (ctx->display)
    wl_display_disconnect(ctx->display);
  free(ctx);
}

struct wl_shm *xw_wl_get_shm(xw_wl_ctx *ctx) {
  return ctx ? ctx->shm : NULL;
}

struct wl_display *xw_wl_get_display(xw_wl_ctx *ctx) {
  return ctx ? ctx->display : NULL;
}

int xw_wl_get_fd(xw_wl_ctx *ctx) {
  if (!ctx || !ctx->display)
    return -1;
  return wl_display_get_fd(ctx->display);
}

void xw_wl_dispatch(xw_wl_ctx *ctx) {
  if (!ctx || !ctx->display)
    return;
  /* dispatch_pending alone never reads the fd: without the read below,
   * frame/configure events sit in the socket buffer forever (black
   * screen deadlock). Non-blocking: only read when poll says data waits. */
  wl_display_dispatch_pending(ctx->display);
  struct pollfd pfd = {wl_display_get_fd(ctx->display), POLLIN, 0};
  if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
    while (wl_display_prepare_read(ctx->display) != 0)
      wl_display_dispatch_pending(ctx->display);
    if (wl_display_read_events(ctx->display) >= 0)
      wl_display_dispatch_pending(ctx->display);
    else
      wl_display_cancel_read(ctx->display);
  }
  wl_display_flush(ctx->display);
}
#endif /* XW_HAVE_WAYLAND */
