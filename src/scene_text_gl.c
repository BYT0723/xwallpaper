#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_text_gl.h"

int xw_scene_text_draw(XWGl *g, const XWSceneObject *o, const char *dir,
                       XWTextItem *it, XWError *err) {
  char font_path[1152];
  unsigned char *px = NULL;
  int w = 0, h = 0, rc = -1;
  unsigned int prog = 0;
  if (!g || !o || !dir || !it) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (!o->text.font[0]) {
    XW_ERROR_SET(err, 1, "text object '%s' has no font", o->id);
    return -1;
  }
  if (!it->valid || strcmp(it->font, o->text.font) != 0 ||
      strcmp(it->content, o->text.content) != 0 || it->size != o->text.size) {
    xw_text_item_free(g, it);
    snprintf(font_path, sizeof(font_path), "%s/%s", dir, o->text.font);
    if (xw_text_rasterize(font_path, o->text.content, o->text.size, &px, &w,
                           &h, err) != 0)
      return -1;
    if (xw_gl_upload_rgba(g, px, w, h, &it->tex, err) != 0) {
      free(px);
      return -1;
    }
    free(px);
    snprintf(it->font, sizeof(it->font), "%s", o->text.font);
    snprintf(it->content, sizeof(it->content), "%s", o->text.content);
    it->size = o->text.size;
    it->w = w;
    it->h = h;
    it->valid = 1;
  }
  if (xw_gl_program_copy(g, &prog, err) != 0)
    return -1;
  if (xw_gl_blend_begin(g, XW_BLEND_TRANSLUCENT) != 0)
    return -1;
  if (xw_gl_draw_rect(g, prog, it->tex, 0.0f, (int)o->origin[0],
                      (int)o->origin[1], it->w, it->h) == 0)
    rc = 0;
  else
    XW_ERROR_SET(err, 1, "text draw failed");
  xw_gl_blend_end(g);
  return rc;
}

void xw_text_item_free(XWGl *g, XWTextItem *it) {
  if (!it)
    return;
  if (it->valid && it->tex)
    xw_gl_delete_texture(g, it->tex);
  memset(it, 0, sizeof(*it));
}

