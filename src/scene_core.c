#include <math.h>
#include <stdio.h>
#include <string.h>

#include "scene_core.h"

void xw_fbo_table_init(XWFboTable *t) {
  if (!t) return;
  memset(t, 0, sizeof(*t));
}

int xw_fbo_create(XWFboTable *t, const char *name, unsigned int w,
                  unsigned int h, float scale, XWGlId id) {
  if (!t || !name || !name[0] || w == 0 || h == 0) return -1;
  if (xw_fbo_find(t, name) != NULL) return -1;
  if (t->n >= XW_FBO_MAX) return -1;
  XWFbo *f = &t->fbos[t->n++];
  snprintf(f->name, sizeof(f->name), "%s", name);
  f->id = id;
  f->w = w;
  f->h = h;
  f->scale = scale;
  return 0;
}

int xw_fbo_alias(XWFboTable *t, const char *alias, const char *target) {
  const XWFbo *src;
  if (!t || !alias || !alias[0] || !target) return -1;
  src = xw_fbo_find(t, target);
  if (!src) return -1;
  return xw_fbo_create(t, alias, src->w, src->h, src->scale, src->id);
}

const XWFbo *xw_fbo_find(const XWFboTable *t, const char *name) {
  int i;
  if (!t || !name) return NULL;
  for (i = 0; i < t->n; i++)
    if (strncmp(t->fbos[i].name, name, XW_FBO_NAME_MAX) == 0) return &t->fbos[i];
  return NULL;
}

void xw_camera_ortho(float l, float r, float b, float tt, float *m16) {
  if (!m16) return;
  memset(m16, 0, 16 * sizeof(float));
  m16[0] = 2.0f / (r - l);
  m16[5] = 2.0f / (tt - b);
  m16[10] = -1.0f;
  m16[12] = -(r + l) / (r - l);
  m16[13] = -(tt + b) / (tt - b);
  m16[15] = 1.0f;
}

void xw_camera_auto_size(const XWSceneObject *objs, int n, float fb_w,
                         float fb_h, float *w, float *h) {
  float mw = 0.0f, mh = 0.0f;
  int i;
  if (!w || !h) return;
  for (i = 0; objs && i < n; i++) {
    float need_w = (fabsf(objs[i].origin[0]) + objs[i].size[0] / 2.0f) * 2.0f;
    float need_h = (fabsf(objs[i].origin[1]) + objs[i].size[1] / 2.0f) * 2.0f;
    if (need_w > mw) mw = need_w;
    if (need_h > mh) mh = need_h;
  }
  *w = mw > 0.0f ? mw : fb_w;
  *h = mh > 0.0f ? mh : fb_h;
}

void xw_blend_funcs(XWBlend m, int *srcRGB, int *dstRGB, int *srcA, int *dstA) {
  int sRGB = XW_GL_ONE, dRGB = XW_GL_ZERO;
  int sA = XW_GL_ONE, dA = XW_GL_ZERO;
  switch (m) {
  case XW_BLEND_TRANSLUCENT:
    sRGB = XW_GL_SRC_ALPHA;
    dRGB = XW_GL_ONE_MINUS_SRC_ALPHA;
    sA = XW_GL_ONE;
    dA = XW_GL_ONE_MINUS_SRC_ALPHA;
    break;
  case XW_BLEND_ADDITIVE:
    sRGB = XW_GL_SRC_ALPHA;
    dRGB = XW_GL_ONE;
    sA = XW_GL_SRC_ALPHA;
    dA = XW_GL_ONE;
    break;
  case XW_BLEND_NORMAL:
  default:
    break;
  }
  if (srcRGB) *srcRGB = sRGB;
  if (dstRGB) *dstRGB = dRGB;
  if (srcA) *srcA = sA;
  if (dstA) *dstA = dA;
}

int xw_bloom_enabled(const XWSceneCamera *cam) {
  return (cam && cam->bloom) ? 1 : 0;
}

int xw_core_present_uvs(float *u0, float *v0, float *u1, float *v1) {
  if (u0) *u0 = 0.0f;
  if (v0) *v0 = 0.0f;
  if (u1) *u1 = 1.0f;
  if (v1) *v1 = 1.0f;
  return 0;
}

float xw_particle_texture_ratio(int tex_w, int tex_h, int real_w, int real_h,
                                float frame_w, float frame_h, int spritesheet) {
  /* the engine's updateParticleRenderVars: a sprite sheet uses the atlas
   * dimensions scaled by the frame fractions (i.e. the per-frame aspect),
   * anything else the real texture's height/width */
  if (spritesheet && tex_w > 0 && tex_h > 0 && frame_w > 0.0f &&
      frame_h > 0.0f)
    return ((float)tex_h * frame_h) / ((float)tex_w * frame_w);
  return real_w > 0 ? (float)real_h / (float)real_w : 1.0f;
}

/* DFS post-order over `dependencies`: emit every dependency (recursively)
 * before the object itself. Mirrors CScene::addObjectToRenderOrder, which
 * also skips self-references, unknown ids and objects already emitted. */
static void render_order_visit(const XWSceneObject *objs, int n, int i,
                               int *order, int *n_out, unsigned char *done) {
  int d, k;
  if (done[i])
    return;
  done[i] = 1;
  for (d = 0; d < objs[i].n_deps; d++) {
    if (strcmp(objs[i].deps[d], objs[i].id) == 0)
      continue;
    for (k = 0; k < n; k++) {
      if (strcmp(objs[k].id, objs[i].deps[d]) == 0) {
        render_order_visit(objs, n, k, order, n_out, done);
        break;
      }
    }
  }
  order[(*n_out)++] = i;
}

int xw_scene_render_order(const XWSceneObject *objs, int n, int *order,
                          unsigned char *scratch) {
  int i, c = 0;
  if (!order || n < 0 || (n > 0 && (!objs || !scratch)))
    return -1;
  memset(scratch, 0, (size_t)n);
  for (i = 0; i < n; i++)
    render_order_visit(objs, n, i, order, &c, scratch);
  return c;
}

void xw_text_place(float ox, float oy, float ortho_w, float ortho_h, int win_w,
                   int win_h, float *dx, float *dy, float *scale) {
  float sx, sy;
  if (!dx || !dy || !scale)
    return;
  /* text anchors are canvas pixels with Y down (CText notes this differs
   * from the image path, which flips Y): scale them straight into the
   * window, no centring and no flip */
  sx = (ortho_w > 0.0f && win_w > 0) ? (float)win_w / ortho_w : 1.0f;
  sy = (ortho_h > 0.0f && win_h > 0) ? (float)win_h / ortho_h : 1.0f;
  *dx = ox * sx;
  *dy = oy * sy;
  /* glyphs rasterise at the canvas' vertical scale (the same squeeze the
   * camera applies to everything else) */
  *scale = sy;
}

void xw_scene_object_rect(const XWSceneObject *o, int tex_w, int tex_h,
                          float ortho_w, float ortho_h, int win_w, int win_h,
                          int *x, int *y, int *w, int *h) {
  float qw, qh, sx, sy, left, top;
  if (!o)
    return;
  qw = (float)(tex_w > 0 ? tex_w : win_w) * o->scale[0];
  qh = (float)(tex_h > 0 ? tex_h : win_h) * o->scale[1];
  sx = (ortho_w > 0.0f && win_w > 0) ? (float)win_w / ortho_w : 1.0f;
  sy = (ortho_h > 0.0f && win_h > 0) ? (float)win_h / ortho_h : 1.0f;
  left = (o->origin[0] - qw * 0.5f) * sx;
  top = (ortho_h - o->origin[1] - qh * 0.5f) * sy;
  if (x)
    *x = (int)floorf(left + 0.5f);
  if (y)
    *y = (int)floorf(top + 0.5f);
  if (w)
    *w = (int)floorf(qw * sx + 0.5f);
  if (h)
    *h = (int)floorf(qh * sy + 0.5f);
}

void xw_scene_object_ndc_rect(const XWSceneObject *o, float ortho_w,
                              float ortho_h, float ndc[8]) {
  float hw, hh, cx, cy;
  if (!ndc)
    return;
  if (!o || ortho_w <= 0.0f || ortho_h <= 0.0f) {
    ndc[0] = -1.0f;
    ndc[1] = -1.0f;
    ndc[2] = 1.0f;
    ndc[3] = -1.0f;
    ndc[4] = -1.0f;
    ndc[5] = 1.0f;
    ndc[6] = 1.0f;
    ndc[7] = 1.0f;
    return;
  }
  /* the scene's own size decides the quad here: a render-target material has
   * no texture dimensions to fall back on, and the engine uses the object's
   * declared size for it */
  hw = o->size[0] * o->scale[0] * 0.5f / (ortho_w * 0.5f);
  hh = o->size[1] * o->scale[1] * 0.5f / (ortho_h * 0.5f);
  cx = (o->origin[0] - ortho_w * 0.5f) / (ortho_w * 0.5f);
  cy = (o->origin[1] - ortho_h * 0.5f) / (ortho_h * 0.5f);
  ndc[0] = cx - hw;
  ndc[1] = cy - hh;
  ndc[2] = cx + hw;
  ndc[3] = cy - hh;
  ndc[4] = cx - hw;
  ndc[5] = cy + hh;
  ndc[6] = cx + hw;
  ndc[7] = cy + hh;
}
