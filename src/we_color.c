#include "we_color.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 4 bits/channel = 4096 buckets; sampling caps the input at ~64x64 pixels. */
#define XW_COLOR_BUCKETS 4096
#define XW_COLOR_SAMPLE 64
#define XW_COLOR_MIN_DIST2 (32 * 32)

typedef struct {
  unsigned count;
  unsigned r, g, b;
} XWColorBucket;

static double channel_to_linear(unsigned c) {
  double v = (double)c / 255.0;
  return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* WCAG relative luminance. */
static double relative_luminance(unsigned r, unsigned g, unsigned b) {
  return 0.2126 * channel_to_linear(r) + 0.7152 * channel_to_linear(g) +
         0.0722 * channel_to_linear(b);
}

static void format_hex(char out[8], unsigned r, unsigned g, unsigned b) {
  static const char hex[] = "0123456789abcdef";
  out[0] = '#';
  out[1] = hex[(r >> 4) & 0xf];
  out[2] = hex[r & 0xf];
  out[3] = hex[(g >> 4) & 0xf];
  out[4] = hex[g & 0xf];
  out[5] = hex[(b >> 4) & 0xf];
  out[6] = hex[b & 0xf];
  out[7] = '\0';
}

static int bucket_cmp(const void *pa, const void *pb) {
  const XWColorBucket *a = pa;
  const XWColorBucket *b = pb;
  double la, lb;
  if (a->count != b->count)
    return a->count < b->count ? 1 : -1;
  la = relative_luminance(a->r, a->g, a->b);
  lb = relative_luminance(b->r, b->g, b->b);
  if (la != lb)
    return la < lb ? 1 : -1;
  if (a->r != b->r)
    return a->r < b->r ? -1 : 1;
  if (a->g != b->g)
    return a->g < b->g ? -1 : 1;
  if (a->b != b->b)
    return a->b < b->b ? -1 : 1;
  return 0;
}

int xw_we_color_palette(const unsigned char *px, int w, int h, int rowstride,
                        int n_channels, char primary[8], char secondary[8],
                        char tertiary[8], char text[8], char high[8]) {
  XWColorBucket *buckets;
  unsigned char picked[3][3];
  char *out[3];
  int step_x, step_y, n = 0, npicked = 0;

  if (!px || w <= 0 || h <= 0 || rowstride <= 0 || n_channels < 3 ||
      !primary || !secondary || !tertiary || !text || !high)
    return -1;
  primary[0] = '\0';
  secondary[0] = '\0';
  tertiary[0] = '\0';
  text[0] = '\0';
  high[0] = '\0';

  buckets = calloc(XW_COLOR_BUCKETS, sizeof(*buckets));
  if (!buckets)
    return -1;

  step_x = w / XW_COLOR_SAMPLE;
  step_y = h / XW_COLOR_SAMPLE;
  if (step_x < 1)
    step_x = 1;
  if (step_y < 1)
    step_y = 1;

  for (int y = 0; y < h; y += step_y) {
    const unsigned char *row = px + (size_t)y * (size_t)rowstride;
    for (int x = 0; x < w; x += step_x) {
      const unsigned char *p = row + (size_t)x * (size_t)n_channels;
      unsigned r = p[0], g = p[1], b = p[2];
      unsigned idx;
      if (n_channels == 4 && p[3] == 0)
        continue;
      idx = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
      buckets[idx].count++;
      buckets[idx].r += r;
      buckets[idx].g += g;
      buckets[idx].b += b;
    }
  }

  for (int i = 0; i < XW_COLOR_BUCKETS; i++) {
    unsigned count = buckets[i].count;
    if (!count)
      continue;
    buckets[n].count = count;
    buckets[n].r = (buckets[i].r + count / 2) / count;
    buckets[n].g = (buckets[i].g + count / 2) / count;
    buckets[n].b = (buckets[i].b + count / 2) / count;
    n++;
  }
  qsort(buckets, (size_t)n, sizeof(*buckets), bucket_cmp);

  out[0] = primary;
  out[1] = secondary;
  out[2] = tertiary;
  for (int i = 0; i < n && npicked < 3; i++) {
    int distinct = 1;
    for (int j = 0; j < npicked; j++) {
      long dr = (long)buckets[i].r - picked[j][0];
      long dg = (long)buckets[i].g - picked[j][1];
      long db = (long)buckets[i].b - picked[j][2];
      if (dr * dr + dg * dg + db * db <= XW_COLOR_MIN_DIST2) {
        distinct = 0;
        break;
      }
    }
    if (!distinct)
      continue;
    picked[npicked][0] = (unsigned char)buckets[i].r;
    picked[npicked][1] = (unsigned char)buckets[i].g;
    picked[npicked][2] = (unsigned char)buckets[i].b;
    format_hex(out[npicked], buckets[i].r, buckets[i].g, buckets[i].b);
    npicked++;
  }

  if (npicked > 0) {
    double l = relative_luminance(picked[0][0], picked[0][1], picked[0][2]);
    double on_white = 1.05 / (l + 0.05);
    double on_black = (l + 0.05) / 0.05;
    const char *best = on_white > on_black ? "#ffffff" : "#000000";
    memcpy(text, best, 8);
    memcpy(high, best, 8);
  }

  free(buckets);
  return 0;
}
