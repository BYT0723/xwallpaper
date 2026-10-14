#ifndef XW_SCENE_TEXT_H
#define XW_SCENE_TEXT_H

#include "xw_err.h"

/* CPU glyph rasterization (no GL/X): lays out one line left to right,
 * white RGB with coverage alpha. Content is bytes (Latin-1 codepoints;
 * multi-byte UTF-8 is out of scope for now). Size clamps to 4..256px.
 * Empty content yields a blank 2x2 buffer. Caller frees *rgba. */
int xw_text_rasterize(const char *font_path, const char *content, float size_px,
                      unsigned char **rgba, int *w, int *h, XWError *err);

/* Per-object raster cache (owned by the backend). Re-rasterizes only when
 * font/size/content change; the texture is drawn at origin (top-left). */
typedef struct XWTextItem {
  char font[256];
  char content[256];
  float size;
  unsigned int tex;
  int w, h;
  int valid;
  struct XWTextLayer *layer; /* scripted layer, NULL for static text */
} XWTextItem;

#endif
