#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>

#include "backend.h"
#include "backends.h"
#include "scene.h"
#include "scene_model.h"
#include "scene_parse.h"
#include "x11.h"

/* Scene backend shell (Task 7): parses the wallpaper directory, paints the
 * window with the camera clear color, and repaints on an fps-throttled tick.
 * GL object rendering lands in Task 8; the tick/paused plumbing is final. */
typedef struct XWSceneCtx {
  XWWindow *win;
  XWScene scene;
  char dir[1024];
  int parsed;
  int paused;
  double fps;
  uint64_t last_ms;
  unsigned long frames;
  GC gc;
} XWSceneCtx;

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int paint(XWSceneCtx *c) {
  XWWindow *win = c->win;
  float *cc = c->scene.camera.clearcolor;
  XColor xc;
  xc.red = (unsigned short)(cc[0] * 65535.0f);
  xc.green = (unsigned short)(cc[1] * 65535.0f);
  xc.blue = (unsigned short)(cc[2] * 65535.0f);
  xc.flags = DoRed | DoGreen | DoBlue;
  if (!XAllocColor(win->dpy, win->colormap, &xc))
    return -1;
  XSetForeground(win->dpy, c->gc, xc.pixel);
  XFillRectangle(win->dpy, win->win, c->gc, 0, 0, win->width, win->height);
  XFlush(win->dpy);
  c->frames++;
  c->last_ms = now_ms();
  return 0;
}

static int scene_init(XWBackend *b, XWWindow *win, const XWBackendOpt *opt,
                      XWError *err) {
  XWSceneCtx *c;
  if (!b || !win || !opt || !opt->path || !opt->path[0]) {
    XW_ERROR_SET(err, 1, "scene needs a wallpaper directory");
    return -1;
  }
  c = calloc(1, sizeof(*c));
  if (!c) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  c->win = win;
  snprintf(c->dir, sizeof(c->dir), "%s", opt->path);
  c->fps = opt->fps > 0.0 ? opt->fps : XW_SCENE_FPS_DEFAULT;
  if (xw_scene_parse(c->dir, &c->scene, err) != 0) {
    free(c);
    return -1;
  }
  c->parsed = 1;
  c->gc = XCreateGC(win->dpy, win->win, 0, NULL);
  if (!c->gc) {
    XW_ERROR_SET(err, 1, "cannot create scene GC");
    xw_scene_free(&c->scene);
    free(c);
    return -1;
  }
  b->priv = c;
  if (paint(c) != 0) {
    XW_ERROR_SET(err, 1, "cannot paint scene background");
    return -1;
  }
  return 0;
}

static int scene_start(XWBackend *b) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  if (!c || !c->parsed)
    return -1;
  return paint(c);
}

static void scene_stop(XWBackend *b) { (void)b; }

static void scene_destroy(XWBackend *b) {
  XWSceneCtx *c;
  if (!b || !(c = (XWSceneCtx *)b->priv))
    return;
  if (c->gc)
    XFreeGC(c->win->dpy, c->gc);
  xw_scene_free(&c->scene);
  free(c);
  b->priv = NULL;
}

static int scene_reload(XWBackend *b, const XWBackendOpt *opt, XWError *err) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  XWScene next;
  if (!c || !c->parsed || !opt || !opt->path || !opt->path[0]) {
    XW_ERROR_SET(err, 1, "scene reload needs a wallpaper directory");
    return -1;
  }
  memset(&next, 0, sizeof(next));
  if (xw_scene_parse(opt->path, &next, err) != 0)
    return -1;
  xw_scene_free(&c->scene);
  c->scene = next;
  snprintf(c->dir, sizeof(c->dir), "%s", opt->path);
  if (opt->fps > 0.0)
    c->fps = opt->fps;
  return paint(c);
}

static void scene_set_paused(XWBackend *b, int paused) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  if (c)
    c->paused = paused ? 1 : 0;
}

static void scene_tick(XWBackend *b) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  uint64_t interval, now;
  if (!c || !c->parsed || c->paused)
    return;
  interval = (uint64_t)(1000.0 / (c->fps > 0.0 ? c->fps : XW_SCENE_FPS_DEFAULT));
  if (interval < 1)
    interval = 1;
  now = now_ms();
  if (now - c->last_ms < interval)
    return;
  paint(c);
}

XWBackend *xw_scene_backend_new(void) {
  static XWBackend backend = {
    .name = "scene",
    .init = scene_init,
    .start = scene_start,
    .stop = scene_stop,
    .destroy = scene_destroy,
    .on_event = NULL,
    .reload = scene_reload,
    .init_wl = NULL,
    .reload_wl = NULL,
    .start_wl = NULL,
    .destroy_wl = NULL,
    .tick = scene_tick,
    .set_paused = scene_set_paused,
    .retry = NULL,
  };
  return &backend;
}

unsigned long xw_scene_frames(const XWBackend *b) {
  const XWSceneCtx *c = b ? (const XWSceneCtx *)b->priv : NULL;
  return c ? c->frames : 0;
}

int xw_scene_object_count(const XWBackend *b) {
  const XWSceneCtx *c = b ? (const XWSceneCtx *)b->priv : NULL;
  if (!c || !c->parsed)
    return -1;
  return c->scene.n_objects;
}
