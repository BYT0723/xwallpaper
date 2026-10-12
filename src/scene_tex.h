#ifndef XW_SCENE_TEX_H
#define XW_SCENE_TEX_H

#include <stddef.h>

int xw_tex_find_image(const unsigned char *data, size_t len, size_t *img_off,
                      size_t *img_len, unsigned int *w, unsigned int *h);

#endif
