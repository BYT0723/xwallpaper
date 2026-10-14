#ifndef XW_SCENE_TEXT_GL_H
#define XW_SCENE_TEXT_GL_H

#include "scene_gl.h"
#include "scene_model.h"
#include "scene_text.h"

/* Draw a TEXT object (rasterize on key change, alpha-blend the quad at
 * the resolved object alpha). Strict like the image renderer: missing
 * font fails the frame. */
int xw_scene_text_draw(XWGl *g, const XWSceneObject *o, const char *dir,
                       XWTextItem *it, float alpha, XWError *err);

/* Drop a cached item's texture (e.g. before backend destroy). */
void xw_text_item_free(XWGl *g, XWTextItem *it);

/* Drop the texture *and* the scripted layer (backend teardown only: a
 * re-rasterise keeps the layer so its script state survives). */
void xw_text_item_destroy(XWGl *g, XWTextItem *it);

#endif
