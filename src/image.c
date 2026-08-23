#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Imlib2.h>
#include <X11/Xlib.h>

#include "backend.h"
#include "x11.h"

typedef struct {
  XWWindow *win;
  Imlib_Context ctx;    /* per-instance context (avoids GC caching across windows) */
  Imlib_Image img;      /* original */
  Imlib_Image scaled;   /* scaled to current window size */
  int scale_mode;
  int last_w, last_h;
} ImageCtx;

static void img_get_size(Imlib_Context ctx, Imlib_Image img, int *w, int *h) {
  imlib_context_push(ctx);
  imlib_context_set_image(img);
  *w = imlib_image_get_width();
  *h = imlib_image_get_height();
  imlib_context_pop();
}

static int image_init(XWBackend *b, XWWindow *win, const XWBackendOpt *opt, XWError *err) {
  ImageCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->win = win;
  ctx->scale_mode = opt->scale_mode;

  ctx->ctx = imlib_context_new();
  if (!ctx->ctx) {
    XW_ERROR_SET(err, 1, "imlib_context_new failed");
    free(ctx);
    return -1;
  }
  imlib_context_push(ctx->ctx);
  imlib_context_set_display(win->dpy);
  imlib_context_set_visual(DefaultVisual(win->dpy, DefaultScreen(win->dpy)));
  imlib_context_set_colormap(DefaultColormap(win->dpy, DefaultScreen(win->dpy)));
  /* disable the XImage/GC cache: it holds a GC pointing at a destroyed window
   * when windows are torn down and recreated within one process */
  imlib_set_ximage_cache_size_max(0);

  ctx->img = imlib_load_image_immediately_without_cache(opt->path);
  imlib_context_pop();
  if (!ctx->img) {
    XW_ERROR_SET(err, 1, "cannot load image '%s'", opt->path);
    imlib_context_free(ctx->ctx);
    free(ctx);
    return -1;
  }
  b->priv = ctx;
  return 0;
}

static void render_tiled(ImageCtx *ctx) {
  int iw, ih;
  img_get_size(ctx->ctx, ctx->img, &iw, &ih);
  int W = ctx->win->width;
  int H = ctx->win->height;

  imlib_context_push(ctx->ctx);
  imlib_context_set_drawable(ctx->win->win);
  imlib_context_set_image(ctx->img);
  for (int y = 0; y < H; y += ih)
    for (int x = 0; x < W; x += iw)
      imlib_render_image_on_drawable(x, y);
  imlib_context_pop();
}

/* compute scaled rect for contain/fill */
static void scale_rect(int sw, int sh, int dw, int dh, int mode,
                       int *ox, int *oy, int *ow, int *oh) {
  if (mode == XW_SCALE_TILE || mode == XW_SCALE_STRETCH) {
    *ox = 0; *oy = 0; *ow = dw; *oh = dh;
    return;
  }
  double s;
  if (mode == XW_SCALE_FILL)
    s = (double)dw / sw > (double)dh / sh ? (double)dw / sw : (double)dh / sh;
  else /* CONTAIN */
    s = (double)dw / sw < (double)dh / sh ? (double)dw / sw : (double)dh / sh;

  *ow = (int)(sw * s);
  *oh = (int)(sh * s);
  *ox = (dw - *ow) / 2;
  *oy = (dh - *oh) / 2;
}

static void render_scaled(ImageCtx *ctx) {
  int iw, ih;
  img_get_size(ctx->ctx, ctx->img, &iw, &ih);
  int ox = 0, oy = 0, ow = 0, oh = 0;

  scale_rect(iw, ih, ctx->win->width, ctx->win->height,
             ctx->scale_mode, &ox, &oy, &ow, &oh);

  imlib_context_push(ctx->ctx);
  if (ctx->scaled && (ctx->last_w != ow || ctx->last_h != oh)) {
    imlib_context_set_image(ctx->scaled);
    imlib_free_image();
    ctx->scaled = NULL;
  }
  if (!ctx->scaled) {
    ctx->scaled = imlib_create_cropped_scaled_image(0, 0, iw, ih, ow, oh);
    ctx->last_w = ow;
    ctx->last_h = oh;
  }
  if (!ctx->scaled) {
    imlib_context_pop();
    return;
  }

  imlib_context_set_drawable(ctx->win->win);
  imlib_context_set_image(ctx->scaled);
  imlib_render_image_on_drawable(ox, oy);
  imlib_context_pop();
}

static void redraw(ImageCtx *ctx) {
  if (ctx->scale_mode == XW_SCALE_TILE)
    render_tiled(ctx);
  else
    render_scaled(ctx);
  XFlush(ctx->win->dpy);
}

static int image_start(XWBackend *b) {
  redraw((ImageCtx *)b->priv);
  return 0;
}

static int image_on_event(XWBackend *b, XEvent *ev) {
  ImageCtx *ctx = (ImageCtx *)b->priv;
  if (ev->type == Expose) {
    XExposeEvent *ex = &ev->xexpose;
    if (ex->count == 0)
      redraw(ctx);
    return 1;
  }
  if (ev->type == ConfigureNotify) {
    XConfigureEvent *ce = &ev->xconfigure;
    ctx->win->width = ce->width;
    ctx->win->height = ce->height;
    redraw(ctx);
    return 1;
  }
  return 0;
}

static void image_destroy(XWBackend *b) {
  if (!b || !b->priv)
    return;
  ImageCtx *ctx = (ImageCtx *)b->priv;
  imlib_context_push(ctx->ctx);
  if (ctx->scaled) {
    imlib_context_set_image(ctx->scaled);
    imlib_free_image();
  }
  imlib_context_set_image(ctx->img);
  imlib_free_image();
  imlib_context_pop();
  imlib_context_free(ctx->ctx);
  imlib_set_ximage_cache_size_max(0); /* drop GCs tied to the destroyed window */
  free(ctx);
  b->priv = NULL;
}

XWBackend *xw_image_backend_new(void) {
  static XWBackend backend = {
    .name = "image",
    .init = image_init,
    .start = image_start,
    .stop = NULL,
    .destroy = image_destroy,
    .on_event = image_on_event,
  };
  return &backend;
}
