/* Integration test: WE media bridge under Xvfb.
 * Feeds a synthetic snapshot through xw_web_media_push (the same fan-out the
 * daemon tick uses) and reads the page's document.title. No MPRIS bus needed. */

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <X11/Xlib.h>

#include "../harness.h"
#include "../../src/backend.h"
#include "../../src/backends.h"
#include "../../src/x11.h"
#include "../../src/cli.h"
#include "../../src/web.h"

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

static void pump_gtk(Display *dpy, int secs) {
  gint64 deadline = g_get_monotonic_time() + (gint64)secs * 1000 * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    if (dpy)
      XSync(dpy, False);
    g_usleep(10000);
  }
}

static const char *page_title(void) {
  GtkWidget *view = find_web_view();
  return view ? webkit_web_view_get_title(WEBKIT_WEB_VIEW(view)) : NULL;
}

static void fill(XWMediaInfo *m, const char *title, int state, double pos,
                 double dur, const char *thumb, uint64_t version) {
  memset(m, 0, sizeof(*m));
  snprintf(m->title, sizeof(m->title), "%s", title);
  snprintf(m->artist, sizeof(m->artist), "%s", title[0] ? "Artist" : "");
  snprintf(m->album, sizeof(m->album), "%s", title[0] ? "Album" : "");
  snprintf(m->content_type, sizeof(m->content_type), "music");
  m->thumbnail = (char *)thumb;
  m->state = state;
  m->position = pos;
  m->duration = dur;
  m->enabled = 1;
  m->v_props = m->v_thumb = m->v_playback = m->v_timeline = m->v_status =
      version;
}

static void test_media_bridge(void) {
  TEST_BEGIN("media listeners receive snapshot, dedup, update and clear");
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
  snprintf(cfg.name, sizeof(cfg.name), "wemedia");
  cfg.geom_x = 0;
  cfg.geom_y = 0;
  cfg.geom_w = 400;
  cfg.geom_h = 300;

  XWWindow *win = xw_x11_create_window(dpy, &cfg, 0, 0, 400, 300, &err);
  TEST_CHECK(win != NULL);
  if (!win)
    return;

  XWBackendOpt opt;
  memset(&opt, 0, sizeof(opt));
  opt.type = XW_BACKEND_WEB;
  opt.path = "tests/data/wemedia";

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);
  pump_gtk(dpy, 4);
  TEST_CHECK_EQ_INT(xw_web_media_want(), 1);

  XWMediaInfo m;
  fill(&m, "Test Song", XW_WE_MEDIA_PLAYING, 12.5, 180.0,
       "data:image/png;base64,AAAA", 1);
  xw_web_media_push(&m);
  pump_gtk(dpy, 1);
  const char *title = page_title();
  TEST_CHECK(title != NULL);
  if (title) {
    TEST_CHECK(strstr(title, "WEMEDIA:Test Song|Artist|0|12.5|180|"
                             "data:image/png;base64,AAAA|true") != NULL);
  }

  /* Identical versions must not evaluate JS again. */
  int n0 = xw_web_media_eval_count();
  xw_web_media_push(&m);
  TEST_CHECK_EQ_INT(xw_web_media_eval_count(), n0);

  /* A props bump must update the title and force a timeline refresh. */
  fill(&m, "Next Song", XW_WE_MEDIA_PLAYING, 1.0, 90.0,
       "data:image/png;base64,AAAA", 2);
  xw_web_media_push(&m);
  pump_gtk(dpy, 1);
  title = page_title();
  TEST_CHECK(title && strstr(title, "WEMEDIA:Next Song|Artist|0|1|90")
                        != NULL);

  /* Empty snapshot clears text/thumb and reports STOPPED. */
  fill(&m, "", XW_WE_MEDIA_STOPPED, 0.0, 0.0, NULL, 3);
  xw_web_media_push(&m);
  pump_gtk(dpy, 1);
  title = page_title();
  TEST_CHECK(title && strstr(title, "WEMEDIA:||2|0|0||true") != NULL);

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
  pump_gtk(dpy, 1);
  TEST_CHECK_EQ_INT(xw_web_media_want(), 0);
}

int main(void) {
  TEST_SUITE("web media (Xvfb)");
  test_media_bridge();
  TEST_SUMMARY();
}
