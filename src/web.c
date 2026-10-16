#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <webkit2/webkit2.h>

#include "backend.h"
#include "we_audio.h"
#include "we_pulse.h"
#include "web.h"
#include "web_dir.h"
#include "x11.h"

typedef struct {
  XWWindow *win;
  GtkWidget *window;  /* top-level GtkWindow holding the webview */
  GtkWidget *view;
  WebKitWebContext *wctx; /* per-wallpaper isolated context (ephemeral) */
  char *path;
  char root_dir[1024]; /* dir mode: absolute wallpaper root ("" when unused) */
  char entry[256];     /* dir mode: entry file, always index.html */
  int want_audio;      /* page called wallpaperRegisterAudioListener */
  int dead;            /* web_destroy in progress: ignore late signals */
  int crash_count;     /* terminations inside the current 60s window */
  uint64_t crash_first_ms;
} WebCtx;

/* Process-wide registry of live web wallpapers (main thread only:
 * attach/destroy/message-handler/push all run on the GTK main thread). */
static WebCtx **g_views = NULL;
static int g_nviews = 0;

static void web_reg_add(WebCtx *ctx) {
  WebCtx **v = realloc(g_views, sizeof(*v) * (g_nviews + 1));
  if (!v)
    return;
  g_views = v;
  g_views[g_nviews++] = ctx;
}

static void web_reg_remove(WebCtx *ctx) {
  for (int i = 0; i < g_nviews; i++) {
    if (g_views[i] == ctx) {
      g_views[i] = g_views[g_nviews - 1];
      g_nviews--;
      return;
    }
  }
}

int xw_web_we_want(void) {
  for (int i = 0; i < g_nviews; i++)
    if (g_views[i]->want_audio)
      return 1;
  return 0;
}

void xw_web_we_push(const float bins[128]) {
  if (!bins || !xw_web_we_want())
    return;
  char json[2048];
  if (xw_we_audio_format(bins, json, sizeof(json)) != 0)
    return;
  char *js = g_strdup_printf("__xwAudioPush(%s)", json);
  if (!js)
    return;
  for (int i = 0; i < g_nviews; i++) {
    if (g_views[i]->want_audio && g_views[i]->view)
      webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(g_views[i]->view),
                                           js, -1, NULL, NULL, NULL, NULL, NULL);
  }
  g_free(js);
}

/* 1 while the web backend holds a reference to the shared Pulse monitor */
static int web_pulse_ref;

void xw_web_we_tick(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t now = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
  static uint64_t last = 0;
  if (now - last < 50)
    return;
  last = now;
  if (!xw_web_we_want()) {
    /* Release only the reference this backend holds. The monitor is a
     * process-wide refcount shared with the scene backend, so stopping
     * unconditionally would tear down a capture the scene still needs -
     * xw_we_pulse_running() says nothing about who started it. */
    if (web_pulse_ref) {
      xw_we_pulse_stop();
      web_pulse_ref = 0;
    }
    return;
  }
  if (!web_pulse_ref && xw_we_pulse_start() == 0)
    web_pulse_ref = 1; /* failure just leaves a silent spectrum */
  float bins[128];
  if (xw_we_pulse_get(bins) == 0)
    xw_web_we_push(bins);
}

/* gtk_init runs at first web backend init (process-wide, idempotent) */
static int gtk_initialized = 0;

static gboolean web_block_context_menu(WebKitWebView *view,
                                       WebKitContextMenu *menu,
                                       GdkEvent *event,
                                       WebKitHitTestResult *hit,
                                       gpointer ud) {
  /* A wallpaper is not a browser: never pop the default context menu. */
  (void)view;
  (void)menu;
  (void)event;
  (void)hit;
  (void)ud;
  return TRUE;
}

/* Wallpaper pages are not browsers: no text selection, no HTML5 drag,
 * no grab cursors. Injected as USER-level stylesheet + document-start
 * script so author styles cannot override it and every navigation gets it.
 * Clicks, wheel and mousemove still reach the page (parallax / click
 * effects keep working); only the browser affordances are suppressed. */
static const char *kWallpaperCSS =
    "* { -webkit-user-select: none !important; user-select: none !important; "
    "-webkit-user-drag: none !important; cursor: default !important; }";
static const char *kWallpaperJS =
    "(function(){"
    "document.addEventListener('dragstart',function(e){"
    "e.preventDefault();e.stopPropagation();},true);"
    "document.addEventListener('selectstart',function(e){"
    "e.preventDefault();e.stopPropagation();},true);"
    "})();";

/* Wallpaper Engine audio shim: pages call
 * window.wallpaperRegisterAudioListener(cb) once (outside onload);
 * native pushes 128 floats via __xwAudioPush at ~20Hz. First registration
 * notifies native through the xwAudio message handler so silent/static
 * pages never start the Pulse capture. */
static const char *kAudioJS =
    "(function(){"
    "window.__xwAudioCBs=[];"
    "window.wallpaperRegisterAudioListener=function(cb){"
    "if(typeof cb!=='function')return;"
    "window.__xwAudioCBs.push(cb);"
    "try{window.webkit.messageHandlers.xwAudio.postMessage('on');}catch(e){}"
    "};"
    "window.__xwAudioPush=function(arr){"
    "var c=window.__xwAudioCBs;"
    "for(var i=0;i<c.length;i++){try{c[i](arr);}catch(e){}}"
    "};"
    "})();";

static void web_audio_message_cb(WebKitUserContentManager *ucm,
                                 WebKitJavascriptResult *res, gpointer ud) {
  (void)ucm;
  WebCtx *ctx = (WebCtx *)ud;
  if (res)
    webkit_javascript_result_unref(res);
  if (ctx)
    ctx->want_audio = 1;
}

static void web_apply_wallpaper_policy(WebKitWebView *view, WebCtx *ctx) {
  WebKitUserContentManager *ucm =
      webkit_web_view_get_user_content_manager(view);
  if (!ucm)
    return;
  WebKitUserStyleSheet *sheet = webkit_user_style_sheet_new(
      kWallpaperCSS, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_STYLE_LEVEL_USER, NULL, NULL);
  if (sheet) {
    webkit_user_content_manager_add_style_sheet(ucm, sheet);
    webkit_user_style_sheet_unref(sheet);
  }
  WebKitUserScript *script = webkit_user_script_new(
      kWallpaperJS, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
  if (script) {
    webkit_user_content_manager_add_script(ucm, script);
    webkit_user_script_unref(script);
  }
  WebKitUserScript *audio = webkit_user_script_new(
      kAudioJS, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
  if (audio) {
    webkit_user_content_manager_add_script(ucm, audio);
    webkit_user_script_unref(audio);
  }
  webkit_user_content_manager_register_script_message_handler(ucm, "xwAudio");
  g_signal_connect(ucm, "script-message-received::xwAudio",
                   G_CALLBACK(web_audio_message_cb), ctx);
}

/* Per-wallpaper WebsiteDataManager: storage/cookies/cache land under
 * ~/.local/share|cache/xwallpaper/web/<sha256 of wallpaper root or URL>.
 * Same-wallpaper runs share settings (localStorage/IndexedDB survive daemon
 * restarts); different wallpapers never share. */
static WebKitWebsiteDataManager *web_new_data_manager(WebCtx *ctx) {
  const char *key = ctx->root_dir[0] ? ctx->root_dir : ctx->path;
  char *sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256,
                                            key ? key : "", -1);
  char *data = g_build_filename(g_get_user_data_dir(), "xwallpaper",
                                "web", sum, NULL);
  char *cache = g_build_filename(g_get_user_cache_dir(), "xwallpaper",
                                 "web", sum, NULL);
  g_free(sum);
  g_mkdir_with_parents(data, 0700);
  g_mkdir_with_parents(cache, 0700);
  WebKitWebsiteDataManager *man = webkit_website_data_manager_new(
      "base-data-directory", data, "base-cache-directory", cache, NULL);
  g_free(data);
  g_free(cache);
  return man;
}

/* Wallpaper Engine emulation: WE calls
 * window.wallpaperPropertyListener.applyUserProperties(all properties) on
 * load, and many wallpapers only apply their project.json defaults
 * (background, sizes, colors) inside that callback. Without it such pages
 * render with transparent/missing backgrounds. After load, fetch the
 * sibling project.json (dir mode only) and deliver general.properties,
 * retrying briefly while the page script is still defining the listener. */
static void web_deliver_we_properties(WebKitWebView *view) {
  static const char *kScript =
      "(function(){"
      "var tries=0;"
      "function attempt(){"
      "tries++;"
      "var L=window.wallpaperPropertyListener;"
      "if(!L||typeof L.applyUserProperties!=='function'){"
      "if(tries<50)setTimeout(attempt,200);"
      "return;}"
      "fetch('project.json').then(function(r){return r.json();})"
      ".then(function(p){"
      "if(!p||!p.general||!p.general.properties)return;"
      "try{L.applyUserProperties(p.general.properties);}catch(e){}"
      "}).catch(function(){});"
      "}"
      "attempt();"
      "})();";
  webkit_web_view_evaluate_javascript(view, kScript, -1, NULL, NULL, NULL,
                                       NULL, NULL);
}

static void web_load_changed_cb(WebKitWebView *view,
                                WebKitLoadEvent load_event, gpointer ud) {
  WebCtx *ctx = (WebCtx *)ud;
  if (load_event != WEBKIT_LOAD_FINISHED)
    return;
  if (!ctx->root_dir[0])
    return; /* only local dirs have a sibling project.json */
  web_deliver_we_properties(view);
}

static uint64_t web_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* A dead renderer used to freeze the wallpaper forever (last frame kept
 * on screen, clock stopped, no input). Reload the view instead; stop
 * after repeated crashes so a poison page cannot respawn it in a loop. */
static void web_process_terminated_cb(WebKitWebView *view,
                                      WebKitWebProcessTerminationReason reason,
                                      gpointer ud) {
  WebCtx *ctx = (WebCtx *)ud;
  uint64_t now;
  if (!ctx || ctx->dead || view != WEBKIT_WEB_VIEW(ctx->view))
    return;
  if (reason == WEBKIT_WEB_PROCESS_TERMINATED_BY_API)
    return;
  fprintf(stderr, "xwallpaperd: web process %s, reloading '%s'\n",
          reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT
              ? "ran out of memory"
              : "crashed",
          ctx->path ? ctx->path : "");
  now = web_now_ms();
  if (!ctx->crash_first_ms || now - ctx->crash_first_ms > 60000) {
    ctx->crash_first_ms = now;
    ctx->crash_count = 0;
  }
  if (++ctx->crash_count > 5) {
    fprintf(stderr,
            "xwallpaperd: web process keeps dying, giving up auto-reload\n");
    return;
  }
  webkit_web_view_reload(view);
}

/* GStreamer's OpenAL sink aborts the whole WebProcess when Pulse is
 * unreachable (pa_threaded_mainloop_start assertion); native runs show
 * autoaudiosink falls back to it once pulsesink fails. Take openal out
 * of auto-selection so a Pulse hiccup degrades to another sink instead
 * of killing the page. Inherited by the WebProcess via the environment. */
static void web_pin_audio_sink(void) {
  static int done = 0;
  const char *cur;
  char *merged;
  if (done)
    return;
  done = 1;
  cur = getenv("GST_PLUGIN_FEATURE_RANK");
  if (!cur || !cur[0]) {
    g_setenv("GST_PLUGIN_FEATURE_RANK", "openalsink:NONE", TRUE);
    return;
  }
  if (strstr(cur, "openal"))
    return;
  merged = g_strjoin(",", cur, "openalsink:NONE", NULL);
  if (merged) {
    g_setenv("GST_PLUGIN_FEATURE_RANK", merged, TRUE);
    g_free(merged);
  }
}

/* Per-wallpaper isolated context: each wallpaper gets its own WebKitWebContext
 * (own Web/Network processes, which die with the wallpaper instead of
 * lingering in the shared default pool holding GPU/DMA-BUF) backed by the
 * per-wallpaper data manager above. Disk backing (unlike ephemeral) keeps
 * wallpaper settings across daemon restarts. */
static int web_attach_view(WebCtx *ctx, XWError *err) {
  WebKitWebsiteDataManager *man = web_new_data_manager(ctx);
  if (!man) {
    XW_ERROR_SET(err, 1, "webkit_website_data_manager_new failed");
    return -1;
  }
  ctx->wctx = webkit_web_context_new_with_website_data_manager(man);
  g_object_unref(man);
  if (!ctx->wctx) {
    XW_ERROR_SET(err, 1, "webkit_web_context_new_with_website_data_manager failed");
    return -1;
  }
  ctx->view = webkit_web_view_new_with_context(ctx->wctx);
  if (!ctx->view) {
    XW_ERROR_SET(err, 1, "webkit_web_view_new_with_context failed");
    g_object_unref(ctx->wctx);
    ctx->wctx = NULL;
    return -1;
  }
  /* Local wallpapers load via file:// (native media seeking works, unlike
   * custom schemes). The two file-access switches give static pages
   * http-like subresource loading (fetch/ES modules). */
  WebKitSettings *st = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(ctx->view));
  if (st) {
    webkit_settings_set_allow_file_access_from_file_urls(st, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(st, TRUE);
  }
  g_signal_connect(ctx->view, "context-menu",
                   G_CALLBACK(web_block_context_menu), NULL);
  g_signal_connect(ctx->view, "load-changed",
                   G_CALLBACK(web_load_changed_cb), ctx);
  g_signal_connect(ctx->view, "web-process-terminated",
                   G_CALLBACK(web_process_terminated_cb), ctx);
  web_pin_audio_sink();
  web_apply_wallpaper_policy(WEBKIT_WEB_VIEW(ctx->view), ctx);
  web_reg_add(ctx);
  return 0;
}

/* Classify a --web input. Returns 1 = http(s) URL (load directly),
 * 2 = local dir (load <root>/<entry> via file://),
 * 3 = single .svg file (legacy file://, self-contained),
 * 0 = invalid input (err set). */
static int web_classify(const char *path, char *root, size_t rn,
                        char *entry, size_t en, XWError *err) {
  if (xw_web_is_http_url(path))
    return 1;
  if (xw_web_resolve(path, root, rn, entry, en) == 0)
    return 2;
  /* compat: single self-contained svg keeps the old file:// path */
  struct stat st;
  if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
    const char *dot = strrchr(path, '.');
    if (dot && strcasecmp(dot, ".svg") == 0)
      return 3;
    XW_ERROR_SET(err, 1, "'%.100s' is a file, not a wallpaper directory "
                 "(wrap it in a dir with index.html)", path);
    return 0;
  }
  XW_ERROR_SET(err, 1, "'%.100s' is not a wallpaper directory "
               "(need index.html inside) or http(s) URL", path);
  return 0;
}

/* Validate path and record the resolved form in ctx. Returns the
 * web_classify mode, or 0 on failure (err set). On failure ctx's resolved
 * fields are left cleared so reload() can bail without side effects. */
static int web_store_input(WebCtx *ctx, const char *path, XWError *err) {
  char root[sizeof(ctx->root_dir)], entry[sizeof(ctx->entry)];
  int mode = web_classify(path, root, sizeof(root), entry, sizeof(entry), err);
  if (mode == 0)
    return 0;
  ctx->root_dir[0] = '\0';
  ctx->entry[0] = '\0';
  if (mode == 2) {
    snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", root);
    snprintf(ctx->entry, sizeof(ctx->entry), "%s", entry);
  }
  return mode;
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
  if (web_store_input(ctx, ctx->path, err) == 0) {
    free(ctx->path);
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
  if (ctx->root_dir[0]) {
    /* dir mode: file:// URI of <root>/<entry>; subresources resolve
     * relatively and media seeks natively (custom schemes break media) */
    char full[2048];
    snprintf(full, sizeof(full), "%s/%s", ctx->root_dir, ctx->entry);
    uri = g_filename_to_uri(full, NULL, NULL);
    if (!uri)
      return -1;
  } else if (xw_web_is_http_url(ctx->path)) {
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
  WebCtx *ctx = (WebCtx *)b->priv;
  if (!ctx)
    return -1;
  if (web_store_input(ctx, opt->path, err) == 0)
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
  ctx->dead = 1;
  web_reg_remove(ctx);
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
  if (web_store_input(ctx, ctx->path, err) == 0) {
    free(ctx->path);
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
