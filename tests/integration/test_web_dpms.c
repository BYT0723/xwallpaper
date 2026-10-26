/* Integration test: web backend DPMS freeze under Xvfb.
 * set_paused(1) (DPMS power-off) must hide the window (stop presenting so the
 * display stays off), hard-mute the view and pause page media; set_paused(0)
 * restores presentation and unmutes without clearing a manual user pause. */

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

static GtkWidget *find_web_toplevel(void) {
  GList *tops = gtk_window_list_toplevels();
  GtkWidget *found = NULL;
  for (GList *l = tops; l; l = l->next) {
    GList *ch = gtk_container_get_children(GTK_CONTAINER(l->data));
    for (GList *c = ch; c; c = c->next) {
      if (WEBKIT_IS_WEB_VIEW(c->data)) {
        found = GTK_WIDGET(l->data);
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

static GtkWidget *find_web_view(void) {
  GtkWidget *top = find_web_toplevel();
  if (!top)
    return NULL;
  GList *ch = gtk_container_get_children(GTK_CONTAINER(top));
  GtkWidget *found = NULL;
  for (GList *c = ch; c; c = c->next) {
    if (WEBKIT_IS_WEB_VIEW(c->data)) {
      found = GTK_WIDGET(c->data);
      break;
    }
  }
  g_list_free(ch);
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

/* How many times the fixture page's <video> received play() (-1 on error).
 * The shim may invoke play once per tracked copy, so callers compare
 * relatively instead of asserting an absolute count. */
static int play_count(GtkWidget *view) {
  char *s = run_js_sync(view, "String(window.__plays)");
  int n = s ? atoi(s) : -1;
  g_free(s);
  return n;
}

static const char kPage[] =
    "<html><body>"
    "<video id=\"v\"></video>"
    "<script>window.__plays=0;"
    "var v=document.getElementById('v');"
    "v.play=function(){window.__plays++;};"
    "</script>"
    "</body></html>";

static void test_web_dpms_freeze(void) {
  TEST_BEGIN("dpms pause hides window, mutes view and pauses media");
  XWError err, ctl;
  memset(&err, 0, sizeof(err));
  memset(&ctl, 0, sizeof(ctl));
  Display *dpy = xw_x11_open_display(&err);
  TEST_CHECK(dpy != NULL);
  if (!dpy)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_GEOM;
  cfg.have_geom = 1;
  snprintf(cfg.name, sizeof(cfg.name), "webdpms");
  cfg.geom_x = 0; cfg.geom_y = 0; cfg.geom_w = 400; cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/webdir";

  XWBackend backend = *xw_web_backend_new();
  TEST_CHECK(backend.set_paused != NULL);
  if (!backend.set_paused) {
    xw_x11_destroy_window(win);
    return;
  }
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);

  GtkWidget *view = find_web_view();
  TEST_CHECK(view != NULL);
  if (!view) {
    backend.destroy(&backend);
    xw_x11_destroy_window(win);
    return;
  }
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(view), kPage, NULL);
  pump_gtk(dpy, 4);

  view = find_web_view();
  TEST_CHECK(view != NULL);
  if (!view) {
    backend.destroy(&backend);
    xw_x11_destroy_window(win);
    return;
  }

  /* freeze: window hidden, view hard-muted, media paused */
  backend.set_paused(&backend, 1);
  pump_gtk(dpy, 1);
  GtkWidget *top = find_web_toplevel();
  TEST_CHECK(top != NULL && !gtk_widget_get_visible(top));
  TEST_CHECK(webkit_web_view_get_is_muted(WEBKIT_WEB_VIEW(view)));

  /* thaw: presentation back, view unmuted, media resumed (no user pause) */
  backend.set_paused(&backend, 0);
  pump_gtk(dpy, 1);
  top = find_web_toplevel();
  TEST_CHECK(top != NULL && gtk_widget_get_visible(top));
  TEST_CHECK(!webkit_web_view_get_is_muted(WEBKIT_WEB_VIEW(view)));
  /* resume replays play (no user pause); compare relatively */
  int n0 = play_count(view);
  TEST_CHECK(n0 > 0);

  /* a manual user pause survives a DPMS cycle: thaw must not replay */
  TEST_CHECK_EQ_INT(backend.control(&backend, "pause", "on", &ctl), 0);
  pump_gtk(dpy, 1);
  backend.set_paused(&backend, 1);
  pump_gtk(dpy, 1);
  top = find_web_toplevel();
  TEST_CHECK(top != NULL && !gtk_widget_get_visible(top));
  backend.set_paused(&backend, 0);
  pump_gtk(dpy, 1);
  top = find_web_toplevel();
  TEST_CHECK(top != NULL && gtk_widget_get_visible(top));
  TEST_CHECK_EQ_INT(play_count(view), n0);
  TEST_CHECK_EQ_INT(backend.control(&backend, "pause", "off", &ctl), 0);
  pump_gtk(dpy, 1);

  /* switching wallpapers mid-freeze: the fresh view must stay frozen
   * (muted + hidden) instead of playing unmuted behind the lock screen.
   * No volume/mute touched yet (ctl_started==0), so without a dpms-aware
   * LOAD_FINISHED push the new page would play audibly while hidden. */
  backend.set_paused(&backend, 1);
  pump_gtk(dpy, 1);
  {
    XWBackendOpt opt2;
    memset(&opt2, 0, sizeof(opt2));
    opt2.type = XW_BACKEND_WEB;
    opt2.path = "tests/data/wepropset";
    TEST_CHECK_EQ_INT(backend.reload(&backend, &opt2, &err), 0);
  }
  pump_gtk(dpy, 4);
  view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view) {
    top = find_web_toplevel();
    TEST_CHECK(top != NULL && !gtk_widget_get_visible(top));
    TEST_CHECK(webkit_web_view_get_is_muted(WEBKIT_WEB_VIEW(view)));
  }
  backend.set_paused(&backend, 0);
  pump_gtk(dpy, 1);

  /* a manual user mute survives a DPMS cycle: thaw must restore the
   * view mute flag to ctl_muted (and keep the JS level muted) */
  TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "on", &ctl), 0);
  pump_gtk(dpy, 1);
  backend.set_paused(&backend, 1);
  pump_gtk(dpy, 1);
  top = find_web_toplevel();
  TEST_CHECK(top != NULL && !gtk_widget_get_visible(top));
  view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view)
    TEST_CHECK(webkit_web_view_get_is_muted(WEBKIT_WEB_VIEW(view)));
  backend.set_paused(&backend, 0);
  pump_gtk(dpy, 1);
  top = find_web_toplevel();
  TEST_CHECK(top != NULL && gtk_widget_get_visible(top));
  view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view) {
    TEST_CHECK(webkit_web_view_get_is_muted(WEBKIT_WEB_VIEW(view)));
    {
      char *m = run_js_sync(
          view, "String(window.__xwCtl&&window.__xwCtl.muted)");
      TEST_CHECK(m != NULL && strcmp(m, "true") == 0);
      g_free(m);
    }
  }
  TEST_CHECK_EQ_INT(backend.control(&backend, "mute", "off", &ctl), 0);
  pump_gtk(dpy, 1);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
}

int main(void) {
  TEST_SUITE("web dpms freeze (Xvfb)");
  test_web_dpms_freeze();
  TEST_SUMMARY();
}
