/* Integration test: web backend under Xvfb.
 * Verifies webkit actually paints into the wallpaper window: creates the
 * app window, loads a wallpaper *directory* (file:// index.html with the
 * file-access switches on, exercising module + fetch + css subresources),
 * pumps the GTK context (the pattern the daemon uses), then samples a pixel. */

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <jsc/jsc.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"

static GtkWidget *find_web_view(void) {
  GList *tops = gtk_window_list_toplevels();
  GtkWidget *found = NULL;
  for (GList *l = tops; l; l = l->next) {
    GList *ch = gtk_container_get_children(GTK_CONTAINER(l->data));
    for (GList *c = ch; c; c = c->next) {
      if (WEBKIT_IS_WEB_VIEW(c->data)) {
        found = GTK_WIDGET(c->data);
        break;
      }
    }
    g_list_free(ch);
    if (found)
      break;
  }
  g_list_free(tops);
  return found;
}

/* Pump the GTK main context (the pattern the daemon uses) for up to secs. */
static void pump_gtk(Display *dpy, int secs) {
  gint64 deadline = g_get_monotonic_time() + (gint64)secs * 1000 * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    if (dpy)
      XSync(dpy, False);
    g_usleep(10000);
  }
}

static void test_context_menu_blocked(void) {
  TEST_BEGIN("right-click context menu is suppressed");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "webmenu");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/webdir";

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);

  GtkWidget *view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view) {
    guint sig = g_signal_lookup("context-menu", WEBKIT_TYPE_WEB_VIEW);
    TEST_CHECK(sig != 0);
    if (sig) {
      gulong h = g_signal_handler_find(view, G_SIGNAL_MATCH_ID, sig,
                                       0, NULL, NULL, NULL);
      TEST_CHECK(h != 0); /* our blocker must be wired */
    }
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

static void test_web_paints(void) {
  TEST_BEGIN("web backend paints non-black pixels");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "webtest");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/webdir"; /* index.html: module + fetch paint #006699 */

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  /* pump GTK + let webkit render for up to ~3s */
  pump_gtk(dpy, 3);
  XSync(dpy, False);

  /* sample the center pixel; page bg is #006699 so blue should dominate */
  XImage *img = XGetImage(dpy, win->win, 200, 150, 1, 1, AllPlanes, ZPixmap);
  TEST_CHECK(img != NULL);
  if (img) {
    unsigned long px = XGetPixel(img, 0, 0);
    XFree(img);
    int r = (px >> 16) & 0xff;
    int g = (px >> 8) & 0xff;
    int b = px & 0xff;
    TEST_CHECK(b > 100 && b > r && b > g); /* blue-dominant = page bg */
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

typedef struct {
  int done;
  char *value;
} JsOut;

static void js_done_cb(GObject *o, GAsyncResult *r, gpointer ud) {
  JsOut *out = (JsOut *)ud;
  GError *e = NULL;
  JSCValue *v =
      webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(o), r, &e);
  if (v) {
    char *s = jsc_value_to_string(v);
    if (s) {
      out->value = g_strdup(s);
      g_free(s);
    }
    g_object_unref(v);
  }
  if (e)
    g_error_free(e);
  out->done = 1;
}

static char *run_js_sync(GtkWidget *view, const char *script) {
  JsOut out;
  memset(&out, 0, sizeof(out));
  webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(view), script, -1,
                                       NULL, NULL, NULL, js_done_cb, &out);
  gint64 deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
  while (!out.done && g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    g_usleep(10000);
  }
  return out.value; /* caller g_frees, may be NULL on timeout */
}

static void test_storage_persistent(void) {
  TEST_BEGIN("web storage is persistent and per-wallpaper isolated");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "webstore");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/webdir";

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  pump_gtk(dpy, 3);

  GtkWidget *view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view) {
    WebKitWebContext *wctx =
        webkit_web_view_get_context(WEBKIT_WEB_VIEW(view));
    /* ephemeral contexts keep localStorage in memory only: wallpaper
     * settings would reset on every daemon restart. Must be disk-backed. */
    TEST_CHECK(!webkit_web_context_is_ephemeral(wctx));
    WebKitWebsiteDataManager *man =
        webkit_web_context_get_website_data_manager(wctx);
    const char *datadir =
        webkit_website_data_manager_get_base_data_directory(man);
    /* per-wallpaper isolation: must not be the shared default location */
    TEST_CHECK(datadir != NULL && strstr(datadir, "xwallpaper") != NULL);

    char *stored = run_js_sync(view,
        "try{localStorage.setItem('xw-test','42');"
        "localStorage.getItem('xw-test');}catch(e){'JSERR:'+e}");
    TEST_CHECK_EQ_STR(stored ? stored : "(null)", "42");
    g_free(stored);
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

/* Wallpaper de-interaction: author grab cursors / draggable nodes must not
 * leak browser affordances into the desktop. Passive input (mousemove,
 * clicks, rAF) stays alive. */
static void test_wallpaper_noninteractive(void) {
  TEST_BEGIN("web wallpaper suppresses drag/select/grab cursor");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "webnointeract");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/webdir";

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);

  GtkWidget *view = find_web_view();
  TEST_CHECK(view != NULL);
  if (!view) {
    backend.destroy(&backend);
    xw_x11_destroy_window(win);
    return;
  }
  webkit_web_view_load_html(
      WEBKIT_WEB_VIEW(view),
      "<html><body>"
      "<div id=\"t\" draggable=\"true\" style=\"cursor:grab\">drag me</div>"
      "</body></html>",
      NULL);
  pump_gtk(dpy, 3);

  char *style = run_js_sync(view,
      "(function(){var el=document.getElementById('t');"
      "if(!el)return 'NOEL';"
      "var cs=getComputedStyle(el);"
      "return cs.getPropertyValue('-webkit-user-select')+'|'"
      "+cs.getPropertyValue('user-select')+'|'+cs.userSelect+'|'"
      "+cs.getPropertyValue('cursor');"
      "})()");
  TEST_CHECK(style != NULL && strstr(style, "none") != NULL);
  TEST_CHECK(style != NULL && strstr(style, "default") != NULL);
  TEST_CHECK(style == NULL || strstr(style, "grab") == NULL);
  g_free(style);

  char *drag = run_js_sync(view,
      "(function(){var el=document.getElementById('t');"
      "var e=new Event('dragstart',{bubbles:true,cancelable:true});"
      "el.dispatchEvent(e);return e.defaultPrevented?'1':'0';})()");
  TEST_CHECK_EQ_STR(drag ? drag : "(null)", "1");
  g_free(drag);

  char *sel = run_js_sync(view,
      "(function(){"
      "var e=new Event('selectstart',{bubbles:true,cancelable:true});"
      "document.dispatchEvent(e);return e.defaultPrevented?'1':'0';})()");
  TEST_CHECK_EQ_STR(sel ? sel : "(null)", "1");
  g_free(sel);

  /* passive input must survive: mousemove listeners still fire */
  char *move = run_js_sync(view,
      "(function(){window.__moved='0';"
      "document.addEventListener('mousemove',function(){window.__moved='1';});"
      "document.dispatchEvent(new Event('mousemove',{bubbles:true}));"
      "return window.__moved;})()");
  TEST_CHECK_EQ_STR(move ? move : "(null)", "1");
  g_free(move);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

/* Wallpaper Engine emulation: after load, general.properties from the
 * sibling project.json must reach applyUserProperties (many WE pages only
 * apply background/sizes/colors there). The fixture listener echoes the
 * received value into the title. */
static void test_we_properties_delivered(void) {
  TEST_BEGIN("WE project.json properties reach applyUserProperties");
  XWError err;
  memset(&err, 0, sizeof(err));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "weprops");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/weprops";

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  pump_gtk(dpy, 6);

  GtkWidget *view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view) {
    const char *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(view));
    TEST_CHECK(title != NULL && strstr(title, "WE:cover") != NULL);
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

/* Runtime audio control: WebKitGTK has no volume API, so the injected shim
 * must drive media elements and Web Audio graphs from inside the page. The
 * fixture has both an <audio> element and an AudioContext wired straight to
 * its destination, which is what a wallpaper using Web Audio does. */
/* Effective (native) level of a media element, which is what actually
 * reaches the audio device - as opposed to the level the page believes it
 * set. Two decimals because the master is applied as a multiplier. */
static char *js_vol(GtkWidget *view, const char *expr) {
  char js[256];
  snprintf(js, sizeof(js),
           "(function(){try{return window.__xwCtlRaw(%s).toFixed(2);}"
           "catch(e){return 'err';}})()",
           expr);
  return run_js_sync(view, js);
}

static void test_audio_control(void) {
  XWError err, ctl;
  Display *dpy;
  XWConfig cfg;
  XWWindow *win;
  XWBackendOpt opt;
  XWBackend backend;
  GtkWidget *view;
  char *s;
  int i;

  TEST_BEGIN("volume/mute reach media elements and Web Audio graphs");
  memset(&err, 0, sizeof(err));
  memset(&ctl, 0, sizeof(ctl));
  dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "wectl");
  cfg.geom_w = 400;
  cfg.geom_h = 300;
  win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/weaudioctl";
  opt.volume = 100.0;
  backend = *xw_web_backend_new();
  if (backend.init(&backend, win, &opt, &err) != 0 || !backend.control) {
    TEST_FAIL("web init: %s", err.msg);
    if (win)
      xw_x11_destroy_window(win);
    return;
  }
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  pump_gtk(dpy, 8);

  view = find_web_view();
  TEST_CHECK(view != NULL);
  if (!view) {
    backend.destroy(&backend);
    xw_x11_destroy_window(win);
    return;
  }

  /* Nothing is pushed before the user turns the volume: a page's own level
   * must be left exactly as it is. The detached elements are played first so
   * the play() hook has seen them. */
  s = run_js_sync(view, "String(window.__xwCtl.started)");
  TEST_CHECK(s != NULL && strcmp(s, "false") == 0);
  g_free(s);
  g_free(run_js_sync(view, "window.detPlay();window.det2Play();0"));
  s = js_vol(view, "window.det");
  TEST_CHECK(s != NULL && strcmp(s, "0.50") == 0);
  g_free(s);

  TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "50", &ctl), 0);
  pump_gtk(dpy, 3);
  /* in-DOM element, the detached BGM the page levels itself, and a detached
   * element the page never touches */
  s = js_vol(view, "document.getElementById('a')");
  TEST_CHECK(s != NULL && strcmp(s, "0.50") == 0);
  g_free(s);
  s = js_vol(view, "window.det");
  TEST_CHECK(s != NULL && strcmp(s, "0.25") == 0);
  g_free(s);
  s = js_vol(view, "window.det2");
  TEST_CHECK(s != NULL && strcmp(s, "0.50") == 0);
  g_free(s);
  s = run_js_sync(view, "String(window.__xwCtl.gains.length)");
  TEST_CHECK(s != NULL && strcmp(s, "1") == 0);
  g_free(s);
  s = run_js_sync(view, "String(window.__xwCtl.gains[0].gain.value)");
  TEST_CHECK(s != NULL && strcmp(s, "0.5") == 0);
  g_free(s);

  /* Mute must survive the page re-syncing its own BGM level (the fixture
   * does that every 50ms, exactly like the real wallpapers), which is why
   * the level is composed on write instead of assigned once. */
  TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "toggle", &ctl), 0);
  pump_gtk(dpy, 3);
  s = js_vol(view, "document.getElementById('a')");
  TEST_CHECK(s != NULL && strcmp(s, "0.00") == 0);
  g_free(s);
  s = js_vol(view, "window.det");
  TEST_CHECK(s != NULL && strcmp(s, "0.00") == 0);
  g_free(s);
  s = js_vol(view, "window.det2");
  TEST_CHECK(s != NULL && strcmp(s, "0.00") == 0);
  g_free(s);
  s = run_js_sync(view, "String(window.__xwCtl.gains[0].gain.value)");
  TEST_CHECK(s != NULL && strcmp(s, "0") == 0);
  g_free(s);
  /* the page still reads back the level it set, not the composed one */
  s = run_js_sync(view, "String(window.det.volume)");
  TEST_CHECK(s != NULL && strcmp(s, "0.5") == 0);
  g_free(s);

  TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "off", &ctl), 0);
  pump_gtk(dpy, 3);
  s = js_vol(view, "window.det");
  TEST_CHECK(s != NULL && strcmp(s, "0.25") == 0);
  g_free(s);

  /* a relative step scales from the last level */
  TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "+10", &ctl), 0);
  pump_gtk(dpy, 3);
  s = js_vol(view, "window.det");
  TEST_CHECK(s != NULL && strcmp(s, "0.30") == 0);
  g_free(s);

  /* what a web page cannot do fails honestly */
  TEST_CHECK_EQ_INT(backend.control(&backend, "rotate", "cycle", &ctl), -1);
  TEST_CHECK_EQ_INT(backend.control(&backend, "volume", "abc", &ctl), -1);
  TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "bogus", &ctl), -1);

  /* the level survives a navigation: reload and read it back */
  webkit_web_view_reload(WEBKIT_WEB_VIEW(view));
  for (i = 0; i < 20; i++)
    pump_gtk(dpy, 2);
  s = js_vol(view, "window.det");
  TEST_CHECK(s != NULL && strcmp(s, "0.30") == 0);
  g_free(s);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("web backend (Xvfb)");
  test_web_paints();
  test_context_menu_blocked();
  test_wallpaper_noninteractive();
  test_storage_persistent();
  test_we_properties_delivered();
  test_audio_control();
  TEST_SUMMARY();
}
