#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <X11/Xlib.h>

#include "backend.h"
#include "backends.h"
#include "keybinds.h"
#include "props_store.h"
#include "scene.h"
#include "scene_gl.h"
#include "scene_image.h"
#include "scene_model.h"
#include "scene_parse.h"
#include "scene_particle.h"
#include "scene_particle_gl.h"
#include "scene_pkg.h"
#include "scene_shader.h"
#include "scene_script.h"
#include "scene_sound.h"
#include "scene_text.h"
#include "scene_text_gl.h"
#include "we_audio.h"
#include "we_pulse.h"
#include "x11.h"

/* Scene backend shell (Task 7): parses the wallpaper directory, paints the
 * window with the camera clear color, and repaints on an fps-throttled tick.
 * GL object rendering lands in Task 8; the tick/paused plumbing is final. */
typedef struct XWSceneCtx {
  XWWindow *win;
  XWScene scene;
  char dir[1024];
  char wallpaper_id[2048]; /* canonical opt->path (realpath), stable for .pkg tmpdir */
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
  int n_emitters;
  XWTextItem *texts;
  int n_texts;
  struct XWSound **sounds;
  int n_sounds;
  int sound_sys;
  int mute;
  double volume;
  XWScript *script;
  char tmpdir[1024];
  /* the engine's render order: object indices, dependencies first */
  int *order;
  unsigned char *order_scratch;
  int order_n;
  /* 1 while this context holds a reference to the shared Pulse monitor */
  int pulse_ref;
} XWSceneCtx;

static void free_order(XWSceneCtx *c) {
  free(c->order);
  free(c->order_scratch);
  c->order = NULL;
  c->order_scratch = NULL;
  c->order_n = 0;
  c->scene.order = NULL;
  c->scene.n_order = 0;
}

/* The engine's render order is static per scene: build it once after the
 * parse and reuse it every frame. */
static int build_order(XWSceneCtx *c, XWError *err) {
  int n = c->scene.n_objects;
  free_order(c);
  if (n <= 0)
    return 0;
  c->order = malloc(sizeof(int) * (size_t)n);
  c->order_scratch = malloc((size_t)n);
  if (!c->order || !c->order_scratch) {
    free_order(c);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  c->order_n =
      xw_scene_render_order(c->scene.objects, n, c->order, c->order_scratch);
  /* the scene owns the order so scripts (thisScene.sortLayer) always see the
   * live array, even after a rebuild */
  c->scene.order = c->order;
  c->scene.n_order = c->order_n;
  if (c->order_n < 0) {
    free_order(c);
    return -1;
  }
  return 0;
}

static void free_emitters(XWSceneCtx *c) {
  int i;
  if (!c || !c->emitters)
    return;
  /* count travels with the array: reload swaps scenes between alloc/free */
  for (i = 0; i < c->n_emitters; i++) xw_emitter_free(&c->emitters[i]);
  free(c->emitters);
  c->emitters = NULL;
  c->n_emitters = 0;
}

static void free_texts(XWSceneCtx *c) {
  int i;
  if (!c || !c->texts)
    return;
  for (i = 0; i < c->n_texts; i++)
    xw_text_item_destroy(c->gl, &c->texts[i]);
  free(c->texts);
  c->texts = NULL;
  c->n_texts = 0;
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
  c->n_sounds = c->scene.n_objects > 0 ? c->scene.n_objects : 1;
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
  for (i = 0; i < c->n_sounds; i++) {
    if (c->sounds[i])
      xw_sound_free(c->sounds[i]);
  }
  free(c->sounds);
  c->sounds = NULL;
  c->n_sounds = 0;
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
  c->n_emitters = c->scene.n_objects > 0 ? c->scene.n_objects : 1;
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
static int render_overlays(XWSceneCtx *c, float t, const float *alphas,
                           XWError *rerr);
static int scene_list_props(XWBackend *b, char *out, size_t n);
static int scene_set_prop(XWBackend *b, const char *name, const char *value,
                          XWError *err);
static int scene_clear_props(XWBackend *b, XWError *err);
static int scene_on_event(XWBackend *b, XEvent *ev);

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

static int unlink_cb(const char *path, const struct stat *st, int flag,
                     struct FTW *ftw) {
  (void)st;
  (void)flag;
  (void)ftw;
  remove(path);
  return 0;
}

static void free_tmpdir_path(char *tmpdir) {
  if (!tmpdir || !tmpdir[0])
    return;
  nftw(tmpdir, unlink_cb, 16, FTW_DEPTH | FTW_PHYS);
  rmdir(tmpdir);
  tmpdir[0] = '\0';
}

static void free_tmpdir(XWSceneCtx *c) {
  if (c)
    free_tmpdir_path(c->tmpdir);
}

/* Canonical wallpaper identity: realpath(opt path), fallback to raw path.
 * .pkg scenes extract to a fresh mkdtemp each init, so the resolved dir
 * is unstable; the opt path is the stable key. */
static void scene_wallpaper_id(const char *optpath, char *out, size_t n) {
  char *abs;
  if (!out || n == 0)
    return;
  out[0] = '\0';
  if (!optpath || !optpath[0])
    return;
  abs = realpath(optpath, NULL);
  if (abs) {
    snprintf(out, n, "%s", abs);
    free(abs);
  } else {
    snprintf(out, n, "%s", optpath);
  }
}

static int scene_prop_known(XWSceneCtx *c, const char *name) {
  int i;
  if (!c || !name)
    return 0;
  for (i = 0; i < c->scene.n_props; i++) {
    if (strcmp(c->scene.props[i].name, name) == 0)
      return 1;
  }
  return 0;
}

static void scene_disk_save(XWSceneCtx *c) {
  XWUserProp over[256];
  XWStoredProp s[256];
  int n = 0, i;
  if (!c || !c->wallpaper_id[0] || !c->script)
    return;
  if (xw_script_list_overrides(c->script, over, 256, &n) != 0 || n <= 0) {
    xw_props_store_save(c->wallpaper_id, NULL, 0);
    return;
  }
  for (i = 0; i < n; i++) {
    snprintf(s[i].name, sizeof(s[i].name), "%s", over[i].name);
    snprintf(s[i].type, sizeof(s[i].type), "%s", over[i].type);
    snprintf(s[i].value, sizeof(s[i].value), "%s", over[i].value);
  }
  xw_props_store_save(c->wallpaper_id, s, n);
}

static void scene_disk_load(XWSceneCtx *c) {
  XWStoredProp s[256];
  int n = 0, i;
  if (!c || !c->wallpaper_id[0] || !c->script)
    return;
  if (xw_props_store_load(c->wallpaper_id, s, 256, &n) != 0 || n <= 0)
    return;
  for (i = 0; i < n; i++) {
    if (!scene_prop_known(c, s[i].name))
      continue;
    (void)xw_script_set_user_prop(c->script, s[i].name, s[i].value);
  }
}

/* Copy a file byte for byte; returns -1 on any read/write/open failure. */
static int copy_file(const char *src, const char *dst) {
  char buf[8192];
  FILE *in = fopen(src, "rb");
  FILE *out;
  size_t got;
  if (!in)
    return -1;
  out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return -1;
  }
  while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, got, out) != got) {
      fclose(out);
      fclose(in);
      return -1;
    }
  }
  {
    int rc = ferror(in) ? -1 : 0;
    fclose(out);
    fclose(in);
    return rc;
  }
}

/* Resolve the asset directory: loose scene.json wins, otherwise unpack
 * scene.pkg into a temp dir (cleaned up at destroy). `dir` always points at
 * parsed assets, `tmpdir` is the tree to remove ("" when none); opt->path
 * stays the user's dir for --list. Outputs are written even on failure so a
 * caller can unwind what was created. */
static int resolve_dir(const char *optpath, char *dir, size_t dirlen,
                       char *tmpdir, size_t tmplen, XWError *err) {
  char probe[1152], pkgpath[1152];
  struct stat st;
  int loose;
  snprintf(dir, dirlen, "%s", optpath);
  tmpdir[0] = '\0';
  snprintf(probe, sizeof(probe), "%.512s/scene.json", optpath);
  loose = stat(probe, &st) == 0;
  snprintf(pkgpath, sizeof(pkgpath), "%.512s/scene.pkg", optpath);
  if (loose && stat(pkgpath, &st) != 0)
    return 0; /* a loose scene with no package: use the directory as it is */
  if (stat(pkgpath, &st) != 0) {
    XW_ERROR_SET(err, 1, "cannot open scene.json");
    return -1;
  }
  {
    XWPkg *pkg = NULL;
    char tmp[] = "/tmp/xw-scene-XXXXXX";
    if (!mkdtemp(tmp)) {
      XW_ERROR_SET(err, 1, "cannot make temp dir");
      return -1;
    }
    if (xw_pkg_open(pkgpath, &pkg) != 0 ||
        xw_pkg_extract_all(pkg, tmp) != 0) {
      if (pkg)
        xw_pkg_close(pkg);
      /* a partial extract leaves files behind, so a bare rmdir would leak
       * the whole tree; nftw it away instead */
      snprintf(tmpdir, tmplen, "%s", tmp);
      free_tmpdir_path(tmpdir);
      if (loose)
        return 0; /* a loose scene that stands on its own still works */
      XW_ERROR_SET(err, 1, "cannot unpack scene.pkg");
      return -1;
    }
    xw_pkg_close(pkg);
    /* project.json holds the wallpaper's user properties (the sliders) and
     * sits next to the .pkg rather than inside it: bring it along so the
     * {user, value} overrides resolve against the author's values */
    {
      char proj[1152], dst[1152];
      snprintf(proj, sizeof(proj), "%.512s/project.json", optpath);
      if (stat(proj, &st) == 0) {
        snprintf(dst, sizeof(dst), "%.1000s/project.json", tmp);
        (void)copy_file(proj, dst);
      }
    }
    /* the engine mounts the directory and the package together, so a loose
     * scene.json overrides the scene while the package still supplies the
     * models, materials and textures it references */
    if (loose) {
      char dst[1152];
      snprintf(dst, sizeof(dst), "%.1000s/scene.json", tmp);
      (void)copy_file(probe, dst);
    }
    snprintf(dir, dirlen, "%s", tmp);
    snprintf(tmpdir, tmplen, "%s", tmp);
  }
  return 0;
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
  /* The engine hands scene shaders g_AudioSpectrum{N}{Left,Right}. Only pay
   * for the monitor capture once a drawn pass has actually declared one, so
   * scenes without audio-reactive materials stay silent and idle. */
  {
    float bins[XW_WE_AUDIO_BINS];
    /* The monitor is a process-wide refcount shared with the other backends,
     * so hold our own reference and never release one we do not own. */
    if (xw_gl_audio_used(c->gl) && !c->pulse_ref && xw_we_pulse_start() == 0)
      c->pulse_ref = 1; /* failure just leaves a silent spectrum */
    if (c->pulse_ref && xw_we_pulse_get(bins) == 0)
      xw_gl_set_audio_spectrum(c->gl, bins);
    else
      xw_gl_set_audio_spectrum(c->gl, NULL);
  }
  /* XW_FIXED_TIME drives the clock from the frame counter so renders are
   * reproducible; timing noise otherwise swamps frame-to-frame A/Bs */
  if (getenv("XW_FIXED_TIME"))
    t = c->fps > 0.0f ? (float)c->frames / c->fps : (float)c->frames / 30.0f;
  else
    t = (float)(now_ms() - c->t0_ms) / 1000.0f;
  {
    float *alphas = NULL;
    int n = c->scene.n_objects, rc = -1, i;
    if (n > 0) {
      alphas = malloc(sizeof(float) * (size_t)n);
      if (!alphas)
        return -1;
      for (i = 0; i < n; i++) {
        /* scripted transform properties run once per frame, before the
         * object is drawn, so visibility and placement see this frame's
         * values */
        if (c->script)
          xw_script_apply_object_props(c->script, &c->scene.objects[i]);
        alphas[i] = c->script ? xw_script_resolve_alpha(
                                    c->script, &c->scene.objects[i])
                              : 1.0f;
      }
    }
    xw_gl_frame_begin(c->gl);
    if (xw_gl_bind_target(c->gl, -1) != 0)
      rc = -1;
    else {
      xw_gl_clear(c->gl, c->scene.camera.clearcolor[0],
                  c->scene.camera.clearcolor[1],
                  c->scene.camera.clearcolor[2]);
      rc = render_overlays(c, t, alphas, &rerr);
    }
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

/* One pass over the scene in the engine's dependency order: an object's
 * dependencies are drawn before it, and images, particles and text are
 * interleaved as the scene declares rather than grouped by type. */
static int render_overlays(XWSceneCtx *c, float t, const float *alphas,
                           XWError *rerr) {
  uint64_t now = now_ms();
  float dt = c->last_ms ? (float)(now - c->last_ms) / 1000.0f : 1.0f / 30.0f;
  if (getenv("XW_FIXED_TIME"))
    dt = c->fps > 0.0f ? 1.0f / c->fps : 1.0f / 30.0f;
  int k;
  if (dt < 0.0f)
    dt = 0.0f;
  if (dt > 0.25f)
    dt = 0.25f;
  /* engine.runtime / engine.frametime for this frame, before the scripted
   * object properties run below */
  if (c->script)
    xw_script_set_frame(c->script,
                        (double)xw_script_now_ms(c->script) / 1000.0,
                        (double)dt);
  if (!c->emitters || !c->texts || !c->order)
    return -1;
  for (k = 0; k < c->order_n; k++) {
    int i = c->order[k];
    const XWSceneObject *o = &c->scene.objects[i];
    float a = alphas ? alphas[i] : 1.0f;
    if (!o->visible)
      continue;
    if (o->type == XW_OBJ_IMAGE) {
      if (xw_scene_image_draw(c->gl, o, c->dir, &c->scene.camera, t, a,
                              rerr) < 0)
        return -1;
    } else if (o->type == XW_OBJ_PARTICLE) {
      if (xw_scene_particle_render_cam(c->gl, o, &c->emitters[i], c->dir, dt,
                                       a, rerr, &c->scene.camera) < 0)
        return -1;
    } else if (o->type == XW_OBJ_TEXT) {
      if (xw_scene_text_draw(c->gl, o, c->dir, &c->scene.camera, &c->texts[i],
                             a, rerr) < 0)
        return -1;
    }
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
  /* Wallpaper Engine's own assets (shaders/common.h): XW_ASSETS_DIR when it
   * is set, otherwise a Steam install auto-detected under $HOME. */
  {
    char assets[1024];
    if (xw_shader_resolve_assets_dir(getenv("XW_ASSETS_DIR"), getenv("HOME"),
                                     assets, sizeof(assets)) == 0) {
      xw_shader_set_assets_dir(assets);
      if (getenv("XW_DEBUG_DAEMON"))
        fprintf(stderr, "[scene] engine assets: %s\n", assets);
    }
  }
  if (resolve_dir(opt->path, c->dir, sizeof(c->dir), c->tmpdir,
                  sizeof(c->tmpdir), err) != 0) {
    free(c);
    return -1;
  }
  scene_wallpaper_id(opt->path, c->wallpaper_id, sizeof(c->wallpaper_id));
  c->fps = opt->fps > 0.0 ? opt->fps : XW_SCENE_FPS_DEFAULT;
  c->mute = opt->mute ? 1 : 0;
  c->volume = opt->volume;
  if (xw_scene_parse(c->dir, &c->scene, err) != 0) {
    free_tmpdir(c);
    free(c);
    return -1;
  }
  c->parsed = 1;
  if (build_order(c, err) != 0) {
    xw_scene_free(&c->scene);
    free_tmpdir(c);
    free(c);
    return -1;
  }
  if (init_emitters(c, err) != 0) {
    xw_scene_free(&c->scene);
    free_tmpdir(c);
    free(c);
    return -1;
  }
  c->texts = calloc(c->scene.n_objects > 0 ? (size_t)c->scene.n_objects : 1,
                    sizeof(XWTextItem));
  if (!c->texts) {
    XW_ERROR_SET(err, 1, "out of memory");
    free_emitters(c);
    xw_scene_free(&c->scene);
    free_tmpdir(c);
    free(c);
    return -1;
  }
  c->n_texts = c->scene.n_objects > 0 ? c->scene.n_objects : 1;
  if (load_script(c, err) != 0) {
    free_texts(c);
    free_emitters(c);
    xw_scene_free(&c->scene);
    free_tmpdir(c);
    free(c);
    return -1;
  }
  scene_disk_load(c);
  c->gc = XCreateGC(win->dpy, win->win, 0, NULL);
  if (!c->gc) {
    XW_ERROR_SET(err, 1, "cannot create scene GC");
    free_script(c);
    free_texts(c);
    free_sounds(c);
    free_emitters(c);
    xw_scene_free(&c->scene);
    free_tmpdir(c);
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
  scene_disk_save(c);
  free_texts(c);
  if (c->gl)
    xw_gl_destroy(c->gl);
  c->gl = NULL;
  if (c->pulse_ref) {
    xw_we_pulse_stop();
    c->pulse_ref = 0;
  }
  free_sounds(c);
  free_emitters(c);
  free_order(c);
  free_script(c);
  if (c->gc)
    XFreeGC(c->win->dpy, c->gc);
  xw_scene_free(&c->scene);
  free_tmpdir(c);
  free(c);
  b->priv = NULL;
}

static int scene_reload(XWBackend *b, const XWBackendOpt *opt, XWError *err) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  XWScene next;
  char dir[1024], tmpdir[1024], new_id[2048], old_id[2048];
  XWUserProp snap[256];
  int nsnap = 0, same = 0, i;
  if (!c || !c->parsed || !opt || !opt->path || !opt->path[0]) {
    XW_ERROR_SET(err, 1, "scene reload needs a wallpaper directory");
    return -1;
  }
  snprintf(old_id, sizeof(old_id), "%s", c->wallpaper_id);
  scene_wallpaper_id(opt->path, new_id, sizeof(new_id));
  same = old_id[0] && new_id[0] && strcmp(old_id, new_id) == 0;
  /* Snapshot live overrides before the script is freed; same-wallpaper
   * reload replays them, a switch persists them for later. */
  if (c->script)
    (void)xw_script_list_overrides(c->script, snap, 256, &nsnap);
  if (nsnap > 0 && old_id[0]) {
    XWStoredProp s[256];
    for (i = 0; i < nsnap; i++) {
      snprintf(s[i].name, sizeof(s[i].name), "%s", snap[i].name);
      snprintf(s[i].type, sizeof(s[i].type), "%s", snap[i].type);
      snprintf(s[i].value, sizeof(s[i].value), "%s", snap[i].value);
    }
    xw_props_store_save(old_id, s, nsnap);
  } else if (old_id[0]) {
    /* No live overrides: keep whatever the disk already has (do not delete,
     * the disk may hold overrides from an earlier window showing the same
     * wallpaper while this window never customized it). */
  }
  /* Stage the replacement before touching the live scene: a bad path or a
   * parse error must keep the old wallpaper up (app.c promises exactly
   * that), not blank the window. */
  memset(&next, 0, sizeof(next));
  dir[0] = '\0';
  tmpdir[0] = '\0';
  if (resolve_dir(opt->path, dir, sizeof(dir), tmpdir, sizeof(tmpdir), err) != 0)
    return -1;
  if (xw_scene_parse(dir, &next, err) != 0) {
    free_tmpdir_path(tmpdir);
    return -1;
  }
  free_texts(c);
  free_sounds(c);
  free_script(c);
  xw_scene_free(&c->scene);
  free_tmpdir(c);
  snprintf(c->dir, sizeof(c->dir), "%s", dir);
  snprintf(c->tmpdir, sizeof(c->tmpdir), "%s", tmpdir);
  snprintf(c->wallpaper_id, sizeof(c->wallpaper_id), "%s", new_id);
  c->scene = next;
  c->mute = opt->mute ? 1 : 0;
  c->volume = opt->volume;
  if (build_order(c, err) != 0)
    return -1;
  if (init_emitters(c, err) != 0)
    return -1;
  init_sounds(c);
  c->texts = calloc(c->scene.n_objects > 0 ? (size_t)c->scene.n_objects : 1,
                    sizeof(XWTextItem));
  if (!c->texts) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  c->n_texts = c->scene.n_objects > 0 ? c->scene.n_objects : 1;
  /* Degraded, not fatal: the new scene is already swapped in (reload keeps
   * the window alive by design); a broken script just leaves it scriptless.
   * Fresh sets fail hard in init instead so typos never land silently. */
  if (load_script(c, err) != 0)
    memset(err, 0, sizeof(*err));
  if (same && nsnap > 0 && c->script) {
    for (i = 0; i < nsnap; i++) {
      if (!scene_prop_known(c, snap[i].name))
        continue;
      (void)xw_script_set_user_prop(c->script, snap[i].name, snap[i].value);
    }
    scene_disk_save(c);
  } else if (!same) {
    scene_disk_load(c);
  }
  if (opt->fps > 0.0)
    c->fps = opt->fps;
  {
    int r = paint(c);
    if (r == 0 && !c->mute)
      start_sounds(c);
    return r;
  }
}

/* Re-apply the master level to every live sound object; a muted scene
 * drives them to zero rather than stopping them, so unmuting resumes the
 * same playback. */
static void scene_set_paused(XWBackend *b, int paused);

static void apply_sound_volumes(XWSceneCtx *c) {
  int i;
  double g;
  if (!c || !c->sounds)
    return;
  g = c->mute ? 0.0 : c->volume;
  for (i = 0; i < c->scene.n_objects; i++)
    if (c->sounds[i])
      xw_sound_set_volumes(c->sounds[i], c->scene.objects[i].sound.volume, g);
}

/* Runtime control shared by the key bindings and the volume/mute
 * subcommands. Scene wallpapers have sound objects, so volume/mute drive
 * those and pause also stops the animation, as a paused wallpaper should. */
static int scene_control(XWBackend *b, const char *name, const char *value,
                         XWError *err) {
  XWSceneCtx *c = (b && b->priv) ? (XWSceneCtx *)b->priv : NULL;
  int want, v;
  if (!c || !name) {
    XW_ERROR_SET(err, 1, "scene: not running");
    return -1;
  }
  if (strcmp(name, "volume") == 0) {
    if (xw_audio_apply((int)(c->volume + 0.5), value, &v) != 0) {
      XW_ERROR_SET(err, 1, XW_VOLUME_HINT);
      return -1;
    }
    c->volume = v;
    c->mute = 0;
    apply_sound_volumes(c);
    return 0;
  }
  if (strcmp(name, "mute") == 0 || strcmp(name, "pause") == 0) {
    int is_mute = strcmp(name, "mute") == 0;
    if (xw_ctl_toggle(is_mute ? c->mute : c->paused, value, &want) != 0) {
      XW_ERROR_SET(err, 1, "%s takes on, off or toggle", name);
      return -1;
    }
    if (is_mute) {
      c->mute = want;
      apply_sound_volumes(c);
    } else {
      scene_set_paused(b, want);
    }
    return 0;
  }
  XW_ERROR_SET(err, 1, "scene does not support '%s'", name);
  return -1;
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
    .on_event = scene_on_event,
    .control = scene_control,
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
    .clear_props = scene_clear_props,
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

struct XWScript *xw_scene_script(const XWBackend *b) {
  const XWSceneCtx *c = b ? (const XWSceneCtx *)b->priv : NULL;
  return (c && c->script) ? c->script : NULL;
}

static int scene_on_event(XWBackend *b, XEvent *ev) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  if (!c || !ev)
    return 0;
  /* Feed pointer position to scripts; never consume: x11 still forwards
   * the event to the root window so icons stay clickable. Coordinates
   * are root (screen) pixels, stable across wallpaper windows. */
  if (ev->type == MotionNotify && c->script) {
    xw_script_set_mouse(c->script, (float)ev->xmotion.x_root,
                        (float)ev->xmotion.y_root);
  }
  return 0;
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
  scene_disk_save(c);
  return 0;
}

static int scene_clear_props(XWBackend *b, XWError *err) {
  XWSceneCtx *c = b ? (XWSceneCtx *)b->priv : NULL;
  XWUserProp over[256];
  int n = 0, i;
  if (!c || !c->parsed) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (!c->script || c->scene.n_props <= 0) {
    XW_ERROR_SET(err, 1, "that wallpaper has no user properties");
    return -1;
  }
  if (xw_script_list_overrides(c->script, over, 256, &n) == 0) {
    for (i = 0; i < n; i++)
      (void)xw_script_clear_user_prop(c->script, over[i].name);
  }
  xw_props_store_save(c->wallpaper_id, NULL, 0);
  return 0;
}
