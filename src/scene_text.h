#ifndef XW_SCENE_TEXT_H
#define XW_SCENE_TEXT_H

#include "xw_err.h"

/* CPU glyph rasterization (no GL/X): lays out one line left to right,
 * white RGB with coverage alpha. Content is bytes (Latin-1 codepoints;
 * multi-byte UTF-8 is out of scope for now). Size clamps to 4..256px.
 * Empty content yields a blank 2x2 buffer. Caller frees *rgba. */
int xw_text_rasterize(const char *font_path, const char *content, float size_px,
                      unsigned char **rgba, int *w, int *h, XWError *err);

#endif
