#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>

#include "backend.h"
#include "backends.h"
#include "scene.h"
#include "scene_gl.h"
#include "scene_image.h"
#include "scene_model.h"
#include "scene_parse.h"
#include "scene_particle.h"
#include "scene_particle_gl.h"
#include "scene_script.h"
#include "scene_sound.h"
#include "scene_text.h"
#include "scene_text_gl.h"
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
  uint64_t t0_ms;
  uint64_t last_gl_try_ms;
  unsigned long frames;
  GC gc;
  XWGl *gl;
  XWEmitter *emitters;
  XWTextItem *texts;
  struct XWSound **sounds;
  int sound_sys;
  int mute;
  double volume;
  XWScript *script;
} XWSceneCtx;

static void free_emitters(XWSceneCtx *c) {
  int i;
  if (!c || !c->emitters)
    return;
  for (i = 0; i < c->scene.n_objects; i++) xw_emitter_free(&c->emitters[i]);
  free(c->emitters);
  c->emitters = NULL;
}

static void free_texts(XWSceneCtx *c) {
  int i;
  if (!c || !c->texts)
    return;
  for (i = 0; i < c->scene.n_objects; i++)
    xw_text_item_free(c->gl, &c->texts[i]);
  free(c->texts);
  c->texts = NULL;
}

/* Load every SOUND object with a file. Degrades: a dead device or a bad
 * file skips that object and the wallpaper runs mute. */
static void init_sounds(XWSceneCtx *c) {
  int i, want = 0;
  XWError serr;
  if (!c)
    return;
  for (i = 0; i < c->scene.n_objects; i++)
    if (c->scene.objects[i].type == XW_OBJ_SOUND &&
        c->scene.objects[i].sound.file[0])
      want = 1;
  if (!want)
    return;
  memset(&serr, 0, sizeof(serr));
  if (xw_sound_sys_init(&serr) != 0)
    return;
  c->sound_sys = 1;
  c->sounds = calloc(c->scene.n_objects > 0 ? (size_t)c->scene.n_objects : 1,
                     sizeof(struct XWSound *));
  if (!c->sounds)
    return;
  for (i = 0; i < c->scene.n_objects; i++) {
    const XWSceneObject *o = &c->scene.objects[i];
    char path[1152];
    if (o->type != XW_OBJ_SOUND || !o->sound.file[0])
      continue;
    snprintf(path, sizeof(path), "%.900s/%.200s", c->dir, o->sound.file);
    if (xw_sound_load(&c->sounds[i], path, &serr) != 0)
      c->sounds[i] = NULL;
  }
}

static void free_sounds(XWSceneCtx *c) {
  int i;
  if (!c || !c->sounds)
    return;
  for (i = 0; i < c->scene.n_objects; i++) {
    if (c->sounds[i])
      xw_sound_free(c->sounds[i]);
  }
  free(c->sounds);
  c->sounds = NULL;
  if (c->sound_sys) {
    xw_sound_sys_free();
    c->sound_sys = 0;
  }
}

static void start_sounds(XWSceneCtx *c) {
  int i;
  XWError serr;
  if (!c || !c->sounds || c->mute)
    return;
  memset(&serr, 0, sizeof(serr));
  for (i = 0; i < c->scene.n_objects; i++) {
    const XWSceneObject *o = &c->scene.objects[i];
    if (!c->sounds[i])
      continue;
    if (xw_sound_play(c->sounds[i], o->sound.volume, c->volume,
                      o->sound.loop, &serr) != 0)
      continue;
    if (c->paused)
      xw_sound_pause(c->sounds[i], 1);
  }
}

static void pause_sounds(XWSceneCtx *c, int paused) {
  int i;
  if (!c || !c->sounds)
    return;
  for (i = 0; i < c->scene.n_objects; i++)
    if (c->sounds[i])
      xw_sound_pause(c->sounds[i], paused);
}

static int init_emitters(XWSceneCtx *c, XWError *err) {
  int i;
  free_emitters(c);
  c->emitters = calloc(c->scene.n_objects > 0 ? (size_t)c->scene.n_objects : 1,
                       sizeof(XWEmitter));
  if (!c->emitters) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  for (i = 0; i < c->scene.n_objects; i++) {
    if (xw_emitter_init(&c->emitters[i], &c->scene.objects[i].particle) != 0) {
      XW_ERROR_SET(err, 1, "cannot init emitter %d", i);
      free_emitters(c);
      return -1;
    }
  }
  return 0;
}

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int paint_fill(XWSceneCtx *c);
static int render_overlays(XWSceneCtx *c, const float *alphas, XWError *rerr);
static int scene_list_props(XWBackend *b, char *out, size_t n);
static int scene_set_prop(XWBackend *b, const char *name, const char *value,
                          XWError *err);

static int load_script(XWSceneCtx *c, XWError *err) {
  char path[1152], *src = NULL;
  FILE *f;
  long sz;
  /* An engine exists when there is a script to run or props to resolve
   * and override (USER refs and --set-property need the override table
   * even on scriptless walls). Neither means zero scripting overhead. */
  if (!c->scene.script[0] && c->scene.n_props == 0)
    return 0;
  if (xw_script_create(&c->script, err) != 0)
    return -1;
  xw_script_set_scene(c->script, &c->scene);
  xw_script_set_props(c->script, c->scene.props, c->scene.n_props);
  if (!c->scene.script[0])
    return 0;
  snprintf(path, sizeof(path), "%.900s/%.200s", c->dir, c->scene.script);
  f = fopen(path, "rb");
  if (!f) {
    XW_ERROR_SET(err, 1, "cannot open script '%.200s'", c->scene.script);
    xw_script_destroy(c->script);
    c->script = NULL;
    return -1;
  }
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 1024 * 1024) {
    fclose(f);
    XW_ERROR_SET(err, 1, "bad script size");
    xw_script_destroy(c->script);
    c->script = NULL;
    return -1;
  }
  src = malloc((size_t)sz + 1);
  if (!src) {
    fclose(f);
    XW_ERROR_SET(err, 1, "out of memory");
    xw_script_destroy(c->script);
    c->script = NULL;
    return -1;
  }
  if (fread(src, 1, (size_t)sz, f) != (size_t)sz) {
    free(src);
    fclose(f);
    XW_ERROR_SET(err, 1, "cannot read script");
    xw_script_destroy(c->script);
    c->script = NULL;
    return -1;
  }
  fclose(f);
  src[sz] = '\0';
  if (xw_script_eval(c->script, src, c->scene.script, err) != 0) {
    free(src);
    xw_script_destroy(c->script);
    c->script = NULL;
    return -1;
  }
  free(src);
  return 0;
}

static void free_script(XWSceneCtx *c) {
  if (!c || !c->script)
    return;
  xw_script_destroy(c->script);
  c->script = NULL;
}

static int paint_gl(XWSceneCtx *c) {
  XWError rerr;
  float t;
  memset(&rerr, 0, sizeof(rerr));
  if (!c->gl) {
    uint64_t now = now_ms();
    if (now - c->last_gl_try_ms < 1000)
      return -1;
    c->last_gl_try_ms = now;
    if (xw_gl_create(&c->gl, c->win->dpy, c->win->win, (int)c->win->width,
                     (int)c->win->height, &rerr) != 0) {
      c->gl = NULL;
      return -1;
    }
  }
  t = (float)(now_ms() - c->t0_ms) / 1000.0f;
  {
    float *alphas = NULL;
    int n = c->scene.n_objects, rc = -1, i;
    if (n > 0) {
      alphas = malloc(sizeof(float) * (size_t)n);
      if (!alphas)
        return -1;
      for (i = 0; i < n; i++)
        alphas[i] = c->script ? xw_script_resolve_alpha(
                                    c->script, &c->scene.objects[i])
                              : 1.0f;
    }
    if (xw_scene_image_render(c->gl, &c->scene, c->dir, t, alphas, &rerr) == 0)
      rc = render_overlays(c, alphas, &rerr);
    free(alphas);
    if (rc != 0)
      return -1;
  }
  if (xw_gl_present(c->gl) != 0)
    return -1;
  c->frames++;
  c->last_ms = now_ms();
  return 0;
}

/* particle + text passes over the image layer (dt from frame pacing). */
static int render_overlays(XWSceneCtx *c, const float *alphas, XWError *rerr) {
  uint64_t now = now_ms();
  float dt = c->last_ms ? (float)(now - c->last_ms) / 1000.0f : 1.0f / 30.0f;
  int i;
  if (dt < 0.0f)
    dt = 0.0f;
  if (dt > 0.25f)
    dt = 0.25f;
  if (!c->emitters || !c->texts)
    return -1;
  for (i = 0; i < c->scene.n_objects; i++) {
    const XWSceneObject *o = &c->scene.objects[i];
    float a = alphas ? alphas[i] : 1.0f;
    if (!o->visible || o->type != XW_OBJ_PARTICLE)
      continue;
    if (xw_scene_particle_render(c->gl, o, &c->emitters[i], c->dir, dt, a,
                                 rerr) != 0)
      return -1;
  }
  /* text pass: per-type loops until the multi-object composite task;
   * object-order compositing comes with scene-space FBOs. */
  for (i = 0; i < c->scene.n_objects; i++) {
    const XWSceneObject *o = &c->scene.objects[i];
    float a = alphas ? alphas[i] : 1.0f;
    if (!o->visible || o->type != XW_OBJ_TEXT)
      continue;
    if (xw_scene_text_draw(c->gl, o, c->dir, &c->texts[i], a, rerr) != 0)
      return -1;
  }
  return 0;
}

static int paint(XWSceneCtx *c) {
  if (c->script)
    xw_script_tick(c->script);
  if (paint_gl(c) == 0)
    return 0;
  return paint_fill(c);
}

static int paint_fill(XWSceneCtx *c) {
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
  c->mute = opt->mute ? 1 : 0;
  c->volume = opt->volume;
  if (xw_scene_parse(c->dir, &c->scene, err) != 0) {
    free(c);
    return -1;
  }
  c->parsed = 1;
  if (init_emitters(c, err) != 0) {
    xw_scene_free(&c->scene);
    free(c);
    return -1;
  }
  c->texts = calloc(c->scene.n_objects > 0 ? (size_t)c->scene.n_objects : 1,
                    sizeof(XWTextItem));
  if (!c->texts) {
    XW_ERROR_SET(err, 1, "out of memory");
    free_emitters(c);
    xw_scene_free(&c->scene);
    free(c);
    return -1;
  }
  if (load_script(c, err) != 0) {
    free(c->texts);
    free_emitters(c);
    xw_scene_free(&c->scene);
    free(c);
    return -1;
  }
  init_sounds(c);
  c->gc = XCreateGC(win->dpy, win->win, 0, NULL);
  if (!c->gc) {
    XW_ERROR_SET(err, 1, "cannot create scene GC");
    free_script(c);
    free(c->texts);
    free_sounds(c);
    free_emitters(c);
    xw_scene_free(&c->scene);
    free(c);
    return -1;
  }
  b->priv = c;
  c->t0_ms = now_ms();
  init_sounds(c);
  if (paint(c) != 0) {
    XW_ERROR_SET(err, 1, "cannot paint scene background");
    return -1;
  }
  return 0;
}

static int scene_start(XWBackend *b) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  int r;
  if (!c || !c->parsed)
    return -1;
  r = paint(c);
  if (r == 0)
    start_sounds(c);
  return r;
}

static void scene_stop(XWBackend *b) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  if (c)
    pause_sounds(c, 1);
}

static void scene_destroy(XWBackend *b) {
  XWSceneCtx *c;
  if (!b || !(c = (XWSceneCtx *)b->priv))
    return;
  free_texts(c);
  if (c->gl)
    xw_gl_destroy(c->gl);
  c->gl = NULL;
  free_sounds(c);
  free_emitters(c);
  free_script(c);
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
  free_texts(c);
  free_sounds(c);
  free_script(c);
  xw_scene_free(&c->scene);
  c->scene = next;
  snprintf(c->dir, sizeof(c->dir), "%s", opt->path);
  c->mute = opt->mute ? 1 : 0;
  c->volume = opt->volume;
  if (init_emitters(c, err) != 0)
    return -1;
  init_sounds(c);
  c->texts = calloc(c->scene.n_objects > 0 ? (size_t)c->scene.n_objects : 1,
                    sizeof(XWTextItem));
  if (!c->texts) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  /* Degraded, not fatal: the new scene is already swapped in (reload keeps
   * the window alive by design); a broken script just leaves it scriptless.
   * Fresh sets fail hard in init instead so typos never land silently. */
  if (load_script(c, err) != 0)
    memset(err, 0, sizeof(*err));
  if (opt->fps > 0.0)
    c->fps = opt->fps;
  {
    int r = paint(c);
    if (r == 0 && !c->mute)
      start_sounds(c);
    return r;
  }
}

static void scene_set_paused(XWBackend *b, int paused) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  if (!c)
    return;
  c->paused = paused ? 1 : 0;
  pause_sounds(c, paused);
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
    .list_props = scene_list_props,
    .set_prop = scene_set_prop,
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

int xw_scene_has_gl(const XWBackend *b) {
  const XWSceneCtx *c = b ? (const XWSceneCtx *)b->priv : NULL;
  return (c && c->gl) ? 1 : 0;
}

int xw_scene_script_active(const XWBackend *b) {
  const XWSceneCtx *c = b ? (const XWSceneCtx *)b->priv : NULL;
  return (c && c->script) ? 1 : 0;
}

static int scene_list_props(XWBackend *b, char *out, size_t n) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  size_t off = 0;
  int i, count = 0;
  if (!c || !c->parsed || !out || n == 0)
    return -1;
  for (i = 0; i < c->scene.n_props; i++) {
    const XWUserProp *p = &c->scene.props[i];
    char value[256];
    int w;
    if (c->script && xw_script_get_user_prop(c->script, p->name, value,
                                             sizeof(value)) == 0) {
      /* override shadows the parsed value */
    } else {
      snprintf(value, sizeof(value), "%s", p->value);
    }
    if (strchr(p->name, '\t') || strchr(p->name, '\n') || strchr(value, '\n'))
      continue;
    w = snprintf(out + off, n - off, "%s\t%s\t%s\n", p->name, p->type,
                 value);
    if (w < 0 || (size_t)w >= n - off)
      return -1;
    off += (size_t)w;
    count++;
  }
  return count;
}

static int scene_set_prop(XWBackend *b, const char *name, const char *value,
                          XWError *err) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  int known = 0, i;
  if (!c || !c->parsed || !name || !name[0] || !value) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (!c->script) {
    XW_ERROR_SET(err, 1, "wallpaper has no user properties");
    return -1;
  }
  for (i = 0; i < c->scene.n_props; i++) {
    if (strcmp(c->scene.props[i].name, name) == 0) {
      known = 1;
      break;
    }
  }
  if (!known) {
    XW_ERROR_SET(err, 1, "unknown property '%.64s'", name);
    return -1;
  }
  if (xw_script_set_user_prop(c->script, name, value) != 0) {
    XW_ERROR_SET(err, 1, "cannot set property '%.64s'", name);
    return -1;
  }
  return 0;
}
