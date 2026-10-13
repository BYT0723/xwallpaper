#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_particle_gl.h"
#include "scene_tex.h"

#include <math.h>

int xw_scene_particle_render(XWGl *g, const XWSceneObject *o, XWEmitter *e,
                             const char *dir, float dt, float alpha,
                             XWError *err) {
  char path[1152];
  unsigned int tex = 0, prog = 0;
  XWPoint *pts = NULL;
  float speed = o->particle.seq_mult > 0.0f ? o->particle.seq_mult : 1.0f;
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
    xw_gl_particle_frame(g, tex, p->age, speed, &fr);
    pts[i].frame = fr;
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
