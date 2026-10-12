#ifndef XW_SCENE_IMAGE_H
#define XW_SCENE_IMAGE_H

#include "scene_gl.h"
#include "scene_model.h"

/* Render every IMAGE object with a resolvable texture: base copy plus the
 * effect pass chain, ping-ponging the window-size FBOs with the last pass
 * landing on the window backbuffer (caller presents). Objects without a
 * texture are skipped (background clear color shows through); blending
 * stays off (opaque) until the multi-object composite task. Strict: a
 * present-but-unreadable texture or shader fails the frame so broken
 * walls surface during smoke instead of silently showing background. */
int xw_scene_image_render(XWGl *gl, const XWScene *scene, const char *dir,
                          float time, XWError *err);

#endif
