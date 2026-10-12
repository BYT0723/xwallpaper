#include <stdlib.h>
#include <string.h>

#ifdef XW_HAVE_FT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include "scene_text.h"

#ifdef XW_HAVE_FT

int xw_text_rasterize(const char *font_path, const char *content, float size_px,
                      unsigned char **rgba, int *w, int *h, XWError *err) {
  FT_Library lib = NULL;
  FT_Face face = NULL;
  const unsigned char *s;
  int pen = 0, top = 0, bottom = 0, px = 0, baseline = 0;
  unsigned char *buf = NULL;
  int bw = 0, bh = 0;
  if (!font_path || !content || !rgba || !w || !h) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  if (size_px < 4.0f)
    size_px = 4.0f;
  if (size_px > 256.0f)
    size_px = 256.0f;
  if (FT_Init_FreeType(&lib) != 0) {
    XW_ERROR_SET(err, 1, "cannot init freetype");
    return -1;
  }
  if (FT_New_Face(lib, font_path, 0, &face) != 0) {
    FT_Done_FreeType(lib);
    XW_ERROR_SET(err, 1, "cannot open font '%s'", font_path);
    return -1;
  }
  if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size_px) != 0) {
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    XW_ERROR_SET(err, 1, "cannot set font size");
    return -1;
  }
  for (s = (const unsigned char *)content; *s; s++) {
    FT_GlyphSlot g;
    int rows, above, below;
    if (FT_Load_Char(face, *s, FT_LOAD_RENDER) != 0)
      continue;
    g = face->glyph;
    rows = (int)g->bitmap.rows;
    above = g->bitmap_top;
    below = rows - above;
    if (above > top)
      top = above;
    if (below > bottom)
      bottom = below;
    pen += (int)(g->advance.x >> 6);
  }
  bw = pen + 2;
  bh = top + bottom + 2;
  if (bw < 2)
    bw = 2;
  if (bh < 2)
    bh = 2;
  buf = calloc((size_t)bw * bh, 4);
  if (!buf) {
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  baseline = top + 1;
  px = 1;
  for (s = (const unsigned char *)content; *s; s++) {
    FT_GlyphSlot g;
    int r, c, stride;
    if (FT_Load_Char(face, *s, FT_LOAD_RENDER) != 0)
      continue;
    g = face->glyph;
    stride = g->bitmap.pitch >= 0 ? g->bitmap.pitch : -g->bitmap.pitch;
    for (r = 0; r < (int)g->bitmap.rows; r++) {
      int src_r = g->bitmap.pitch >= 0 ? r : (int)g->bitmap.rows - 1 - r;
      int dst_y = baseline - g->bitmap_top + r;
      unsigned char *row;
      if (dst_y < 0 || dst_y >= bh)
        continue;
      row = g->bitmap.buffer + (size_t)src_r * stride;
      for (c = 0; c < (int)g->bitmap.width; c++) {
        int dst_x = px + g->bitmap_left + c;
        unsigned char *o;
        if (dst_x < 0 || dst_x >= bw)
          continue;
        o = buf + ((size_t)dst_y * bw + dst_x) * 4;
        o[0] = 255;
        o[1] = 255;
        o[2] = 255;
        if (row[c] > o[3])
          o[3] = row[c];
      }
    }
    px += (int)(g->advance.x >> 6);
  }
  FT_Done_Face(face);
  FT_Done_FreeType(lib);
  *rgba = buf;
  *w = bw;
  *h = bh;
  return 0;
}

#else /* !XW_HAVE_FT */

int xw_text_rasterize(const char *font_path, const char *content, float size_px,
                      unsigned char **rgba, int *w, int *h, XWError *err) {
  (void)font_path;
  (void)content;
  (void)size_px;
  (void)rgba;
  (void)w;
  (void)h;
  XW_ERROR_SET(err, 1, "scene text support not compiled in (need freetype2)");
  return -1;
}

#endif /* XW_HAVE_FT */
