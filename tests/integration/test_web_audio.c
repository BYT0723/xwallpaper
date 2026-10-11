/* Integration test: WE audio listener under Xvfb.
 * A fixture page registers window.wallpaperRegisterAudioListener; the test
 * feeds a synthetic 100Hz sine through we_audio_map + xw_web_we_push (the
 * same path the daemon tick uses) and asserts the page saw 128 bins with
 * bass dominating treble. No Pulse server required. */

#include <math.h>
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
#include "../../src/we_audio.h"
#include "../../src/web.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

static void test_audio_listener_end_to_end(void) {
  TEST_BEGIN("audio listener receives 128 bins, bass beats treble");
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
  snprintf(cfg.name, sizeof(cfg.name), "weaudio");
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
  opt.path = "tests/data/weaudio";

  XWBackend *tmpl = xw_web_backend_new();
  XWBackend backend = *tmpl;
  TEST_CHECK_EQ_INT(backend.init(&backend, win, &opt, &err), 0);
  TEST_CHECK_EQ_INT(backend.start(&backend), 0);

  /* let the page load, run the document-start shim, register the listener
   * and notify native through the xwAudio message handler */
  pump_gtk(dpy, 4);
  TEST_CHECK_EQ_INT(xw_web_we_want(), 1);

  /* synthetic 100Hz full-scale sine, same feed path as the daemon tick */
  static float L[2048], R[2048];
  for (int i = 0; i < 2048; i++) {
    L[i] = (float)sin(2.0 * M_PI * 100.0 * i / 48000.0);
    R[i] = L[i];
  }
  float bins[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, 2048, bins), 0);
  xw_web_we_push(bins);
  pump_gtk(dpy, 2);

  GtkWidget *view = find_web_view();
  TEST_CHECK(view != NULL);
  if (view) {
    const char *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(view));
    TEST_CHECK(title != NULL && strncmp(title, "WEAUDIO:128:", 12) == 0);
    if (title && strncmp(title, "WEAUDIO:128:", 12) == 0) {
      double bass = 0, treb = 0;
      sscanf(title + 12, "%lf:%lf", &bass, &treb);
      TEST_CHECK(bass > 0.05);
      TEST_CHECK(bass > 3.0 * treb);
    }
  }

  backend.destroy(&backend);
  xw_x11_destroy_window(win);
  pump_gtk(dpy, 1);
  TEST_CHECK_EQ_INT(xw_web_we_want(), 0); /* destroy unregisters */
}

int main(void) {
  TEST_SUITE("web audio (Xvfb)");
  test_audio_listener_end_to_end();
  TEST_SUMMARY();
}
