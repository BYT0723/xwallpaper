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
  gint64 deadline = g_get_monotonic_time() + 3 * 1000 * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    XSync(dpy, False);
    g_usleep(10000);
  }
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
  WebKitJavascriptResult *res =
      webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(o), r, &e);
  if (res) {
    JSCValue *v = webkit_javascript_result_get_js_value(res);
    if (v) {
      char *s = jsc_value_to_string(v);
      if (s) {
        out->value = g_strdup(s);
        g_free(s);
      }
    }
    webkit_javascript_result_unref(res);
  }
  if (e)
    g_error_free(e);
  out->done = 1;
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

  gint64 deadline = g_get_monotonic_time() + 3 * 1000 * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    XSync(dpy, False);
    g_usleep(10000);
  }

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

    JsOut out;
    memset(&out, 0, sizeof(out));
    webkit_web_view_run_javascript(
        WEBKIT_WEB_VIEW(view),
        "try{localStorage.setItem('xw-test','42');"
        "localStorage.getItem('xw-test');}catch(e){'JSERR:'+e}",
        NULL, js_done_cb, &out);
    deadline = g_get_monotonic_time() + 5 * 1000 * 1000;
    while (!out.done && g_get_monotonic_time() < deadline) {
      g_main_context_iteration(NULL, FALSE);
      g_usleep(10000);
    }
    TEST_CHECK(out.done);
    if (out.done)
      TEST_CHECK_EQ_STR(out.value ? out.value : "(null)", "42");
    g_free(out.value);
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("web backend (Xvfb)");
  test_web_paints();
  test_context_menu_blocked();
  test_storage_persistent();
  TEST_SUMMARY();
}
