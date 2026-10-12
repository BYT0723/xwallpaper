#include <stdio.h>
#include <string.h>

#include "scene_gl.h"
#include "scene_image.h"

static int render_object(XWGl *gl, const XWSceneObject *o, const char *dir,
                         float time, float alpha, XWError *err) {
  char path[1152];
  unsigned int tex = 0, prog = 0, comp = 0, src = 0;
  int i, e, dst = 1;
  snprintf(path, sizeof(path), "%s/%s", dir, o->texture);
  if (xw_gl_upload(gl, path, &tex, err) != 0)
    return -1;
  if (xw_gl_program_copy(gl, &prog, err) != 0)
    return -1;
  if (xw_gl_program_composite(gl, &comp, err) != 0)
    return -1;
  /* base copy always lands in an FBO; the composite pass owns the window
   * so resolved alpha modulates arbitrary effect chains uniformly. */
  if (xw_gl_bind_target(gl, 0) != 0)
    return -1;
  if (xw_gl_draw(gl, prog, tex, time) != 0)
    return -1;
  if (xw_gl_fbo_texture(gl, 0, &src) != 0)
    return -1;
  for (e = 0; e < o->n_effects; e++) {
    for (i = 0; i < o->effects[e].n_passes; i++) {
      unsigned int ep = 0;
      snprintf(path, sizeof(path), "%s/%s", dir,
               o->effects[e].passes[i].shader);
      if (xw_gl_program_we(gl, path, &ep, err) != 0)
        return -1;
      if (xw_gl_bind_target(gl, dst) != 0)
        return -1;
      if (xw_gl_draw(gl, ep, src, time) != 0)
        return -1;
      if (xw_gl_fbo_texture(gl, dst, &src) != 0)
        return -1;
      dst = 1 - dst;
    }
  }
  if (xw_gl_blend_begin(gl, XW_BLEND_TRANSLUCENT) != 0)
    return -1;
  if (xw_gl_bind_target(gl, -1) != 0) {
    xw_gl_blend_end(gl);
    return -1;
  }
  if (xw_gl_draw_alpha(gl, comp, src, time, alpha) != 0) {
    xw_gl_blend_end(gl);
    return -1;
  }
  xw_gl_blend_end(gl);
  return 0;
}

int xw_scene_image_render(XWGl *gl, const XWScene *scene, const char *dir,
                          float time, const float *alphas, XWError *err) {
  int i;
  if (!gl || !scene || !dir) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (xw_gl_bind_target(gl, -1) != 0)
    return -1;
  xw_gl_clear(gl, scene->camera.clearcolor[0], scene->camera.clearcolor[1],
              scene->camera.clearcolor[2]);
  for (i = 0; i < scene->n_objects; i++) {
    const XWSceneObject *o = &scene->objects[i];
    float a = alphas ? alphas[i] : 1.0f;
    if (!o->visible || o->type != XW_OBJ_IMAGE || !o->texture[0])
      continue;
    if (render_object(gl, o, dir, time, a, err) != 0)
      return -1;
  }
  return 0;
}
