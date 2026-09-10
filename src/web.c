#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <webkit2/webkit2.h>

#include "backend.h"
#include "x11.h"

typedef struct {
  XWWindow *win;
  GtkWidget *window;  /* top-level GtkWindow holding the webview */
  GtkWidget *view;
  WebKitWebContext *wctx; /* per-wallpaper isolated context (ephemeral) */
  char *path;
} WebCtx;

/* gtk_init runs at first web backend init (process-wide, idempotent) */
static int gtk_initialized = 0;

/* Per-wallpaper ephemeral context: its Web/Network processes die with the
 * wallpaper instead of lingering in the shared default context pool
 * (GPU/DMA-BUF held until process exit). Ephemeral also avoids disk
 * cache/cookie sharing between wallpapers. */
static int web_attach_view(WebCtx *ctx, XWError *err) {
  ctx->wctx = webkit_web_context_new_ephemeral();
  if (!ctx->wctx) {
    XW_ERROR_SET(err, 1, "webkit_web_context_new_ephemeral failed");
    return -1;
  }
  ctx->view = webkit_web_view_new_with_context(ctx->wctx);
  if (!ctx->view) {
    XW_ERROR_SET(err, 1, "webkit_web_view_new_with_context failed");
    g_object_unref(ctx->wctx);
    ctx->wctx = NULL;
    return -1;
  }
  return 0;
}

static void web_terminate(WebCtx *ctx) {
  if (!ctx || !ctx->view)
    return;
  webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(ctx->view));
#if WEBKIT_CHECK_VERSION(2, 34, 0)
  webkit_web_view_terminate_web_process(WEBKIT_WEB_VIEW(ctx->view));
#endif
}

static int web_init(XWBackend *b, XWWindow *win, const XWBackendOpt *opt, XWError *err) {
  if (!gtk_initialized) {
    if (!gtk_init_check(NULL, NULL)) {
      XW_ERROR_SET(err, 1, "gtk_init_check failed");
      return -1;
    }
    gtk_initialized = 1;
  }

  WebCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->win = win;
  ctx->path = strdup(opt->path);
  if (!ctx->path) {
    XW_ERROR_SET(err, 1, "out of memory");
    free(ctx);
    return -1;
  }
  if (web_attach_view(ctx, err) != 0) {
    free(ctx->path);
    free(ctx);
    return -1;
  }

  /* A plain GtkWindow + WebKitWebView renders reliably. We then reparent the
   * window onto the wallpaper X window at the X protocol level (NOT via
   * gdk_window_reparent, which breaks GDK's frame clock and freezes webkit
   * animation/rAF). XReparentWindow keeps GDK's structure intact so the
   * frame clock keeps ticking. */
  ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(ctx->window), win->width, win->height);
  gtk_window_set_decorated(GTK_WINDOW(ctx->window), FALSE);
  gtk_container_add(GTK_CONTAINER(ctx->window), ctx->view);

  /* Realize the window WITHOUT showing it, so we can reparent it onto the
   * wallpaper window before it ever appears as a top-level popup. Then pump
   * GTK until the GdkWindow exists. */
  gtk_widget_realize(ctx->window);
  for (int i = 0; i < 50; i++) {
    g_main_context_iteration(NULL, FALSE);
    if (gtk_widget_get_window(ctx->window))
      break;
    g_usleep(10000);
  }

  GdkWindow *gw = gtk_widget_get_window(ctx->window);
  if (gw) {
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window child = GDK_WINDOW_XID(gw);
    XReparentWindow(dpy, child, win->win, 0, 0);
    XMoveResizeWindow(dpy, child, 0, 0, win->width, win->height);
    XSync(dpy, False);
  }
  gtk_widget_show_all(ctx->window);

  b->priv = ctx;
  return 0;
}

static int web_start(XWBackend *b) {
  WebCtx *ctx = (WebCtx *)b->priv;

  char *uri;
  if (strncmp(ctx->path, "http://", 7) == 0 || strncmp(ctx->path, "https://", 8) == 0) {
    uri = strdup(ctx->path);
  } else {
    char *abs = realpath(ctx->path, NULL);
    if (!abs) {
      return -1;
    }
    uri = g_filename_to_uri(abs, NULL, NULL);
    free(abs);
  }

  if (uri) {
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(ctx->view), uri);
    g_free(uri);
  }
  return 0;
}

static void web_stop(XWBackend *b) {
  WebCtx *ctx = (WebCtx *)b->priv;
  if (ctx && ctx->view)
    webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(ctx->view));
}

/* 同类型复用: 换 URL/页面后重新加载 webkit 视图。 */
static int web_reload(XWBackend *b, const XWBackendOpt *opt, XWError *err) {
  (void)err;
  WebCtx *ctx = (WebCtx *)b->priv;
  if (!ctx)
    return -1;
  free(ctx->path);
  ctx->path = strdup(opt->path);
  if (!ctx->path)
    return -1;
  return web_start(b);
}

static void web_destroy(XWBackend *b) {
  if (!b || !b->priv)
    return;
  WebCtx *ctx = (WebCtx *)b->priv;
  b->priv = NULL;
  web_terminate(ctx);
  if (ctx->window)
    gtk_widget_destroy(ctx->window);
  ctx->window = NULL;
  ctx->view = NULL;
  /* Let the destroy finalize promptly so the reparented X child and the
   * WebView release their GPU/DMA-BUF handles without waiting for the
   * next daemon poll tick. */
  for (int i = 0; i < 5; i++)
    g_main_context_iteration(NULL, FALSE);
  if (ctx->wctx)
    g_object_unref(ctx->wctx);
  free(ctx->path);
  free(ctx);
}

#if defined(XW_HAVE_WAYLAND) && defined(XW_HAVE_GTK_LAYER_SHELL)
static int web_init_wl(XWBackend *b, XWWlWindow *win,
                       const XWBackendOpt *opt, XWError *err);
#endif

XWBackend *xw_web_backend_new(void) {
  static XWBackend backend = {
    .name = "web",
    .uses_gtk = 1,
    .init = web_init,
    .start = web_start,
    .stop = web_stop,
    .destroy = web_destroy,
    .on_event = NULL,
    .reload = web_reload,
#if defined(XW_HAVE_WAYLAND) && defined(XW_HAVE_GTK_LAYER_SHELL)
    /* start/stop/destroy/reload are pure WebKit/GTK ops on the isolated
     * WebCtx, so only init differs per platform. */
    .init_wl = web_init_wl,
    .start_wl = web_start,
    .destroy_wl = web_destroy,
    .reload_wl = web_reload,
#else
    .init_wl = NULL,
    .start_wl = NULL,
    .destroy_wl = NULL,
    .reload_wl = NULL,
#endif
  };
  return &backend;
}

#if defined(XW_HAVE_WAYLAND) && defined(XW_HAVE_GTK_LAYER_SHELL)
#include <gtk-layer-shell/gtk-layer-shell.h>

#include "wl.h"

/* Wayland: the GtkWindow itself becomes the background layer surface via
 * gtk-layer-shell (no XReparent — foreign-window embedding does not exist
 * on Wayland). The output is picked by matching monitor geometry against
 * the resolved rect; -g rects that match nothing stay on the default
 * monitor. WebKit rendering/frame clock work natively on the Wayland
 * GDK backend. */
static int web_init_wl(XWBackend *b, XWWlWindow *win,
                       const XWBackendOpt *opt, XWError *err) {
  if (!gtk_initialized) {
    if (!gtk_init_check(NULL, NULL)) {
      XW_ERROR_SET(err, 1, "gtk_init_check failed");
      return -1;
    }
    gtk_initialized = 1;
  }

  WebCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->path = strdup(opt->path);
  if (!ctx->path) {
    XW_ERROR_SET(err, 1, "out of memory");
    free(ctx);
    return -1;
  }
  if (web_attach_view(ctx, err) != 0) {
    free(ctx->path);
    free(ctx);
    return -1;
  }

  ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(ctx->window),
                              (int)win->width, (int)win->height);
  gtk_window_set_decorated(GTK_WINDOW(ctx->window), FALSE);
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ctx->window), TRUE);
  gtk_window_set_skip_pager_hint(GTK_WINDOW(ctx->window), TRUE);
  gtk_container_add(GTK_CONTAINER(ctx->window), ctx->view);

  gtk_layer_init_for_window(GTK_WINDOW(ctx->window));
  gtk_layer_set_namespace(GTK_WINDOW(ctx->window), "xwallpaper");
  gtk_layer_set_layer(GTK_WINDOW(ctx->window),
                      GTK_LAYER_SHELL_LAYER_BACKGROUND);
  gtk_layer_set_anchor(GTK_WINDOW(ctx->window),
                       GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(ctx->window),
                       GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(ctx->window),
                       GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(ctx->window),
                       GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(ctx->window), -1);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(ctx->window),
                                GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

  GdkDisplay *gdpy = gdk_display_get_default();
  if (gdpy) {
    int n = gdk_display_get_n_monitors(gdpy);
    for (int i = 0; i < n; i++) {
      GdkMonitor *mon = gdk_display_get_monitor(gdpy, i);
      GdkRectangle g;
      gdk_monitor_get_geometry(mon, &g);
      if (g.x == win->x && g.y == win->y &&
          g.width == (int)win->width && g.height == (int)win->height) {
        gtk_layer_set_monitor(GTK_WINDOW(ctx->window), mon);
        break;
      }
    }
  }

  gtk_widget_show_all(ctx->window);
  b->priv = ctx;
  return 0;
}
#endif /* XW_HAVE_WAYLAND && XW_HAVE_GTK_LAYER_SHELL */
