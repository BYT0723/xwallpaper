#ifndef XW_SCENE_PARTICLE_GL_H
#define XW_SCENE_PARTICLE_GL_H

#include "scene_gl.h"
#include "scene_model.h"
#include "scene_particle.h"

/* Simulate dt then draw live particles as one point-sprite batch on the
 * bound target (positions are object-local; origin maps to window pixels).
 * Strict like the image renderer: missing sprite errors fail. */
int xw_scene_particle_render(XWGl *g, const XWSceneObject *o, XWEmitter *e,
                             const char *dir, float dt, XWError *err);

#endif
