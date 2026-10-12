#ifndef XW_SCENE_IMAGE_H
#define XW_SCENE_IMAGE_H

#include "scene_gl.h"
#include "scene_model.h"

/* Render every IMAGE object with a resolvable texture: base copy plus the
 * effect pass chain in FBO ping-pong, composited to the window through the
 * builtin alpha program (alphas[i] per object, NULL = all opaque).
 * Objects without a texture are skipped (background clear color shows
 * through); blending is scoped to the composite pass only. Strict: a
 * present-but-unreadable texture or shader fails the frame so broken
 * walls surface during smoke instead of silently showing background. */
int xw_scene_image_render(XWGl *gl, const XWScene *scene, const char *dir,
                          float time, const float *alphas, XWError *err);

#endif
