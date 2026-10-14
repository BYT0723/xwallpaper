#ifndef XW_SCENE_CORE_H
#define XW_SCENE_CORE_H

#include "scene_model.h"

/* GL object id without pulling GL headers (core stays unit-testable). */
typedef unsigned int XWGlId;

#define XW_FBO_NAME_MAX 64
#define XW_FBO_MAX 32

typedef struct XWFbo {
  char name[XW_FBO_NAME_MAX];
  XWGlId id;
  unsigned int w;
  unsigned int h;
  float scale;
} XWFbo;

typedef struct XWFboTable {
  XWFbo fbos[XW_FBO_MAX];
  int n;
} XWFboTable;

void xw_fbo_table_init(XWFboTable *t);
int xw_fbo_create(XWFboTable *t, const char *name, unsigned int w,
                  unsigned int h, float scale, XWGlId id);
int xw_fbo_alias(XWFboTable *t, const char *alias, const char *target);
const XWFbo *xw_fbo_find(const XWFboTable *t, const char *name);

void xw_camera_ortho(float l, float r, float b, float tt, float *m16);
void xw_camera_auto_size(const XWSceneObject *objs, int n, float fb_w,
                         float fb_h, float *w, float *h);

/* blend enum lives in scene_model.h */

#define XW_GL_ONE 1
#define XW_GL_ZERO 0
#define XW_GL_SRC_ALPHA 0x0302
#define XW_GL_ONE_MINUS_SRC_ALPHA 0x0303

void xw_blend_funcs(XWBlend m, int *srcRGB, int *dstRGB, int *srcA, int *dstA);

int xw_bloom_enabled(const XWSceneCamera *cam);

/* g_RenderVar1.w: the sprite's per-frame aspect ratio, as the engine's
 * updateParticleRenderVars computes it (atlasH*frameH)/(atlasW*frameW) for a
 * sprite sheet and realH/realW otherwise. common_particles.h scales the
 * trail's length axis by it. */
float xw_particle_texture_ratio(int tex_w, int tex_h, int real_w, int real_h,
                                float frame_w, float frame_h, int spritesheet);

int xw_core_present_uvs(float *u0, float *v0, float *u1, float *v1);

/* Text anchors live in the scene's centred canvas space (Y up, origin at
 * the canvas centre); map one to window pixels (Y down) and report the
 * scale to rasterise its glyphs at. */
void xw_text_place(float ox, float oy, float ortho_w, float ortho_h, int win_w,
                   int win_h, float *dx, float *dy, float *scale);

/* An image object's quad in window pixels. The engine sizes the quad from
 * the texture's real dimensions (not the scene's "size" field) times the
 * object's scale, and centres it on "origin", which is in canvas pixels
 * with Y counted up from the bottom. Probe-verified against the engine. */
void xw_scene_object_rect(const XWSceneObject *o, int tex_w, int tex_h,
                          float ortho_w, float ortho_h, int win_w, int win_h,
                          int *x, int *y, int *w, int *h);

/* Dependency-ordered render list. The engine walks the objects in scene
 * order and emits each object's dependencies before it (a DFS post-order),
 * skipping self-references, unknown ids and repeats. Fills `order` with
 * indices into `objs` and returns how many were written; `scratch` must
 * have room for `n` bytes. */
int xw_scene_render_order(const XWSceneObject *objs, int n, int *order,
                          unsigned char *scratch);

#endif
