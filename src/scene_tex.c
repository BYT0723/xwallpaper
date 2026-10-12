#include <stdint.h>
#include <string.h>

#include "scene_tex.h"

static uint32_t rd32be(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int xw_tex_find_image(const unsigned char *data, size_t len, size_t *img_off,
                      size_t *img_len, unsigned int *w, unsigned int *h) {
  if (!data || len < 12) return -1;
  if (!img_off || !img_len || !w || !h) return -1;
  if (memcmp(data, "TEXV", 4) != 0) return -1;
  static const unsigned char magic[8] = {0x89, 'P', 'N', 'G',
                                         0x0D, 0x0A, 0x1A, 0x0A};
  size_t off = 4;
  int found = 0;
  for (; off + 8 <= len; off++) {
    if (memcmp(data + off, magic, 8) == 0) {
      found = 1;
      break;
    }
  }
  if (!found) return -1;
  if (off + 33 > len) return -1;
  if (memcmp(data + off + 12, "IHDR", 4) != 0) return -1;
  *img_off = off;
  *img_len = len - off;
  *w = rd32be(data + off + 16);
  *h = rd32be(data + off + 20);
  if (*w == 0 || *h == 0 || *w > 16384 || *h > 16384) return -1;
  return 0;
}
