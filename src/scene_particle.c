#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_particle.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define XW_PARTICLE_RNG_SEED 0x9E3779B9u

static float next_unit(unsigned int *rng) {
  *rng = *rng * 1664525u + 1013904223u;
  return (float)(*rng >> 8) * (1.0f / 16777216.0f);
}

int xw_emitter_init(XWEmitter *e, const XWParticleCfg *cfg) {
  if (!e || !cfg)
    return -1;
  memset(e, 0, sizeof(*e));
  e->cfg = *cfg;
  if (e->cfg.max_count < 0)
    e->cfg.max_count = 0;
  if (e->cfg.life < 0.0f)
    e->cfg.life = 0.0f;
  if (e->cfg.rate < 0.0f)
    e->cfg.rate = 0.0f;
  e->parts = calloc(e->cfg.max_count > 0 ? (size_t)e->cfg.max_count : 1,
                    sizeof(XWParticle));
  if (!e->parts)
    return -1;
  e->rng = XW_PARTICLE_RNG_SEED;
  return 0;
}

void xw_emitter_free(XWEmitter *e) {
  if (!e)
    return;
  free(e->parts);
  e->parts = NULL;
  e->alive = 0;
  e->acc = 0.0f;
}

static void spawn_one(XWEmitter *e) {
  XWParticle *p = &e->parts[e->alive];
  float ang, sp;
  /* base direction is screen-up (0,-1); spread widens symmetrically */
  ang = (next_unit(&e->rng) * 2.0f - 1.0f) * e->cfg.spread;
  sp = e->cfg.speed;
  p->x = 0.0f;
  p->y = 0.0f;
  p->vx = sp * sinf(ang);
  p->vy = -sp * cosf(ang);
  p->age = 0.0f;
  p->life = e->cfg.life;
  p->size = e->cfg.size_min +
            next_unit(&e->rng) * (e->cfg.size_max - e->cfg.size_min);
  p->r = e->cfg.color[0];
  p->g = e->cfg.color[1];
  p->b = e->cfg.color[2];
  p->a = e->cfg.color[3];
  e->alive++;
}

int xw_emitter_update(XWEmitter *e, float dt) {
  int i, room;
  if (!e || !e->parts || dt < 0.0f)
    return -1;
  e->acc += e->cfg.rate * dt;
  room = e->cfg.max_count - e->alive;
  while (e->acc >= 1.0f && room > 0) {
    spawn_one(e);
    e->acc -= 1.0f;
    room--;
  }
  if (e->acc >= 1.0f)
    e->acc = 0.0f;
  for (i = 0; i < e->alive; i++) {
    XWParticle *p = &e->parts[i];
    p->vy += e->cfg.gravity * dt;
    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->age += dt;
    if (p->age > p->life) {
      e->alive--;
      if (i != e->alive)
        e->parts[i] = e->parts[e->alive];
      i--;
    }
  }
  return e->alive;
}

int xw_scene_particle_render(XWGl *g, const XWSceneObject *o, XWEmitter *e,
                             const char *dir, float dt, XWError *err) {
  char path[1152];
  unsigned int tex = 0, prog = 0;
  XWPoint *pts = NULL;
  int i, rc = -1;
  if (!g || !o || !e || !dir) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (xw_emitter_update(e, dt) < 0) {
    XW_ERROR_SET(err, 1, "emitter update failed");
    return -1;
  }
  if (e->alive <= 0)
    return 0;
  if (!o->particle.sprite[0]) {
    XW_ERROR_SET(err, 1, "particle object '%s' has no sprite", o->id);
    return -1;
  }
  snprintf(path, sizeof(path), "%s/%s", dir, o->particle.sprite);
  if (xw_gl_upload(g, path, &tex, err) != 0)
    return -1;
  if (xw_gl_program_points(g, &prog, err) != 0)
    return -1;
  pts = malloc(sizeof(XWPoint) * (size_t)e->alive);
  if (!pts) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  for (i = 0; i < e->alive; i++) {
    const XWParticle *p = &e->parts[i];
    float fade = p->life > 0.0f ? 1.0f - p->age / p->life : 0.0f;
    if (fade < 0.0f)
      fade = 0.0f;
    pts[i].x = o->origin[0] + p->x;
    pts[i].y = o->origin[1] + p->y;
    pts[i].size = p->size;
    pts[i].alpha = p->a * fade;
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
