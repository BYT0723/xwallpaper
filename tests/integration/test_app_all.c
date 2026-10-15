/* Integration test: app module --all + swap under Xvfb.
 * set --all creates one window per output; set -m <output> on an existing
 * output swaps its backend in place without duplicating the window. */

#include <stdio.h>
#include <string.h>

#include "../harness.h"
#include "../../src/app.h"
#include "../../src/cli.h"
#include "../../src/x11.h"

static void set_path(XWConfig *cfg, const char *p) {
  snprintf(cfg->path, sizeof(cfg->path), "%s", p);
}

static void test_app_all_then_swap(void) {
  TEST_BEGIN("set --all creates per-output windows; set -m swaps in place");
  XWError err;
  memset(&err, 0, sizeof(err));

  XWApp *app = xw_app_create(&err);
  TEST_CHECK(app != NULL);
  if (!app)
    return;

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.command = XW_CMD_SET;
  cfg.target = XW_TARGET_ALL;
  set_path(&cfg, "tests/data/red.png");
  cfg.scale_mode = XW_SCALE_CONTAIN;
  cfg.opacity = 1.0;
  cfg.backend_explicit = -1;

  /* Key bindings are the daemon's, not mpv's: the window must select
   * KeyPress so presses arrive at all, and the file must have been parsed
   * into it. The action's effect on a backend is covered by test_video. */
  {
    FILE *kf = fopen("/tmp/xw_app_keys.keys", "w");
    if (kf) {
      fputs("0 add volume 2\n- cycle mute\n", kf);
      fclose(kf);
    }
    snprintf(cfg.keybinds, sizeof(cfg.keybinds), "/tmp/xw_app_keys.keys");
  }

  TEST_CHECK_EQ_INT(xw_app_set(app, &cfg, &err), 0);
  int before = xw_app_window_count(app);
  TEST_CHECK(before >= 1);
  for (int i = 0; i < before; i++) {
    XWWindow *win = xw_app_window(app, i);
    XWindowAttributes wa;
    XEvent ev;
    TEST_CHECK(win != NULL);
    TEST_CHECK(win->width > 0 && win->height > 0);
    TEST_CHECK(win->output[0] != '\0');
    TEST_CHECK_EQ_INT(win->nbinds, 2);
    if (XGetWindowAttributes(win->dpy, win->win, &wa))
      TEST_CHECK((wa.your_event_mask & KeyPressMask) != 0);

    /* a bound key reaches the window's handler and is consumed */
    memset(&ev, 0, sizeof(ev));
    ev.xkey.type = KeyPress;
    ev.xkey.display = win->dpy;
    ev.xkey.window = win->win;
    ev.xkey.root = win->win;
    ev.xkey.subwindow = None;
    ev.xkey.time = CurrentTime;
    ev.xkey.same_screen = True;
    ev.xkey.keycode = 19; /* '0' on a standard layout */
    TEST_CHECK_EQ_INT(xw_app_handle_event(app, &ev), 1);

    /* an event for a window that is not ours is not consumed */
    ev.xkey.window = None;
    TEST_CHECK_EQ_INT(xw_app_handle_event(app, &ev), 0);
    break; /* the first window is enough; the rest share the path */
  }

  xw_app_stop(app);
  TEST_CHECK_EQ_INT(xw_app_run(app), 0);

  /* swap the first window's output in place */
  char out[64];
  snprintf(out, sizeof(out), "%s", xw_app_window(app, 0)->output);
  XWConfig swap;
  memset(&swap, 0, sizeof(swap));
  swap.command = XW_CMD_SET;
  swap.target = XW_TARGET_OUTPUT;
  snprintf(swap.output, sizeof(swap.output), "%s", out);
  set_path(&swap, "tests/data/red.png");
  swap.scale_mode = XW_SCALE_STRETCH;
  swap.opacity = 1.0;
  swap.backend_explicit = -1;
  TEST_CHECK_EQ_INT(xw_app_set(app, &swap, &err), 0);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), before); /* no duplication */

  cfg.command = XW_CMD_CLEAR;
  cfg.target = XW_TARGET_ALL;
  xw_app_clear(app, &cfg, &err);
  TEST_CHECK_EQ_INT(xw_app_window_count(app), 0);

  xw_app_destroy(app);
}

int main(void) {
  TEST_SUITE("app all + swap (Xvfb)");
  test_app_all_then_swap();
  TEST_SUMMARY();
}
