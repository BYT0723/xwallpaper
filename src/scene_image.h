#ifndef XW_SCENE_IMAGE_H
#define XW_SCENE_IMAGE_H

#include "scene_gl.h"
#include "scene_model.h"

/* Render every IMAGE object with a resolvable texture: base copy plus the
 * effect pass chain in FBO ping-pong, composited to the window through the
 * builtin alpha program (alphas[i] per object, NULL = all opaque).
 * Unresolvable objects are skipped (1); background shows through.
 * Returns 0 ok, -1 frame error. */
int xw_scene_image_render(XWGl *gl, const XWScene *scene, const char *dir,
                          float time, const float *alphas, XWError *err);

#endif
