#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "scene_gl.h"
#include "scene_image.h"
#include "scene_tex.h"

static int resolve_shader(const char *dir, const char *ref, char *out,
                          size_t n) {
  static const char *exts[] = {"", ".frag"};
  static const char *roots[] = {"", "shaders/"};
  char base[1152];
  size_t i, r;
  struct stat st;
  int prefixed;
  if (!dir || !ref || !ref[0] || !out || n == 0)
    return -1;
  if (strstr(ref, "..") != NULL)
    return -1;
  /* material shader names ("effects/shake") omit the shaders/ root dir */
  prefixed = strncmp(ref, "shaders/", 8) == 0;
  for (r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
    if (r > 0 && prefixed)
      break;
    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
      snprintf(base, sizeof(base), "%.512s/%.64s%.400s%s", dir, roots[r], ref,
               exts[i]);
      if (stat(base, &st) == 0 && S_ISREG(st.st_mode)) {
        snprintf(out, n, "%.1000s", base);
        return 0;
      }
    }
  }
  return -1;
}

/* Resolve + upload one pass texture ref into a WE slot. Empty refs and
 * failures leave tex=0 (draw falls back to the pass input). Pass texture
 * refs are relative to the scene's materials/ directory. */
static void fill_slot(XWGl *gl, const char *dir, const char *ref,
                      XWWeSlot *slot) {
  char path[1152];
  char matdir[1152];
  XWError err;
  unsigned int t = 0;
  int tw = 0, th = 0, rw = 0, rh = 0;
  memset(slot, 0, sizeof(*slot));
  if (!ref || !ref[0])
    return;
  memset(&err, 0, sizeof(err));
  if (strncmp(ref, "util/", 5) == 0) {
    float res[4];
    if (xw_gl_util_texture(gl, ref, &t, res) != 0)
      return;
    slot->tex = t;
    memcpy(slot->res, res, sizeof(res));
    return;
  }
  if (strstr(ref, "..") != NULL)
    return;
  snprintf(matdir, sizeof(matdir), "%.512s/materials", dir);
  /* real packages resolve pass refs against materials/; synthetic scenes
   * keep them beside the scene file, so fall back to the scene root */
  if (xw_tex_resolve(matdir, NULL, ref, path, sizeof(path)) != 0 &&
      xw_tex_resolve(dir, NULL, ref, path, sizeof(path)) != 0)
    return;
  if (xw_gl_upload(gl, path, &t, &err) != 0 || !t)
    return;
  if (xw_gl_texture_size(gl, t, &tw, &th, &rw, &rh) != 0)
    return;
  slot->tex = t;
  slot->res[0] = (float)(tw > 0 ? tw : 1);
  slot->res[1] = (float)(th > 0 ? th : 1);
  slot->res[2] = (float)(rw > 0 ? rw : tw);
  slot->res[3] = (float)(rh > 0 ? rh : th);
}

/* Returns 0 drawn, 1 skipped (missing texture), -1 frame error.
 * Effect passes that cannot resolve or compile degrade to the base
 * image (or last good pass) instead of dropping the object. */
static int render_object(XWGl *gl, const XWSceneObject *o, const char *dir,
                         float time, float alpha, XWError *err) {
  char path[1152];
  char shpath[1152];
  unsigned int tex = 0, prog = 0, comp = 0, src = 0;
  int i, e, dst = 1;
  if (o->texture[0]) {
    snprintf(path, sizeof(path), "%.512s/%.400s", dir, o->texture);
  } else if (o->mat_tex[0]) {
    /* real walls: texture name resolved against the material dir */
    if (xw_tex_resolve(dir, o->material, o->mat_tex, path, sizeof(path)) != 0)
      return 1;
  } else {
    return 1;
  }
  if (xw_gl_upload(gl, path, &tex, err) != 0) {
    /* undecodable texture skips the object, not the frame */
    memset(err, 0, sizeof(*err));
    return 1;
  }
  if (xw_gl_program_copy(gl, &prog, err) != 0)
    return -1;
  if (xw_gl_program_composite(gl, &comp, err) != 0)
    return -1;
  /* base copy always lands in an FBO; the composite pass owns the window
   * so resolved alpha modulates arbitrary effect chains uniformly. */
  if (xw_gl_bind_target(gl, 0) != 0)
    return -1;
  if (xw_gl_draw(gl, prog, tex, time) != 0)
    return -1;
  if (xw_gl_fbo_texture(gl, 0, &src) != 0)
    return -1;
  for (e = 0; e < o->n_effects; e++) {
    int broken = 0;
    if (!o->effects[e].visible)
      continue;
    for (i = 0; i < o->effects[e].n_passes; i++) {
      unsigned int ep = 0;
      XWScenePass *pass = &o->effects[e].passes[i];
      const char *sh = pass->shader;
      XWWeSlot slots[XW_GL_WE_SLOTS];
      float input_res[4];
      int ww = 0, hh = 0, t;
      /* offscreen render-target passes are not routed yet; running one in
       * the main chain would overwrite the image, so degrade the effect */
      if (pass->target[0]) {
        broken = 1;
        break;
      }
      /* builtin shader names (genericimage*) need no file */
      if (!sh[0] || strchr(sh, '/') == NULL) {
        /* empty slot: passthrough to the next pass */
        continue;
      }
      if (resolve_shader(dir, sh, shpath, sizeof(shpath)) != 0 ||
          xw_gl_program_we(gl, shpath, pass->combos, pass->n_combos, &ep,
                           err) != 0) {
        /* unknown shader or untranslatable dialect: keep the base
         * image (or last good pass) instead of dropping the object */
        memset(err, 0, sizeof(*err));
        broken = 1;
        break;
      }
      if (xw_gl_target_size(gl, &ww, &hh) != 0 || ww <= 0 || hh <= 0) {
        memset(err, 0, sizeof(*err));
        broken = 1;
        break;
      }
      input_res[0] = (float)ww;
      input_res[1] = (float)hh;
      input_res[2] = (float)ww;
      input_res[3] = (float)hh;
      for (t = 0; t < XW_GL_WE_SLOTS; t++) {
        if (t < pass->n_textures)
          fill_slot(gl, dir, pass->textures[t], &slots[t]);
        else
          memset(&slots[t], 0, sizeof(slots[t]));
      }
      if (xw_gl_bind_target(gl, dst) != 0)
        return -1;
      if (xw_gl_draw_we(gl, ep, src, input_res, slots, pass->consts,
                        pass->n_consts, time) != 0)
        return -1;
      if (xw_gl_fbo_texture(gl, dst, &src) != 0)
        return -1;
      dst = 1 - dst;
    }
    if (broken)
      break;
  }
  if (xw_gl_blend_begin(gl, XW_BLEND_TRANSLUCENT) != 0)
    return -1;
  if (xw_gl_bind_target(gl, -1) != 0) {
    xw_gl_blend_end(gl);
    return -1;
  }
  if (xw_gl_draw_alpha(gl, comp, src, time, alpha) != 0) {
    xw_gl_blend_end(gl);
    return -1;
  }
  xw_gl_blend_end(gl);
  return 0;
}

int xw_scene_image_render(XWGl *gl, const XWScene *scene, const char *dir,
                          float time, const float *alphas, XWError *err) {
  int i;
  if (!gl || !scene || !dir) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (xw_gl_bind_target(gl, -1) != 0)
    return -1;
  xw_gl_clear(gl, scene->camera.clearcolor[0], scene->camera.clearcolor[1],
              scene->camera.clearcolor[2]);
  for (i = 0; i < scene->n_objects; i++) {
    const XWSceneObject *o = &scene->objects[i];
    float a = alphas ? alphas[i] : 1.0f;
    int r;
    if (!o->visible || o->type != XW_OBJ_IMAGE)
      continue;
    if (!o->texture[0] && !o->mat_tex[0])
      continue;
    r = render_object(gl, o, dir, time, a, err);
    if (r < 0)
      return -1;
  }
  return 0;
}
