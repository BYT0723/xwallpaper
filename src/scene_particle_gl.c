#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_particle_gl.h"

int xw_scene_particle_render(XWGl *g, const XWSceneObject *o, XWEmitter *e,
                             const char *dir, float dt, float alpha,
                             XWError *err) {
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
    pts[i].alpha = p->a * fade * alpha;
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
