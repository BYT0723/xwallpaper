#include <locale.h>
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
#include "keybinds.h"
#include "we_audio.h"
#include "we_pulse.h"
#include "web.h"
#include "web_dir.h"
#include "web_props.h"
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
  /* runtime audio control, kept here so it survives page navigations */
  double ctl_volume;   /* 0..100, the master level the user asked for */
  int ctl_muted;
  int ctl_paused;
  int ctl_started;     /* 0 = leave the page's own mixing alone */
  int dpms_paused;     /* DPMS power-off freeze, independent of ctl_paused */
  /* WE user properties from <root>/project.json (dir mode only) plus
   * runtime overrides set via --set-property (replayed after each load). */
  XWWebProp *props;
  int n_props;
  XWWebProp *over;
  int n_over;
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
    if (g_views[i]->want_audio && g_views[i]->view &&
        !g_views[i]->dpms_paused)
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

/* gtk_init_check() runs setlocale(LC_ALL, "") even when it fails, dropping the
 * process into the environment's numeric locale: mpv_create() then returns NULL
 * for anything but "C"/"C.UTF-8" (no video wallpaper) and the daemon's own
 * %g/atof output would use ",". Restore the C numeric locale on both paths. */
static int web_gtk_ensure(XWError *err) {
  if (gtk_initialized)
    return 0;
  int ok = gtk_init_check(NULL, NULL);
  setlocale(LC_NUMERIC, "C");
  if (!ok) {
    XW_ERROR_SET(err, 1, "gtk_init_check failed");
    return -1;
  }
  gtk_initialized = 1;
  return 0;
}

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

/* Runtime audio control. WebKitGTK has no volume API, so the page is driven
 * from inside: every media element is held at the master level, including
 * ones created later, and Web Audio graphs are rerouted through a master
 * GainNode (pages using AudioContext ignore element volume entirely). The
 * level starts unset so a page's own mixing is untouched until the user
 * actually turns the volume. The daemon re-injects this on every navigation
 * and re-applies the level once the load finishes. */
static const char *kAudioCtlJS =
    "(function(){"
    "if(window.__xwCtl)return;"
    /* master 1 = leave the page's own level alone, 0 = silent. */
    "var st=window.__xwCtl={master:1,muted:false,started:false,els:[],gains:[]};"
    "var vd=null;"
    "try{vd=Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype,'volume');}"
    "catch(x){}"
    "function eff(v){return st.muted?0:v*st.master;}"
    "function track(e){if(e&&!e.__xwT){e.__xwT=1;st.els.push(e);}}"
    /* Compose rather than assign: __xwVol holds the level the page asked
     * for, so a page that keeps re-syncing its own BGM level (which is what
     * WE web wallpapers do) cannot undo the master. */
    "function apply(e){"
    "if(!e)return;"
    "track(e);"
    "try{"
    "var b=e.__xwVol;"
    "if(b===undefined){b=vd?vd.get.call(e):e.volume;e.__xwVol=b;}"
    "if(vd)vd.set.call(e,eff(b));else e.volume=eff(b);"
    "}catch(x){}"
    "}"
    "function all(){"
    "var n=document.querySelectorAll('audio,video');"
    "for(var i=0;i<n.length;i++)"
    "if(!n[i].__xwT)apply(n[i]);"
    "}"
    "function sync(){"
    "if(!st.started)return;"
    "all();"
    /* detached elements (new Audio()) are invisible to the DOM, so the
     * tracked list is the only way master reaches them at all */
    "for(var i=0;i<st.els.length;i++)apply(st.els[i]);"
    "var g=st.muted?0:st.master;"
    "for(var i=0;i<st.gains.length;i++)"
    "try{st.gains[i].gain.value=g;}catch(x){}"
    "}"
    "window.__xwCtlRaw=function(e){"
    "try{return vd?vd.get.call(e):e.volume;}catch(x){return -1;}};"
    "window.__xwCtlApply=function(master,muted){"
    "st.master=master;st.muted=!!muted;st.started=true;sync();};"
    "window.__xwCtlMedia=function(what,arg){"
    "function doit(e){"
    "try{"
    "if(what==='pause')e.pause();"
    "else if(what==='play'){var p=e.play();"
    "if(p&&p.catch)p.catch(function(){});}"
    "else if(what==='seek')e.currentTime=Math.max(0,e.currentTime+arg);"
    "}catch(x){}"
    "}"
    "var n=document.querySelectorAll('audio,video');"
    "for(var i=0;i<n.length;i++)doit(n[i]);"
    "for(var i=0;i<st.els.length;i++)doit(st.els[i]);"
    "};"
    /* Everything above only holds if the page's own writes go through it. */
    "try{"
    "if(vd&&vd.configurable){"
    "Object.defineProperty(HTMLMediaElement.prototype,'volume',{"
    "configurable:true,enumerable:vd.enumerable,"
    "get:function(){"
    "return this.__xwVol===undefined?vd.get.call(this):this.__xwVol;},"
    "set:function(v){this.__xwVol=v;track(this);vd.set.call(this,eff(v));}"
    "});}"
    "}catch(x){}"
    "try{new MutationObserver(function(m){"
    "for(var i=0;i<m.length;i++){var a=m[i].addedNodes;"
    "for(var j=0;j<a.length;j++){var n=a[j];"
    "if(n.tagName==='AUDIO'||n.tagName==='VIDEO')apply(n);"
    "if(n.querySelectorAll){var d=n.querySelectorAll('audio,video');"
    "for(var k=0;k<d.length;k++)apply(d[k]);}}}"
    "}).observe(document.documentElement||document,"
    "{childList:true,subtree:true});}catch(x){}"
    "try{var op=HTMLMediaElement.prototype.play;"
    "HTMLMediaElement.prototype.play=function(){"
    "track(this);"
    "if(st.started)apply(this);"
    "return op.apply(this,arguments);};}catch(x){}"
    /* Web Audio: pages using an AudioContext ignore element volume entirely */
    "try{if(typeof AudioNode!=='undefined'&&window.AudioDestinationNode){"
    "var connect=AudioNode.prototype.connect;"
    "AudioNode.prototype.connect=function(dest){"
    "try{"
    "if(!this.__xwM&&dest instanceof AudioDestinationNode){"
    "var c=this.context,g=c.createGain();"
    "g.__xwM=true;g.gain.value=(st.muted?0:st.master);"
    "st.gains.push(g);"
    "connect.call(g,dest);"
    "return connect.call(this,g);}"
    "}catch(x){}"
    "return connect.apply(this,arguments);};}}catch(x){}"
    "})();";

static void web_eval(WebCtx *ctx, const char *js) {
  if (!ctx || !ctx->view || !js)
    return;
  webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(ctx->view), js, -1, NULL,
                                      NULL, NULL, NULL, NULL);
}

/* Push the daemon's stored level into the page (after a control change, and
 * again after every navigation). */
static void web_push_audio_ctl(WebCtx *ctx) {
  char js[192];
  if (!ctx || !ctx->view || !ctx->ctl_started)
    return;
  snprintf(js, sizeof(js),
           "window.__xwCtlApply&&window.__xwCtlApply(%g,%d);",
           ctx->ctl_volume / 100.0, ctx->ctl_muted ? 1 : 0);
  web_eval(ctx, js);
  if (ctx->ctl_paused)
    web_eval(ctx, "window.__xwCtlMedia&&window.__xwCtlMedia('pause');");
}

/* Wallpaper Engine user properties: <root>/project.json general.properties
 * parsed once per wallpaper (dir mode only). Overrides from --set-property
 * shadow the parsed values for list and are replayed after every load. */
#define XW_WEB_PROPS_CAP 256

static void web_free_parsed(WebCtx *ctx) {
  if (!ctx)
    return;
  free(ctx->props);
  ctx->props = NULL;
  ctx->n_props = 0;
}

/* Drop every runtime override (wallpaper switch: old values must not leak
 * into the new wallpaper even when a property name collides). */
static void web_clear_overrides(WebCtx *ctx) {
  if (!ctx)
    return;
  free(ctx->over);
  ctx->over = NULL;
  ctx->n_over = 0;
}

static void web_free_props(WebCtx *ctx) {
  if (!ctx)
    return;
  web_free_parsed(ctx);
  web_clear_overrides(ctx);
}

static void web_load_props(WebCtx *ctx) {
  char path[2048];
  FILE *f;
  long sz;
  char *buf;
  size_t nr;
  XWWebProp *tmp;
  int n;

  if (!ctx)
    return;
  /* Re-read the defaults from disk but keep runtime overrides; the caller
   * (web_reload) drops/prunes them on a wallpaper switch. */
  web_free_parsed(ctx);
  if (!ctx->root_dir[0])
    return;
  snprintf(path, sizeof(path), "%s/project.json", ctx->root_dir);
  f = fopen(path, "rb");
  if (!f)
    return;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (1 << 20)) {
    fclose(f);
    return;
  }
  buf = malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return;
  }
  nr = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (nr != (size_t)sz) {
    free(buf);
    return;
  }
  tmp = malloc(sizeof(*tmp) * XW_WEB_PROPS_CAP);
  if (!tmp) {
    free(buf);
    return;
  }
  n = xw_web_props_parse(buf, nr, tmp, XW_WEB_PROPS_CAP);
  free(buf);
  if (n <= 0) {
    free(tmp);
    return;
  }
  ctx->props = tmp;
  ctx->n_props = n;
}

static XWWebProp *web_find_prop(XWWebProp *arr, int n, const char *name) {
  int i;
  for (i = 0; i < n; i++)
    if (strcmp(arr[i].name, name) == 0)
      return &arr[i];
  return NULL;
}

/* Drop overrides with no matching parsed property (project.json edited, or
 * the same wallpaper re-read with fewer properties). */
static void web_prune_overrides(WebCtx *ctx) {
  int i;
  if (!ctx || !ctx->over || ctx->n_over <= 0)
    return;
  for (i = 0; i < ctx->n_over;) {
    if (web_find_prop(ctx->props, ctx->n_props, ctx->over[i].name))
      i++;
    else {
      ctx->over[i] = ctx->over[ctx->n_over - 1];
      ctx->n_over--;
    }
  }
  if (ctx->n_over == 0) {
    free(ctx->over);
    ctx->over = NULL;
  }
}

static int web_list_props(XWBackend *b, char *out, size_t n) {
  WebCtx *ctx = (b && b->priv) ? (WebCtx *)b->priv : NULL;
  size_t off = 0;
  int i, count = 0;

  if (!ctx || !out || n == 0 || ctx->n_props <= 0)
    return -1;
  for (i = 0; i < ctx->n_props; i++) {
    const XWWebProp *p = &ctx->props[i];
    const XWWebProp *o = web_find_prop(ctx->over, ctx->n_over, p->name);
    int w;
    if (o)
      p = o;
    if (strchr(p->name, '\t') || strchr(p->name, '\n') ||
        strchr(p->value, '\n'))
      continue;
    w = snprintf(out + off, n - off, "%s\t%s\t%s\n", p->name, p->type,
                 p->value);
    if (w < 0 || (size_t)w >= n - off)
      return -1;
    off += (size_t)w;
    count++;
  }
  return count;
}

static int web_set_prop(XWBackend *b, const char *name, const char *value,
                        XWError *err) {
  WebCtx *ctx = (b && b->priv) ? (WebCtx *)b->priv : NULL;
  const XWWebProp *known;
  XWWebProp *slot = NULL;
  char js[XW_WEB_PROP_JS_MAX + 128];
  char norm[XW_WEB_PROP_VALUE_MAX];

  if (!ctx || !name || !name[0] || !value) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  known = web_find_prop(ctx->props, ctx->n_props, name);
  if (!known) {
    XW_ERROR_SET(err, 1, "unknown property '%.64s'", name);
    return -1;
  }
  /* color props also take #rrggbb/#rgb; garbage fails here instead of
   * being silently ignored by the page. */
  if (xw_web_props_normalize(known->type, value, norm, sizeof(norm)) != 0) {
    XW_ERROR_SET(err, 1, "invalid value for '%.64s'", name);
    return -1;
  }
  value = norm;
  slot = web_find_prop(ctx->over, ctx->n_over, name);
  if (!slot) {
    XWWebProp *grown;
    if (ctx->n_over >= XW_WEB_PROPS_CAP) {
      XW_ERROR_SET(err, 1, "too many property overrides");
      return -1;
    }
    grown = realloc(ctx->over, sizeof(*grown) * (size_t)(ctx->n_over + 1));
    if (!grown) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    ctx->over = grown;
    slot = &ctx->over[ctx->n_over++];
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    snprintf(slot->type, sizeof(slot->type), "%s", known->type);
  }
  snprintf(slot->value, sizeof(slot->value), "%s", value);
  /* Best effort: a page that has not finished loading (or navigated away)
   * misses this push, but the cached override is replayed on LOAD_FINISHED. */
  if (xw_web_props_apply_js(slot, 1, js, sizeof(js)) == 0)
    web_eval(ctx, js);
  return 0;
}

static int web_control(XWBackend *b, const char *name, const char *value,
                       XWError *err) {
  WebCtx *ctx = (b && b->priv) ? (WebCtx *)b->priv : NULL;
  char js[192];
  if (!ctx || !ctx->view || !name) {
    XW_ERROR_SET(err, 1, "web: not running");
    return -1;
  }
  if (strcmp(name, "volume") == 0) {
    int v = 0;
    if (xw_audio_apply((int)(ctx->ctl_volume + 0.5), value, &v) != 0) {
      XW_ERROR_SET(err, 1, XW_VOLUME_HINT);
      return -1;
    }
    ctx->ctl_volume = v;
    ctx->ctl_muted = 0;
    ctx->ctl_started = 1;
    web_push_audio_ctl(ctx);
    return 0;
  }
  if (strcmp(name, "mute") == 0 || strcmp(name, "pause") == 0) {
    int *flag = strcmp(name, "mute") == 0 ? &ctx->ctl_muted : &ctx->ctl_paused;
    int want;
    if (xw_ctl_toggle(*flag, value, &want) != 0) {
      XW_ERROR_SET(err, 1, "%s takes on, off or toggle", name);
      return -1;
    }
    *flag = want;
    if (strcmp(name, "mute") == 0) {
      ctx->ctl_started = 1;
      web_push_audio_ctl(ctx);
    } else if (!ctx->dpms_paused) {
      /* DPMS-frozen pages stay paused regardless; a play here only clears
       * the user flag and is replayed on DPMS resume. */
      snprintf(js, sizeof(js),
               "window.__xwCtlMedia&&window.__xwCtlMedia('%s');",
               want ? "pause" : "play");
      web_eval(ctx, js);
    }
    return 0;
  }
  if (strcmp(name, "seek") == 0) {
    if (!value || !value[0]) {
      XW_ERROR_SET(err, 1, "seek needs seconds");
      return -1;
    }
    snprintf(js, sizeof(js),
             "window.__xwCtlMedia&&window.__xwCtlMedia('seek',%g);",
             atof(value));
    web_eval(ctx, js);
    return 0;
  }
  XW_ERROR_SET(err, 1, "web does not support '%s'", name);
  return -1;
}

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
  WebKitUserScript *ctl = webkit_user_script_new(
      kAudioCtlJS, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
  if (ctl) {
    webkit_user_content_manager_add_script(ucm, ctl);
    webkit_user_script_unref(ctl);
  }
  webkit_user_content_manager_register_script_message_handler(ucm, "xwAudio");  g_signal_connect(ucm, "script-message-received::xwAudio",
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
 * retrying briefly while the page script is still defining the listener.
 * Cached --set-property overrides are applied right after the defaults so
 * they survive reloads and page navigations. */
static void web_deliver_we_properties(WebKitWebView *view, WebCtx *ctx) {
  static const char *kTpl =
      "(function(){"
      "var O=%s;"
      "var tries=0;"
      "function attempt(){"
      "tries++;"
      "var L=window.wallpaperPropertyListener;"
      "if(!L||typeof L.applyUserProperties!=='function'){"
      "if(tries<50)setTimeout(attempt,200);"
      "return;}"
      "fetch('project.json').then(function(r){return r.json();})"
      ".then(function(p){"
      "if(p&&p.general&&p.general.properties){"
      "try{L.applyUserProperties(p.general.properties);}catch(e){}}"
      "if(O){try{L.applyUserProperties(O);}catch(e){}}"
      "}).catch(function(){"
      "if(O){try{L.applyUserProperties(O);}catch(e){}}});"
      "}"
      "attempt();"
      "})();";
  char *over = NULL;
  char *script;

  if (ctx && ctx->n_over > 0) {
    size_t need = (size_t)ctx->n_over * XW_WEB_PROP_JS_MAX + 64;
    over = malloc(need);
    if (over &&
        xw_web_props_object_js(ctx->over, ctx->n_over, over, need) != 0) {
      free(over);
      over = NULL;
    }
  }
  script = g_strdup_printf(kTpl, over ? over : "null");
  free(over);
  if (!script)
    return;
  webkit_web_view_evaluate_javascript(view, script, -1, NULL, NULL, NULL,
                                       NULL, NULL);
  g_free(script);
}

static void web_load_changed_cb(WebKitWebView *view,
                                WebKitLoadEvent load_event, gpointer ud) {
  WebCtx *ctx = (WebCtx *)ud;
  if (load_event != WEBKIT_LOAD_FINISHED)
    return;
  /* the shim is re-injected per document, so re-apply the stored level */
  web_push_audio_ctl(ctx);
  if (!ctx->root_dir[0])
    return; /* only local dirs have a sibling project.json */
  web_deliver_we_properties(view, ctx);
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
  if (mode != 2)
    web_free_parsed(ctx);
  ctx->root_dir[0] = '\0';
  ctx->entry[0] = '\0';
  if (mode == 2) {
    snprintf(ctx->root_dir, sizeof(ctx->root_dir), "%s", root);
    snprintf(ctx->entry, sizeof(ctx->entry), "%s", entry);
    web_load_props(ctx);
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

/* The WebKit view is a nested X window sitting under the pointer. It selects
 * key events for in-page input, so a KeyPress aimed at the wallpaper is
 * consumed there and never reaches the daemon's own wallpaper window - the one
 * whose KeyPressMask drives wallpaper.keys. Wallpapers have no use for page
 * keyboard input, so drop the key masks on the nested windows and let X
 * propagate the KeyPress up to the wallpaper window, exactly like video. */
static void web_drop_key_mask(GtkWidget *w) {
  GdkWindow *g = w ? gtk_widget_get_window(w) : NULL;
  if (g)
    gdk_window_set_events(g, gdk_window_get_events(g) &
                              ~(GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK));
}

static gboolean web_view_map_cb(GtkWidget *w, GdkEvent *ev, gpointer ud) {
  (void)ev;
  (void)ud;
  web_drop_key_mask(w);
  return FALSE;
}

static int web_init(XWBackend *b, XWWindow *win, const XWBackendOpt *opt, XWError *err) {
  if (web_gtk_ensure(err) != 0)
    return -1;

  WebCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->win = win;
  /* the master level starts at the set-time volume so a relative step has a
   * sensible origin, but nothing is pushed to the page until the user
   * actually changes it (ctl_started) */
  ctx->ctl_volume = opt->volume > 0.0 ? opt->volume : 100.0;
  ctx->ctl_muted = opt->mute ? 1 : 0;
  ctx->ctl_started = (opt->mute || (opt->volume > 0.0 && opt->volume != 100.0))
                         ? 1
                         : 0;
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
  web_drop_key_mask(ctx->window);
  web_drop_key_mask(ctx->view);
  g_signal_connect(ctx->view, "map-event", G_CALLBACK(web_view_map_cb), ctx);

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

/* DPMS power-off freeze: stop presenting (so the display stays off),
 * hard-mute the view and pause page media. Mirrors the video/scene
 * set_paused hook; dpms_paused is independent of the user's ctl_paused
 * so a DPMS cycle never clears a manual pause. */
static void web_set_paused(XWBackend *b, int paused) {
  WebCtx *ctx = (WebCtx *)b->priv;
  if (!ctx || !ctx->view || !ctx->window)
    return;
  ctx->dpms_paused = paused ? 1 : 0;
  if (paused) {
    webkit_web_view_set_is_muted(WEBKIT_WEB_VIEW(ctx->view), TRUE);
    web_eval(ctx, "window.__xwCtlMedia&&window.__xwCtlMedia('pause');");
    gtk_widget_hide(ctx->window);
  } else {
    gtk_widget_show(ctx->window);
    webkit_web_view_set_is_muted(WEBKIT_WEB_VIEW(ctx->view), FALSE);
    if (!ctx->ctl_paused)
      web_eval(ctx, "window.__xwCtlMedia&&window.__xwCtlMedia('play');");
  }
}

/* 同类型复用: 换 URL/页面后重新加载 webkit 视图。
 * 换壁纸时丢弃旧 overrides (同名 property 不互通), 同一壁纸重载则保留
 * 仍存在的 overrides 并剪掉已消失的。 */
static int web_reload(XWBackend *b, const XWBackendOpt *opt, XWError *err) {
  WebCtx *ctx = (WebCtx *)b->priv;
  char old_root[sizeof(ctx->root_dir)];
  const char *old_path;
  int same;
  if (!ctx)
    return -1;
  snprintf(old_root, sizeof(old_root), "%s", ctx->root_dir);
  old_path = ctx->path;
  if (web_store_input(ctx, opt->path, err) == 0)
    return -1;
  if (old_root[0] || ctx->root_dir[0])
    same = strcmp(old_root, ctx->root_dir) == 0;
  else if (old_path && opt->path)
    same = strcmp(old_path, opt->path) == 0;
  else
    same = 0;
  if (!same)
    web_clear_overrides(ctx);
  else
    web_prune_overrides(ctx);
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
  web_free_props(ctx);
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
    .control = web_control,
    .set_paused = web_set_paused,
    .list_props = web_list_props,
    .set_prop = web_set_prop,
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
  if (web_gtk_ensure(err) != 0)
    return -1;

  WebCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->ctl_volume = opt->volume > 0.0 ? opt->volume : 100.0;
  ctx->ctl_muted = opt->mute ? 1 : 0;
  ctx->ctl_started = (opt->mute || (opt->volume > 0.0 && opt->volume != 100.0))
                         ? 1
                         : 0;
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
