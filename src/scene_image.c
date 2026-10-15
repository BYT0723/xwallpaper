#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "scene_gl.h"
#include "scene_image.h"
#include "scene_shader.h"
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
  /* built-in effect shaders (genericparticle and friends) live in the
   * engine's asset directory when one is configured */
  if (xw_shader_assets_shader(ref, out, n) == 0)
    return 0;
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

static const XWSceneFbo *find_fbo(const XWSceneEffect *fx, const char *name) {
  int i;
  if (!fx || !name || !name[0])
    return NULL;
  for (i = 0; i < fx->n_fbos; i++)
    if (strcmp(fx->fbos[i].name, name) == 0)
      return &fx->fbos[i];
  return NULL;
}

/* Resolve the shader's annotated `default` for a material's empty sampler
 * slot: `_rt_FullFrameBuffer` is the scene accumulated so far, other
 * `_rt_`/`_alias_` names belong to the pass binds, and everything else
 * resolves like a texture reference. Returns 0 and fills tex/res. */
int xw_scene_sampler_default_tex(XWGl *gl, const char *dir, unsigned int prog,
                                 int slot, unsigned int *tex, float res[4]) {
  char uname[32], dref[64];
  XWWeSlot tmp;
  if (!gl || !tex)
    return -1;
  *tex = 0;
  snprintf(uname, sizeof(uname), "g_Texture%d", slot);
  if (xw_gl_program_sampler_default(gl, prog, uname, dref, sizeof(dref)) != 0 ||
      !dref[0])
    return -1;
  if (strcmp(dref, "_rt_FullFrameBuffer") == 0)
    return xw_gl_framebuffer_snapshot(gl, tex);
  if (strncmp(dref, "_rt_", 4) == 0 || strncmp(dref, "_alias_", 7) == 0)
    return -1;
  memset(&tmp, 0, sizeof(tmp));
  fill_slot(gl, dir, dref, &tmp);
  if (!tmp.tex)
    return -1;
  *tex = tmp.tex;
  if (res)
    memcpy(res, tmp.res, sizeof(tmp.res));
  return 0;
}

int xw_scene_resolve_shader(const char *dir, const char *ref, char *out,
                            size_t n) {
  return resolve_shader(dir, ref, out, n);
}

void xw_scene_resolve_texture(XWGl *gl, const char *dir, const char *ref,
                              unsigned int *tex, float res[4]) {
  XWWeSlot slot;
  fill_slot(gl, dir, ref, &slot);
  if (tex)
    *tex = slot.tex;
  if (res)
    memcpy(res, slot.res, sizeof(slot.res));
}

/* Returns 0 drawn, 1 skipped (missing texture), -1 frame error.
 * Effect passes that cannot resolve or compile degrade to the base
 * image (or last good pass) instead of dropping the object. */
static int render_object(XWGl *gl, const XWSceneObject *o, const char *dir,
                         const XWSceneCamera *cam, float time, float alpha,
                         XWError *err) {
  char path[1152];
  char shpath[1152];
  unsigned int tex = 0, prog = 0, comp = 0, src = 0, blendp = 0, shot = 0;
  int i, e, dst = 1, ffb = 0;
  /* Photoshop-style blends the corpus actually uses; anything else stays a
   * normal alpha composite for now */
  int cblend = o->color_blend == 2 || o->color_blend == 6 || o->color_blend == 7;
  if (o->texture[0]) {
    snprintf(path, sizeof(path), "%.512s/%.400s", dir, o->texture);
  } else if (o->mat_tex[0] && strncmp(o->mat_tex, "_rt_", 4) == 0) {
    /* a material sampling a render target: _rt_FullFrameBuffer is the scene
     * accumulated so far. Such passthrough layers keep their quad full-screen
     * but see the scene through their own rectangle (composelayer.vert reads
     * a_Position for v_ScreenCoord); other targets stay skipped. */
    if (strcmp(o->mat_tex, "_rt_FullFrameBuffer") != 0)
      return 1;
    ffb = 1;
  } else if (o->mat_tex[0]) {
    /* real walls: texture name resolved against the material dir */
    if (xw_tex_resolve(dir, o->material, o->mat_tex, path, sizeof(path)) != 0)
      return 1;
  } else {
    return 1;
  }
  if (ffb) {
    if (xw_gl_framebuffer_snapshot(gl, &tex) != 0)
      return 1;
  } else if (xw_gl_upload(gl, path, &tex, err) != 0) {
    /* undecodable texture skips the object, not the frame */
    memset(err, 0, sizeof(*err));
    return 1;
  }
  if (xw_gl_program_copy(gl, &prog, err) != 0)
    return -1;
  if (xw_gl_program_composite(gl, &comp, err) != 0)
    return -1;
  if (cblend && xw_gl_program_blend(gl, &blendp, err) != 0)
    return -1;
  /* base copy always lands in an FBO; the composite pass owns the window
   * so resolved alpha modulates arbitrary effect chains uniformly. */
  if (xw_gl_bind_target(gl, 0) != 0)
    return -1;
  if (ffb) {
    if (xw_gl_draw_window_source(gl, prog, tex, time) != 0)
      return -1;
  } else {
    /* Placement: the engine sizes the quad from the texture's real size times
     * scale and centres it on origin, and the object covers only that rect.
     * Clearing to transparent first is what keeps the previous object from
     * showing around it (and from being composited a second time). */
    int tw2 = 0, th2 = 0, rw2 = 0, rh2 = 0, ww = 0, wh = 0;
    int rx = 0, ry = 0, rw = 0, rh = 0, mrc = 1;
    xw_gl_texture_size(gl, tex, &tw2, &th2, &rw2, &rh2);
    xw_gl_target_size(gl, &ww, &wh);
    xw_scene_object_rect(o, rw2, rh2, cam ? cam->ortho_w : 0.0f,
                         cam ? cam->ortho_h : 0.0f, ww, wh, &rx, &ry, &rw, &rh);
    if (xw_gl_clear_rgba(gl, 0.0f, 0.0f, 0.0f, 0.0f) != 0)
      return -1;
    if (o->puppet[0]) {
      char mpath[1152];
      const float sx = (float)(rw2 > 0 ? rw2 : (int)o->size[0]);
      const float sy = (float)(rh2 > 0 ? rh2 : (int)o->size[1]);
      snprintf(mpath, sizeof(mpath), "%.512s/%.400s", dir, o->puppet);
      mrc = xw_gl_draw_mesh(gl, mpath, sx, sy, rx, ry, rw, rh, prog, tex,
                            time, err);
      if (mrc < 0)
        return -1;
    }
    if (mrc != 0 && xw_gl_draw_rect_tint(gl, prog, tex, time, o->color, rx, ry,
                                         rw, rh, o->scale[0] < 0.0f,
                                         o->scale[1] < 0.0f) != 0)
      return -1; /* unusable mesh: keep the quad rather than nothing */
  }
  if (xw_gl_fbo_texture(gl, 0, &src) != 0)
    return -1;
  if (ffb) {
    float ndc[8];
    xw_scene_object_ndc_rect(o, cam ? cam->ortho_w : 0.0f,
                             cam ? cam->ortho_h : 0.0f, ndc);
    xw_gl_set_shader_quad(gl, ndc);
  }
  for (e = 0; e < o->n_effects; e++) {
    const XWSceneEffect *fx = &o->effects[e];
    int broken = 0;
    /* "previous" binds resolve to the texture that entered the current
     * offscreen-target sequence (see the engine's configurePassTarget) */
    unsigned int effect_input = 0;
    int in_seq = 0;
    if (!fx->visible)
      continue;
    for (i = 0; i < fx->n_passes; i++) {
      unsigned int ep = 0;
      const XWScenePass *pass = &fx->passes[i];
      const char *sh = pass->shader;
      XWWeSlot slots[XW_GL_WE_SLOTS];
      float input_res[4];
      int ww = 0, hh = 0, t, b;
      unsigned int texture_slots = 0;
      int writes_target = 0;
      /* builtin shader names (genericimage*) need no file */
      if (!sh[0] || strchr(sh, '/') == NULL)
        continue;
      /* bound sampler slots: the engine enables a sampler's combo (MASK
       * and friends) only when its texture is actually present */
      {
        unsigned int used = 0;
        for (t = 0; t < pass->n_textures && t < XW_GL_WE_SLOTS; t++)
          if (pass->textures[t][0])
            used |= 1u << t;
        for (b = 0; b < pass->n_binds; b++)
          if (pass->binds[b].slot >= 0 && pass->binds[b].slot < XW_GL_WE_SLOTS)
            used |= 1u << pass->binds[b].slot;
        texture_slots = used;
      }
      if (resolve_shader(dir, sh, shpath, sizeof(shpath)) != 0 ||
          xw_gl_program_we(gl, shpath, pass->combos, pass->n_combos,
                           texture_slots, &ep, err) != 0) {
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
        /* A pass that declares an engine-managed render target we do not
         * produce (_rt_imageLayerComposite_* and friends, which the engine
         * builds per image object) must not fall back to the sampler's
         * annotated `default`: that default is a util texture, usually white,
         * and blending the layer against it painted whole frames white.
         * Keep the base image instead. _rt_FullFrameBuffer is the one render
         * target we do produce, and a bind naming the target counts as
         * producible too. */
        if (t < pass->n_textures && pass->textures[t][0] &&
            (strncmp(pass->textures[t], "_rt_", 4) == 0 ||
             strncmp(pass->textures[t], "_alias_", 7) == 0) &&
            strcmp(pass->textures[t], "_rt_FullFrameBuffer") != 0) {
          int provided = 0, bi;
          for (bi = 0; bi < pass->n_binds; bi++)
            if (strcmp(pass->binds[bi].name, pass->textures[t]) == 0) {
              provided = 1;
              break;
            }
          if (!provided) {
            memset(err, 0, sizeof(*err));
            broken = 1;
            break;
          }
        }
        if (t < pass->n_textures)
          fill_slot(gl, dir, pass->textures[t], &slots[t]);
        else
          memset(&slots[t], 0, sizeof(slots[t]));
        /* the engine also loads a sampler's annotated `default` texture
         * (util/perlin_256, util/white, gradients) when the material does
         * not provide one; named render targets come from pass binds */
        if (!slots[t].tex) {
          unsigned int dt = 0;
          float dr[4];
          if (xw_scene_sampler_default_tex(gl, dir, ep, t, &dt, dr) == 0) {
            slots[t].tex = dt;
            memcpy(slots[t].res, dr, sizeof(dr));
          }
        }
      }
      if (broken)
        break;
      /* destination: a named offscreen target, else the ping-pong chain */
      if (pass->target[0]) {
        const XWSceneFbo *f = find_fbo(fx, pass->target);
        unsigned int tt = 0;
        if (xw_gl_named_target(gl, pass->target, f ? f->width : 0,
                               f ? f->height : 0, f ? f->format : NULL,
                               f ? f->uvs : NULL, &tt) != 0 ||
            xw_gl_bind_named(gl, pass->target) != 0) {
          memset(err, 0, sizeof(*err));
          broken = 1;
          break;
        }
        if (!in_seq) {
          effect_input = src;
          in_seq = 1;
        }
        writes_target = 1;
      } else if (xw_gl_bind_target(gl, dst) != 0) {
        return -1;
      }
      /* binds remap a sampler to a named target or the sequence input */
      for (b = 0; b < pass->n_binds; b++) {
        int slot = pass->binds[b].slot;
        unsigned int bt = 0;
        if (slot < 0 || slot >= XW_GL_WE_SLOTS)
          continue;
        if (strcmp(pass->binds[b].name, "previous") == 0) {
          bt = (in_seq && effect_input) ? effect_input : src;
        } else {
          const XWSceneFbo *f = find_fbo(fx, pass->binds[b].name);
          if (xw_gl_named_target(gl, pass->binds[b].name, f ? f->width : 0,
                                 f ? f->height : 0, f ? f->format : NULL,
                                 f ? f->uvs : NULL, &bt) != 0)
            continue;
        }
        if (bt) {
          int tw2 = 0, th2 = 0, rw2 = 0, rh2 = 0;
          slots[slot].tex = bt;
          if (xw_gl_texture_size(gl, bt, &tw2, &th2, &rw2, &rh2) == 0) {
            slots[slot].res[0] = (float)(tw2 > 0 ? tw2 : 1);
            slots[slot].res[1] = (float)(th2 > 0 ? th2 : 1);
            slots[slot].res[2] = (float)(rw2 > 0 ? rw2 : tw2);
            slots[slot].res[3] = (float)(rh2 > 0 ? rh2 : th2);
          }
        }
      }
      if (xw_gl_draw_we(gl, ep, src, input_res, slots, pass->consts,
                        pass->n_consts, time) != 0)
        return -1;
      if (writes_target) {
        unsigned int tt = 0;
        if (xw_gl_named_target(gl, pass->target, 0, 0, NULL, NULL, &tt) != 0)
          return -1;
        src = tt;
      } else {
        if (xw_gl_fbo_texture(gl, dst, &src) != 0)
          return -1;
        dst = 1 - dst;
        in_seq = 0;
        effect_input = 0;
      }
    }
    if (broken)
      break;
  }
  if (ffb)
    xw_gl_set_shader_quad(gl, NULL);
  if (cblend) {
    /* the blend reads the screen so far, so snapshot the window before the
     * object lands on it, then write the formula result through */
    if (xw_gl_framebuffer_snapshot(gl, &shot) != 0)
      return -1;
    if (xw_gl_bind_target(gl, -1) != 0)
      return -1;
    if (xw_gl_draw_blend(gl, blendp, src, shot, alpha, o->color_blend) != 0)
      return -1;
    return 0;
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

/* Draw a single image object. The engine's dependency order decides when it
 * is called; the caller has already bound the destination and cleared. */
int xw_scene_image_draw(XWGl *gl, const XWSceneObject *o, const char *dir,
                        const XWSceneCamera *cam, float time, float alpha,
                        XWError *err) {
  if (!gl || !o || !dir)
    return -1;
  if (!o->visible || o->type != XW_OBJ_IMAGE)
    return 0;
  if (!o->texture[0] && !o->mat_tex[0])
    return 0;
  return render_object(gl, o, dir, cam, time, alpha, err);
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
    if (!o->visible || o->type != XW_OBJ_IMAGE)
      continue;
    if (!o->texture[0] && !o->mat_tex[0])
      continue;
    if (xw_scene_image_draw(gl, o, dir, &scene->camera, time, a, err) < 0)
      return -1;
  }
  return 0;
}
