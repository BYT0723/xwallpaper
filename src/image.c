#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Imlib2.h>
#include <X11/Xlib.h>

#include "backend.h"
#include "image_cache.h"
#include "x11.h"

typedef struct {
  XWWindow *win;
  Imlib_Context ctx;    /* per-instance context (avoids GC caching across windows) */
  XWImageRef *ref;      /* shared, refcounted decode of img */
  Imlib_Image img;      /* original (owned by ref, never freed here) */
  Imlib_Image scaled;   /* scaled to current window size */
  int scale_mode;
  int last_w, last_h;
  Pixmap pix;           /* window-sized backing, also the window background */
  int pix_w, pix_h;
  GC gc;
} ImageCtx;

/* One cache for the whole daemon: every image window showing the same file
 * shares a single decode. Always used from the main loop thread. */
static XWImageCache *g_cache;
static Imlib_Context g_cache_ctx;

static void *cache_load(void *ud, const char *path) {
  Imlib_Context c = (Imlib_Context)ud;
  Imlib_Image img;
  imlib_context_push(c);
  img = imlib_load_image_immediately_without_cache(path);
  imlib_context_pop();
  return img;
}

static void cache_free(void *ud, void *p) {
  Imlib_Context c = (Imlib_Context)ud;
  imlib_context_push(c);
  imlib_context_set_image((Imlib_Image)p);
  imlib_free_image();
  imlib_context_pop();
}

static XWImageCache *img_cache(void) {
  XWImageCacheOps ops;
  if (g_cache)
    return g_cache;
  g_cache_ctx = imlib_context_new();
  if (!g_cache_ctx)
    return NULL;
  ops.load = cache_load;
  ops.free = cache_free;
  ops.ud = g_cache_ctx;
  g_cache = xw_image_cache_new(&ops);
  return g_cache;
}

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
  imlib_context_pop();

  if (!img_cache()) {
    XW_ERROR_SET(err, 1, "out of memory");
    imlib_context_free(ctx->ctx);
    free(ctx);
    return -1;
  }
  ctx->ref = xw_image_cache_get(g_cache, opt->path);
  ctx->img = xw_image_ref_image(ctx->ref);
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
  imlib_context_set_drawable(ctx->pix);
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
    /* the free above left the context without a current image, and
     * imlib_create_cropped_scaled_image() scales whatever is current:
     * without this the source is NULL and the resize silently no-ops. */
    imlib_context_set_image(ctx->img);
    ctx->scaled = imlib_create_cropped_scaled_image(0, 0, iw, ih, ow, oh);
    ctx->last_w = ow;
    ctx->last_h = oh;
  }
  if (!ctx->scaled) {
    imlib_context_pop();
    return;
  }

  imlib_context_set_drawable(ctx->pix);
  imlib_context_set_image(ctx->scaled);
  imlib_render_image_on_drawable(ox, oy);
  imlib_context_pop();
}

/* Window-sized backing store: it is also installed as the window background,
 * so the X server repaints an exposed area itself and an expose costs the
 * daemon no pixel work. Returns 1 when a fresh (unpainted) pixmap was made. */
static int image_pixmap(ImageCtx *ctx) {
  Display *dpy = ctx->win->dpy;
  int W = ctx->win->width, H = ctx->win->height;
  Pixmap pix;

  if (W <= 0 || H <= 0)
    return 0;
  if (ctx->pix != None && ctx->pix_w == W && ctx->pix_h == H)
    return 0;
  pix = XCreatePixmap(dpy, ctx->win->win, (unsigned int)W, (unsigned int)H,
                      DefaultDepth(dpy, DefaultScreen(dpy)));
  if (pix == None)
    return 0;
  /* install first: the server must never paint from a freed pixmap */
  XSetWindowBackgroundPixmap(dpy, ctx->win->win, pix);
  if (ctx->pix != None)
    XFreePixmap(dpy, ctx->pix);
  ctx->pix = pix;
  ctx->pix_w = W;
  ctx->pix_h = H;
  return 1;
}

static GC image_gc(ImageCtx *ctx) {
  if (!ctx->gc)
    ctx->gc = XCreateGC(ctx->win->dpy, ctx->win->win, 0, NULL);
  return ctx->gc;
}

static void paint(ImageCtx *ctx) {
  Display *dpy = ctx->win->dpy;
  GC gc;
  image_pixmap(ctx);
  if (ctx->pix == None)
    return;
  /* CONTAIN leaves bars; match the black background the window was created
   * with instead of leaving the new pixmap uninitialised */
  gc = image_gc(ctx);
  if (gc) {
    XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
    XFillRectangle(dpy, ctx->pix, gc, 0, 0, (unsigned int)ctx->pix_w,
                   (unsigned int)ctx->pix_h);
  }
  if (ctx->scale_mode == XW_SCALE_TILE)
    render_tiled(ctx);
  else
    render_scaled(ctx);
}

static void redraw(ImageCtx *ctx) {
  paint(ctx);
  XClearWindow(ctx->win->dpy, ctx->win->win);
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
    /* nothing to do while the window background is installed; only a window
     * without a pixmap (never painted, or create failed) needs a client draw */
    if (ctx->pix == None && ex->count == 0)
      redraw(ctx);
    return 1;
  }
  if (ev->type == ConfigureNotify) {
    XConfigureEvent *ce = &ev->xconfigure;
    ctx->win->width = ce->width;
    ctx->win->height = ce->height;
    /* a move also reports ConfigureNotify: repaint only a real size change */
    if (ce->width != ctx->pix_w || ce->height != ctx->pix_h)
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
  /* imlib_context_free() frees whatever image the context currently holds;
   * the original belongs to the cache, so drop the pointer first or the
   * context and the cache both free it. */
  imlib_context_set_image(NULL);
  imlib_context_pop();
  /* the original stays decoded only while some window still shows it */
  xw_image_cache_put(g_cache, ctx->ref);
  ctx->ref = NULL;
  if (ctx->gc) {
    XFreeGC(ctx->win->dpy, ctx->gc);
    ctx->gc = NULL;
  }
  if (ctx->pix != None) {
    XSetWindowBackgroundPixmap(ctx->win->dpy, ctx->win->win, None);
    XFreePixmap(ctx->win->dpy, ctx->pix);
    ctx->pix = None;
  }
  imlib_set_ximage_cache_size_max(0); /* drop GCs tied to the destroyed window */
  imlib_context_free(ctx->ctx);
  free(ctx);
  b->priv = NULL;
}

/* 同类型复用: 加载新图替换旧图 (失败保留旧壁纸), 再重绘。 */
static int image_reload(XWBackend *b, const XWBackendOpt *opt, XWError *err) {
  ImageCtx *ctx = (ImageCtx *)b->priv;
  XWImageRef *ref;
  Imlib_Image img;
  if (!ctx)
    return -1;
  if (!img_cache()) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  /* acquire first: re-setting the same file then hits the cache and keeps
   * the decode (and, with it, the already scaled copy) untouched */
  ref = xw_image_cache_get(g_cache, opt->path);
  img = xw_image_ref_image(ref);
  if (!img) {
    XW_ERROR_SET(err, 1, "cannot load image '%s'", opt->path);
    return -1;
  }
  if (img != ctx->img || ctx->scale_mode != opt->scale_mode) {
    imlib_context_push(ctx->ctx);
    if (ctx->scaled) {
      imlib_context_set_image(ctx->scaled);
      imlib_free_image();
      ctx->scaled = NULL;
    }
    imlib_context_pop();
  }
  xw_image_cache_put(g_cache, ctx->ref);
  ctx->ref = ref;
  ctx->img = img;
  ctx->scale_mode = opt->scale_mode;
  redraw(ctx);
  return 0;
}

#ifdef XW_HAVE_WAYLAND
static int image_init_wl(XWBackend *b, XWWlWindow *win,
                         const XWBackendOpt *opt, XWError *err);
static int image_start_wl(XWBackend *b);
static void image_destroy_wl(XWBackend *b);
static int image_reload_wl(XWBackend *b, const XWBackendOpt *opt,
                           XWError *err);
#endif

XWBackend *xw_image_backend_new(void) {
  static XWBackend backend = {
    .name = "image",
    .init = image_init,
    .start = image_start,
    .stop = NULL,
    .destroy = image_destroy,
    .on_event = image_on_event,
    .reload = image_reload,
#ifdef XW_HAVE_WAYLAND
    .init_wl = image_init_wl,
    .reload_wl = image_reload_wl,
    .start_wl = image_start_wl,
    .destroy_wl = image_destroy_wl,
#else
    .init_wl = NULL,
    .reload_wl = NULL,
    .start_wl = NULL,
    .destroy_wl = NULL,
#endif
  };
  return &backend;
}

#ifdef XW_HAVE_WAYLAND
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wl.h"

typedef struct {
  XWWlWindow *win;
  Imlib_Context ctx;
  Imlib_Image img;
  int scale_mode;
  int span;
  unsigned int span_w, span_h;
  int span_ox, span_oy;
  int fd;
  void *data;
  size_t size;
  struct wl_shm_pool *pool;
  struct wl_buffer *buf;
} ImageWlCtx;

static void wl_opt_span(ImageWlCtx *c, const XWBackendOpt *opt) {
  c->scale_mode = opt->scale_mode;
  c->span = opt->span;
  c->span_w = opt->span_w;
  c->span_h = opt->span_h;
  c->span_ox = opt->span_ox;
  c->span_oy = opt->span_oy;
}

static int wl_shm_alloc(unsigned int w, unsigned int h,
                        int *fd, void **data, size_t *size) {
  size_t stride = (size_t)w * 4;
  *size = stride * h;
  *fd = memfd_create("xwallpaper", MFD_CLOEXEC);
  if (*fd < 0) {
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s/xwallpaper-XXXXXX",
             rt && rt[0] ? rt : "/tmp");
    *fd = mkstemp(tmp);
    if (*fd < 0)
      return -1;
    unlink(tmp);
    fcntl(*fd, F_SETFD, FD_CLOEXEC);
  }
  if (ftruncate(*fd, (off_t)*size) < 0) {
    close(*fd);
    return -1;
  }
  *data = mmap(NULL, *size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
  if (*data == MAP_FAILED) {
    close(*fd);
    return -1;
  }
  return 0;
}

static void wl_shm_free(ImageWlCtx *c) {
  if (c->buf) {
    wl_buffer_destroy(c->buf);
    c->buf = NULL;
  }
  if (c->pool) {
    wl_shm_pool_destroy(c->pool);
    c->pool = NULL;
  }
  if (c->data) {
    munmap(c->data, c->size);
    c->data = NULL;
  }
  if (c->fd >= 0) {
    close(c->fd);
    c->fd = -1;
  }
}

static void img_data_size(ImageWlCtx *c, int *w, int *h) {
  imlib_context_push(c->ctx);
  imlib_context_set_image(c->img);
  *w = imlib_image_get_width();
  *h = imlib_image_get_height();
  imlib_context_pop();
}

/* Paint the loaded image into the shm buffer with X11-identical scale
 * semantics (scale_rect shared). Pixels forced opaque: imlib data may
 * carry alpha 0 for images without an alpha channel. */
/* Span piece paint. Piece (W,H) at span offset (span_ox,span_oy):
 * piece pixel (dc,r) == span pixel (span_ox+dc, span_oy+r) ==
 * scaled-image pixel (span_ox+dc-ox, span_oy+r-oy). */
static void image_wl_paint_span(ImageWlCtx *c, int iw, int ih,
                                uint32_t *px, int W, int H) {
  int ox = 0, oy = 0, ow = 0, oh = 0;

  imlib_context_push(c->ctx);
  if (c->scale_mode == XW_SCALE_TILE) {
    imlib_context_set_image(c->img);
    DATA32 *src = imlib_image_get_data_for_reading_only();
    int phx = ((c->span_ox % iw) + iw) % iw;
    int phy = ((c->span_oy % ih) + ih) % ih;
    for (int y = -phy; y < H; y += ih) {
      int r0 = y < 0 ? -y : 0;
      int rows = ih - r0 < H - y - r0 ? ih - r0 : H - y - r0;
      if (rows <= 0)
        continue;
      for (int x = -phx; x < W; x += iw) {
        int c0 = x < 0 ? -x : 0;
        int cols = iw - c0 < W - x - c0 ? iw - c0 : W - x - c0;
        if (cols <= 0)
          continue;
        for (int r = 0; r < rows; r++) {
          uint32_t *dst = px + (y + r0 + r) * W + (x + c0);
          const uint32_t *s =
              (const uint32_t *)src + (r0 + r) * iw + c0;
          for (int col = 0; col < cols; col++)
            dst[col] = s[col] | 0xFF000000;
        }
      }
    }
    imlib_context_pop();
    return;
  }

  scale_rect(iw, ih, (int)c->span_w, (int)c->span_h, c->scale_mode,
             &ox, &oy, &ow, &oh);
  if (ow <= 0 || oh <= 0) {
    imlib_context_pop();
    return;
  }
  Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, iw, ih, ow, oh);
  if (!scaled) {
    imlib_context_pop();
    return;
  }
  imlib_context_set_image(scaled);
  DATA32 *src = imlib_image_get_data_for_reading_only();
  /* dst col dc needs src col ox'-relative sc = span_ox + dc - ox */
  int dc0 = ox - c->span_ox;
  if (dc0 < 0)
    dc0 = 0;
  int dc1 = ox + ow - c->span_ox;
  if (dc1 > W)
    dc1 = W;
  int dr0 = oy - c->span_oy;
  if (dr0 < 0)
    dr0 = 0;
  int dr1 = oy + oh - c->span_oy;
  if (dr1 > H)
    dr1 = H;
  for (int r = dr0; r < dr1; r++) {
    uint32_t *dst = px + r * W + dc0;
    const uint32_t *s =
        (const uint32_t *)src + (c->span_oy + r - oy) * ow +
        (c->span_ox + dc0 - ox);
    for (int col = dc0; col < dc1; col++)
      *dst++ = *s++ | 0xFF000000;
  }
  imlib_context_set_image(scaled);
  imlib_free_image();
  imlib_context_pop();
}

static void image_wl_paint(ImageWlCtx *c) {
  int W = (int)c->win->width;
  int H = (int)c->win->height;
  uint32_t *px = c->data;
  for (int i = 0; i < W * H; i++)
    px[i] = 0xFF000000;

  int iw, ih;
  img_data_size(c, &iw, &ih);
  if (iw <= 0 || ih <= 0)
    return;

  /* span piece: scale against the whole span rect, then blit the piece
   * sub-rect (tiles keep phase so the pattern continues across outputs) */
  if (c->span && c->span_w > 0 && c->span_h > 0) {
    image_wl_paint_span(c, iw, ih, px, W, H);
    return;
  }

  int ox = 0, oy = 0, ow = 0, oh = 0;
  scale_rect(iw, ih, W, H, c->scale_mode, &ox, &oy, &ow, &oh);
  if (ow <= 0 || oh <= 0)
    return;

  imlib_context_push(c->ctx);
  if (c->scale_mode == XW_SCALE_TILE) {
    imlib_context_set_image(c->img);
    DATA32 *src = imlib_image_get_data_for_reading_only();
    for (int y = 0; y < H; y += ih) {
      int rows = ih < H - y ? ih : H - y;
      for (int x = 0; x < W; x += iw) {
        int cols = iw < W - x ? iw : W - x;
        for (int r = 0; r < rows; r++) {
          uint32_t *dst = px + (y + r) * W + x;
          const uint32_t *s = (const uint32_t *)src + r * iw;
          for (int col = 0; col < cols; col++)
            dst[col] = s[col] | 0xFF000000;
        }
      }
    }
  } else {
    Imlib_Image scaled =
        imlib_create_cropped_scaled_image(0, 0, iw, ih, ow, oh);
    if (scaled) {
      imlib_context_set_image(scaled);
      DATA32 *src = imlib_image_get_data_for_reading_only();
      /* FILL overflows the window (ox/oy negative): clip to the buffer.
       * X11 gets this clipping from the server; shm memcpy must do it. */
      int r0 = oy < 0 ? -oy : 0;
      int r1 = oy + oh > H ? H - oy : oh;
      int c0 = ox < 0 ? -ox : 0;
      int c1 = ox + ow > W ? W - ox : ow;
      for (int r = r0; r < r1; r++) {
        uint32_t *dst = px + (oy + r) * W + (ox + c0);
        const uint32_t *s = (const uint32_t *)src + r * ow + c0;
        for (int col = c0; col < c1; col++)
          *dst++ = *s++ | 0xFF000000;
      }
      imlib_context_set_image(scaled);
      imlib_free_image();
    }
  }
  imlib_context_pop();
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
  (void)data; (void)time;
  wl_callback_destroy(cb);
}

static const struct wl_callback_listener frame_listener = { frame_done };

static void image_wl_attach(ImageWlCtx *c) {
  struct wl_surface *surf = (struct wl_surface *)c->win->surface;
  wl_surface_attach(surf, c->buf, 0, 0);
  wl_surface_damage_buffer(surf, 0, 0, c->win->width, c->win->height);
  struct wl_callback *cb = wl_surface_frame(surf);
  wl_callback_add_listener(cb, &frame_listener, NULL);
  wl_surface_commit(surf);
}

static int image_init_wl(XWBackend *b, XWWlWindow *win, const XWBackendOpt *opt,
                         XWError *err) {
  if (win->width == 0 || win->height == 0) {
    XW_ERROR_SET(err, 1, "invalid window size");
    return -1;
  }
  ImageWlCtx *c = calloc(1, sizeof(*c));
  if (!c) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  c->win = win;
  wl_opt_span(c, opt);
  c->fd = -1;

  c->ctx = imlib_context_new();
  if (!c->ctx) {
    XW_ERROR_SET(err, 1, "imlib_context_new failed");
    free(c);
    return -1;
  }
  imlib_context_push(c->ctx);
  c->img = imlib_load_image_immediately_without_cache(opt->path);
  imlib_context_pop();
  if (!c->img) {
    XW_ERROR_SET(err, 1, "cannot load image '%s'", opt->path);
    imlib_context_free(c->ctx);
    free(c);
    return -1;
  }

  struct wl_shm *shm = xw_wl_get_shm((xw_wl_ctx *)win->wl_ctx);
  if (!shm) {
    XW_ERROR_SET(err, 1, "wayland shm not available");
    imlib_context_push(c->ctx);
    imlib_context_set_image(c->img);
    imlib_free_image();
    imlib_context_pop();
    imlib_context_free(c->ctx);
    free(c);
    return -1;
  }
  if (wl_shm_alloc(win->width, win->height, &c->fd, &c->data,
                   &c->size) != 0) {
    XW_ERROR_SET(err, 1, "cannot allocate shm buffer");
    imlib_context_push(c->ctx);
    imlib_context_set_image(c->img);
    imlib_free_image();
    imlib_context_pop();
    imlib_context_free(c->ctx);
    free(c);
    return -1;
  }
  c->pool = wl_shm_create_pool(shm, c->fd, (int32_t)c->size);
  c->buf = wl_shm_pool_create_buffer(c->pool, 0, (int32_t)win->width,
                                     (int32_t)win->height,
                                     (int32_t)win->width * 4,
                                     WL_SHM_FORMAT_ARGB8888);
  b->priv = c;
  return 0;
}

static int image_start_wl(XWBackend *b) {
  ImageWlCtx *c = (ImageWlCtx *)b->priv;
  image_wl_paint(c);
  image_wl_attach(c);
  return 0;
}

static void image_destroy_wl(XWBackend *b) {
  if (!b || !b->priv)
    return;
  ImageWlCtx *c = (ImageWlCtx *)b->priv;
  wl_shm_free(c);
  imlib_context_push(c->ctx);
  if (c->img) {
    imlib_context_set_image(c->img);
    imlib_free_image();
  }
  imlib_context_pop();
  imlib_context_free(c->ctx);
  free(c);
  b->priv = NULL;
}

static int image_reload_wl(XWBackend *b, const XWBackendOpt *opt,
                           XWError *err) {
  ImageWlCtx *c = (ImageWlCtx *)b->priv;
  if (!c)
    return -1;
  imlib_context_push(c->ctx);
  Imlib_Image img = imlib_load_image_immediately_without_cache(opt->path);
  if (img) {
    if (c->img) {
      imlib_context_set_image(c->img);
      imlib_free_image();
    }
    c->img = img;
    wl_opt_span(c, opt);
  }
  imlib_context_pop();
  if (!c->img) {
    XW_ERROR_SET(err, 1, "cannot load image '%s'", opt->path);
    return -1;
  }
  image_wl_paint(c);
  image_wl_attach(c);
  return 0;
}
#endif /* XW_HAVE_WAYLAND */
