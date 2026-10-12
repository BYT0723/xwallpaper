#ifndef XW_SCENE_TEX_H
#define XW_SCENE_TEX_H

#include <stddef.h>

#include "xw_err.h"

int xw_tex_find_image(const unsigned char *data, size_t len, size_t *img_off,
                      size_t *img_len, unsigned int *w, unsigned int *h);

/* Raw TEXV decode (TEXV0005/TEXI0001/TEXB0001-4, LZ4 mipmaps). Level-0
 * mipmap only; animations/video rejected. Data layouts follow
 * linux-wallpaperengine's TextureParser. */
typedef enum {
  XW_TEXFMT_RGBA8 = 0,
  XW_TEXFMT_R8,
  XW_TEXFMT_RG88,
  XW_TEXFMT_DXT1,
  XW_TEXFMT_DXT3,
  XW_TEXFMT_DXT5,
  XW_TEXFMT_PNG,
} XWTexFmt;

typedef struct XWTexImage {
  XWTexFmt fmt;
  unsigned int w, h;
  unsigned int tex_w, tex_h;
  unsigned char *px;
  size_t len;
} XWTexImage;

int xw_tex_decode(const unsigned char *data, size_t len, XWTexImage *img,
                  XWError *err);
void xw_tex_image_free(XWTexImage *img);

/* Resolve a material texture ref to a loadable file. Bare refs ("night
 * blue") anchor at the material file's directory, pathed refs at the
 * scene directory; tries "", ".tex", ".png", ".jpg" in order. */
int xw_tex_resolve(const char *dir, const char *material, const char *ref,
                   char *out, size_t n);

#endif
