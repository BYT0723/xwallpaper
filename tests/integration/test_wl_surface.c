/* Wayland surface smoke: requires a wlr-layer-shell compositor.
 * Without one (no WAYLAND_DISPLAY or connect failure) prints SKIP and
 * exits 0 — mirrors the test-xvfb skip semantics. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/cli.h"
#include "../../src/wl.h"
#include "../../src/xw_err.h"

int main(void) {
#ifdef XW_HAVE_WAYLAND
  if (!getenv("WAYLAND_DISPLAY")) {
    printf("SKIP: WAYLAND_DISPLAY not set (no wayland compositor)\n");
    return 0;
  }
  XWError err = {0, ""};
  xw_wl_ctx *ctx = xw_wl_connect(&err);
  if (!ctx) {
    printf("SKIP: cannot connect (%s)\n", err.msg);
    return 0;
  }
  printf("outputs: %d\n", xw_wl_output_count(ctx));
  const XWWlOutput *outs = xw_wl_outputs(ctx);
  xw_wl_list_outputs(outs, xw_wl_output_count(ctx), stdout);

  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  int x = 0, y = 0;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  if (xw_wl_resolve_geometry(outs, xw_wl_output_count(ctx), &cfg,
                             &x, &y, &w, &h, name) != 0) {
    printf("FAIL: resolve_geometry failed\n");
    xw_wl_disconnect(ctx);
    return 1;
  }
  XWWlWindow *win = xw_wl_create_window(ctx, &cfg, x, y, w, h, &err);
  if (!win) {
    printf("FAIL: create_window: %s\n", err.msg);
    xw_wl_disconnect(ctx);
    return 1;
  }
  printf("surface ok: %s %ux%u\n", win->output, win->width, win->height);
  xw_wl_destroy_window(win);
  xw_wl_disconnect(ctx);
  printf("PASS\n");
  return 0;
#else
  printf("SKIP: built without XW_HAVE_WAYLAND\n");
  return 0;
#endif
}
