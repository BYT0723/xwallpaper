#include <stdio.h>
#include <string.h>

#include "scene_gl.h"
#include "scene_image.h"

static int render_object(XWGl *gl, const XWSceneObject *o, const char *dir,
                         float time, XWError *err) {
  char path[1152];
  unsigned int tex = 0, prog = 0;
  int npasses = 0, i, e;
  for (e = 0; e < o->n_effects; e++) npasses += o->effects[e].n_passes;
  snprintf(path, sizeof(path), "%s/%s", dir, o->texture);
  if (xw_gl_upload(gl, path, &tex, err) != 0)
    return -1;
  if (xw_gl_program_copy(gl, &prog, err) != 0)
    return -1;
  if (npasses == 0) {
    if (xw_gl_bind_target(gl, -1) != 0)
      return -1;
    return xw_gl_draw(gl, prog, tex, time);
  }
  if (xw_gl_bind_target(gl, 0) != 0)
    return -1;
  if (xw_gl_draw(gl, prog, tex, time) != 0)
    return -1;
  {
    unsigned int src = 0, dst_prog = 0;
    int seen = 0, total = npasses, cur = 0, dst = 1;
    if (xw_gl_fbo_texture(gl, 0, &src) != 0)
      return -1;
    for (e = 0; e < o->n_effects; e++) {
      for (i = 0; i < o->effects[e].n_passes; i++) {
        int last = (cur == total - 1);
        snprintf(path, sizeof(path), "%s/%s", dir,
                 o->effects[e].passes[i].shader);
        if (xw_gl_program_we(gl, path, &dst_prog, err) != 0)
          return -1;
        if (xw_gl_bind_target(gl, last ? -1 : dst) != 0)
          return -1;
        if (xw_gl_draw(gl, dst_prog, src, time) != 0)
          return -1;
        if (!last) {
          if (xw_gl_fbo_texture(gl, dst, &src) != 0)
            return -1;
          dst = 1 - dst;
        }
        seen++;
        cur++;
      }
    }
    (void)seen;
  }
  return 0;
}

int xw_scene_image_render(XWGl *gl, const XWScene *scene, const char *dir,
                          float time, XWError *err) {
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
    if (!o->visible || o->type != XW_OBJ_IMAGE || !o->texture[0])
      continue;
    if (render_object(gl, o, dir, time, err) != 0)
      return -1;
  }
  return 0;
}
