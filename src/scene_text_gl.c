#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_text_gl.h"
#include "scene_script.h"

int xw_scene_text_draw(XWGl *g, const XWSceneObject *o, const char *dir,
                       const XWSceneCamera *cam, XWTextItem *it, float alpha,
                       XWError *err) {
  char font_path[1152];
  unsigned char *px = NULL;
  int w = 0, h = 0, rc = -1;
  unsigned int prog = 0;
  const char *content;
  float dx = 0.0f, dy = 0.0f, scale = 1.0f;
  float px_size;
  int win_w = 0, win_h = 0;
  if (!g || !o || !dir || !it) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (!o->text.font[0])
    return 1;
  /* the anchor is in the scene's centred canvas space, not window pixels */
  if (xw_gl_target_size(g, &win_w, &win_h) != 0) {
    win_w = 0;
    win_h = 0;
  }
  xw_text_place(o->origin[0], o->origin[1], cam ? cam->ortho_w : 0.0f,
                cam ? cam->ortho_h : 0.0f, win_w, win_h, &dx, &dy, &scale);
  px_size = o->text.size * scale;
  content = o->text.content;
  /* a scripted layer owns its text: update(value) returns the next string */
  if (o->text.script) {
    if (!it->layer) {
      XWError lerr = {0};
      if (xw_script_text_layer_create(&it->layer, o->text.script,
                                      o->text.content, &lerr) != 0)
        it->layer = NULL; /* keep the static text, do not fail the frame */
    }
    if (it->layer)
      content = xw_script_text_layer_update(it->layer);
  }
  if (!it->valid || strcmp(it->font, o->text.font) != 0 ||
      strcmp(it->content, content) != 0 || it->size != px_size) {
    XWTextLayer *keep = it->layer; /* survives re-rasterising */
    xw_text_item_free(g, it);
    it->layer = keep;
    snprintf(font_path, sizeof(font_path), "%s/%s", dir, o->text.font);
    if (xw_text_rasterize(font_path, content, px_size, &px, &w, &h, err) != 0) {
      memset(err, 0, sizeof(*err));
      return 1;
    }
    if (xw_gl_upload_rgba(g, px, w, h, &it->tex, err) != 0) {
      free(px);
      return -1;
    }
    free(px);
    snprintf(it->font, sizeof(it->font), "%s", o->text.font);
    snprintf(it->content, sizeof(it->content), "%s", content);
    it->size = px_size;
    it->w = w;
    it->h = h;
    it->valid = 1;
  }
  if (xw_gl_program_composite(g, &prog, err) != 0)
    return -1;
  if (xw_gl_blend_begin(g, XW_BLEND_TRANSLUCENT) != 0)
    return -1;
  /* the anchor is the text's middle, like the engine's centre alignment */
  if (xw_gl_draw_rect_alpha(g, prog, it->tex, 0.0f, alpha,
                            (int)(dx - (float)it->w * 0.5f),
                            (int)(dy - (float)it->h * 0.5f), it->w, it->h) == 0)
    rc = 0;
  else
    XW_ERROR_SET(err, 1, "text draw failed");
  xw_gl_blend_end(g);
  return rc;
}

void xw_text_item_free(XWGl *g, XWTextItem *it) {
  XWTextLayer *keep;
  if (!it)
    return;
  keep = it->layer;
  if (it->valid && it->tex)
    xw_gl_delete_texture(g, it->tex);
  memset(it, 0, sizeof(*it));
  it->layer = keep;
}

void xw_text_item_destroy(XWGl *g, XWTextItem *it) {
  if (!it)
    return;
  xw_script_text_layer_free(it->layer);
  it->layer = NULL;
  xw_text_item_free(g, it);
}

