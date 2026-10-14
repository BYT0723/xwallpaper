#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_image.h"
#include "scene_particle_gl.h"
#include "scene_tex.h"

#include <math.h>

int xw_scene_particle_render(XWGl *g, const XWSceneObject *o, XWEmitter *e,
                             const char *dir, float dt, float alpha,
                             XWError *err) {
  return xw_scene_particle_render_cam(g, o, e, dir, dt, alpha, err, NULL);
}

int xw_scene_particle_render_cam(XWGl *g, const XWSceneObject *o, XWEmitter *e,
                                 const char *dir, float dt, float alpha,
                                 XWError *err, const XWSceneCamera *cam) {
  char path[1152];
  unsigned int tex = 0, prog = 0;
  XWPoint *pts = NULL;
  float speed = o->particle.seq_mult > 0.0f ? o->particle.seq_mult : 1.0f;
  int nframes = 0; /* filled once the sprite is uploaded */
  int i, rc = -1;
  if (!g || !o || !e || !dir) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  {
    int sw = 0, sh = 0;
    xw_gl_target_size(g, &sw, &sh);
    e->screen_w = (float)sw;
    e->screen_h = (float)sh;
  }
  if (xw_emitter_update(e, dt) < 0) {
    XW_ERROR_SET(err, 1, "emitter update failed");
    return -1;
  }
  if (e->alive <= 0)
    return 0;
  if (!o->particle.sprite[0])
    return 1;
  /* sprite refs are relative to the scene's materials/ directory in real
   * packages; synthetic scenes may store them next to the scene file */
  {
    char matdir[1152];
    snprintf(matdir, sizeof(matdir), "%.512s/materials", dir);
    if (xw_tex_resolve(matdir, NULL, o->particle.sprite, path,
                       sizeof(path)) != 0 &&
        xw_tex_resolve(dir, NULL, o->particle.sprite, path, sizeof(path)) != 0)
      return 1;
  }
  if (xw_gl_upload(g, path, &tex, err) != 0) {
    memset(err, 0, sizeof(*err));
    return 1;
  }
  nframes = xw_gl_texture_frame_count(g, tex);
  /* the engine runs the particle material's pass per quad (genericparticle
   * and friends); fall back to our simplified renderers when absent */
  if (o->particle.pshader[0]) {
    char shpath[1152];
    unsigned int texs[XW_GL_WE_SLOTS];
    unsigned int pp = 0, used = 0;
    XWSceneCombo combos[XW_SCENE_COMBOS_MAX + 4];
    float rv0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float rv1[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float *verts;
    int ntex = o->particle.n_ptextures, ti, nv = 0, ok = 1;
    memset(texs, 0, sizeof(texs));
    if (xw_scene_resolve_shader(dir, o->particle.pshader, shpath,
                                sizeof(shpath)) != 0)
      ok = 0;
    for (ti = 0; ok && ti < ntex && ti < XW_GL_WE_SLOTS; ti++) {
      xw_scene_resolve_texture(g, dir, o->particle.ptextures[ti], &texs[ti],
                               NULL);
      if (texs[ti])
        used |= 1u << ti;
    }
    /* spritetrail runs the shader's TRAILRENDERER path: it reads the
     * velocity from the thick vertex format (a_TexCoordVec4C1) and needs
     * the trail lengths in g_RenderVar0 plus the sprite aspect in
     * g_RenderVar1 (common_particles.h) */
    {
      int ncombos = o->particle.n_pcombos, k;
      if (ncombos < 0)
        ncombos = 0;
      if (ncombos > XW_SCENE_COMBOS_MAX)
        ncombos = XW_SCENE_COMBOS_MAX;
      memcpy(combos, o->particle.pcombos,
             sizeof(XWSceneCombo) * (size_t)ncombos);
      {
        const char *extra[3];
        int nextra = 0;
        int sheet = 0;
        /* the engine always runs particles through the thick vertex format
         * (velocity + lifetime in a_TexCoordVec4C1), which is our layout */
        extra[nextra++] = "THICKFORMAT";
        if (o->particle.has_trail) {
          extra[nextra++] = "TRAILRENDERER";
          rv0[0] = o->particle.trail_length;
          rv0[1] = o->particle.trail_max; /* length() clamp, see the header */
          rv0[2] = o->particle.trail_min;
        }
        /* an animated sprite is a sheet: the shader selects the frame from
         * g_RenderVar1 (frame w/h in UV, frame count, texture ratio) and the
         * quad's 0/1 corner UVs are mapped into it via step(0.001, uv) */
        if (nframes > 1) {
          float fr[4];
          int tw = 0, th = 0, rw = 0, rh = 0;
          if (xw_gl_texture_size(g, tex, &tw, &th, &rw, &rh) == 0 && tw > 0 &&
              th > 0 && xw_gl_texture_uv_rect(g, tex, 0.0f, fr) == 0 &&
              fr[2] > 0.0f && fr[3] > 0.0f) {
            /* only a grid atlas (frames smaller than the texture) is a
             * spritesheet: sequences that store one frame per texture are
             * switched by texture id and must keep sampling the whole one */
            if (fr[2] < 1.0f || fr[3] < 1.0f) {
              /* the cached rect is normalised UV already; g_RenderVar1 wants
               * the frame size in the same units */
              extra[nextra++] = "SPRITESHEET";
              sheet = 1;
              rv1[2] = (float)nframes;
              rv1[0] = fr[2];
              rv1[1] = fr[3];
            } else {
              nframes = 1;
            }
          }
        }
        {
          int tw = 0, th = 0, rw = 0, rh = 0;
          if (xw_gl_texture_size(g, tex, &tw, &th, &rw, &rh) == 0)
            /* the shader scales the trail's length axis by this; a sheet
             * wants its per-frame aspect, not the atlas' */
            rv1[3] = xw_particle_texture_ratio(tw, th, rw, rh, rv1[0], rv1[1],
                                               sheet);
        }
        for (k = 0; k < nextra; k++) {
          snprintf(combos[ncombos].key, sizeof(combos[ncombos].key), "%s",
                   extra[k]);
          snprintf(combos[ncombos].value, sizeof(combos[ncombos].value), "1");
          ncombos++;
        }
      }
      if (xw_gl_program_we(g, shpath, combos, ncombos, used, &pp, err) != 0)
        ok = 0;
    }
    if (ok) {
      /* samplers the material leaves empty fall back to the shader's own
       * annotated default; _rt_FullFrameBuffer is the scene so far (the
       * refractive rain multiplies its colour by it) */
      for (ti = 1; ti < XW_GL_WE_SLOTS; ti++) {
        unsigned int dt = 0;
        if (!texs[ti] &&
            xw_scene_sampler_default_tex(g, dir, pp, ti, &dt, NULL) == 0)
          texs[ti] = dt;
      }
      verts = malloc((size_t)e->alive * 6 * XW_PARTICLE_FLOATS *
                     sizeof(float));
      if (!verts) {
        XW_ERROR_SET(err, 1, "out of memory");
        return -1;
      }
      for (i = 0; i < e->alive; i++) {
        const XWParticle *p = &e->parts[i];
        static const float corner[4][2] = {
          {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        float nf = (float)(nframes > 0 ? nframes : 1);
        float life;
        int k;
        if (nframes > 1) {
          /* the shader recovers the frame as floor(frac(life) * numFrames) */
          float fi = 0.0f;
          if (strcmp(o->particle.animation_mode, "randomframe") == 0)
            fi = floorf(p->rnd * nf);
          else
            xw_gl_particle_frame(g, tex, p->age, speed, &fi);
          life = fi / nf;
        } else {
          life = (floorf(p->rnd * nf) + 0.5f) / nf;
        }
        for (k = 0; k < 6; k++) {
          const float *c = corner[idx[k]];
          float *v = verts + ((size_t)nv + (size_t)k) * XW_PARTICLE_FLOATS;
          v[0] = p->x;
          v[1] = p->y;
          v[2] = 0.0f;
          v[3] = c[0];
          v[4] = c[1];
          v[5] = p->rot;
          v[6] = p->size;
          v[7] = p->r;
          v[8] = p->g;
          v[9] = p->b;
          v[10] = p->a * alpha;
          v[11] = p->vx;
          v[12] = p->vy;
          v[13] = 0.0f;
          v[14] = life;
          v[15] = 0.0f;
          v[16] = 0.0f;
        }
        nv += 6;
      }
      if (xw_gl_blend_begin(g, o->particle.pblend) != 0)
        goto out_pq;
      {
        float persp[16];
        const float *mvp = NULL;
        if ((o->particle.flags & 4) != 0) {
          xw_gl_particle_perspective(g, cam ? cam->fov : 50.0f,
                                     cam ? cam->nearz : 0.01f,
                                     cam ? cam->farz : 10000.0f, persp);
          mvp = persp;
        }
        if (xw_gl_draw_particle_quads(g, pp, texs, XW_GL_WE_SLOTS, verts, nv,
                                      o->particle.pconsts,
                                      o->particle.n_pconsts, rv0, rv1,
                                      mvp) != 0) {
          xw_gl_blend_end(g);
          goto out_pq;
        }
      }
      xw_gl_blend_end(g);
      rc = 0;
    out_pq:
      free(verts);
      return rc;
    }
  }
  if (strcmp(o->particle.renderer, "spritetrail") == 0) {
    XWStreak *st;
    if (xw_gl_program_streaks(g, &prog, err) != 0)
      return -1;
    st = malloc(sizeof(XWStreak) * (size_t)e->alive);
    if (!st) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    for (i = 0; i < e->alive; i++) {
      const XWParticle *p = &e->parts[i];
      float sx = o->origin[0] + p->x;
      float sy = o->origin[1] - p->y;
      /* screen-space velocity (centered space is Y up) */
      float vx = p->vx, vy = -p->vy;
      float len = sqrtf(vx * vx + vy * vy);
      float tail = o->particle.trail_max;
      float fr = 0.0f;
      if (len > 1.0f) {
        st[i].dx = -vx / len * tail;
        st[i].dy = -vy / len * tail;
      } else {
        st[i].dx = 0.0f;
        st[i].dy = tail;
      }
      xw_gl_particle_frame(g, tex, p->age, speed, &fr);
      st[i].x = sx;
      st[i].y = sy;
      st[i].alpha = p->a * alpha;
      st[i].frame = fr;
    }
    if (xw_gl_blend_begin(g, XW_BLEND_TRANSLUCENT) != 0)
      goto out_st;
    if (xw_gl_draw_streaks(g, prog, tex, st, e->alive) != 0) {
      xw_gl_blend_end(g);
      goto out_st;
    }
    xw_gl_blend_end(g);
    rc = 0;
  out_st:
    free(st);
    return rc;
  }
  if (xw_gl_program_points(g, &prog, err) != 0)
    return -1;
  pts = malloc(sizeof(XWPoint) * (size_t)e->alive);
  if (!pts) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  for (i = 0; i < e->alive; i++) {
    const XWParticle *p = &e->parts[i];
    float fr = 0.0f;
    /* engine space is centered pixels with Y up; the object origin maps
     * back to screen pixels (Y down) for the point renderer */
    pts[i].x = o->origin[0] + p->x;
    pts[i].y = o->origin[1] - p->y;
    pts[i].size = p->size;
    pts[i].alpha = p->a * alpha; /* alphafade already applied by the emitter */
    pts[i].rot = p->rot;
    if (strcmp(o->particle.animation_mode, "randomframe") == 0) {
      /* stable per-particle pick across the sheet */
      int nf = nframes > 0 ? nframes : 1;
      pts[i].frame = (float)((int)(p->rnd * (float)nf) % nf);
    } else {
      xw_gl_particle_frame(g, tex, p->age, speed, &fr);
      pts[i].frame = fr;
    }
  }
  if (xw_gl_blend_begin(g, XW_BLEND_TRANSLUCENT) != 0)
    goto out;
  if (xw_gl_draw_points(g, prog, tex, pts, e->alive) != 0)
    goto out;
  rc = 0;
out:
  xw_gl_blend_end(g);
  free(pts);
  if (rc != 0)
    XW_ERROR_SET(err, 1, "particle draw failed");
  return rc;
}
