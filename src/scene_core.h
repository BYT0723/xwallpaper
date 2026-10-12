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

typedef enum {
  XW_BLEND_NORMAL = 0,
  XW_BLEND_TRANSLUCENT,
  XW_BLEND_ADDITIVE,
} XWBlend;

#define XW_GL_ONE 1
#define XW_GL_ZERO 0
#define XW_GL_SRC_ALPHA 0x0302
#define XW_GL_ONE_MINUS_SRC_ALPHA 0x0303

void xw_blend_funcs(XWBlend m, int *srcRGB, int *dstRGB, int *srcA, int *dstA);

int xw_bloom_enabled(const XWSceneCamera *cam);

int xw_core_present_uvs(float *u0, float *v0, float *u1, float *v1);

#endif
