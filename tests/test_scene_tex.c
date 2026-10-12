#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XW_HAVE_LZ4
#include <lz4.h>
#endif

#include "harness.h"
#include "../src/scene_tex.h"

static unsigned char *slurp(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  unsigned char *b = malloc((size_t)sz);
  if (!b) {
    fclose(f);
    return NULL;
  }
  if (fread(b, 1, (size_t)sz, f) != (size_t)sz) {
    free(b);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *len = (size_t)sz;
  return b;
}

static void test_find_mini_tex(void) {
  TEST_BEGIN("mini.tex locates embedded PNG with size");
  size_t len = 0;
  unsigned char *b = slurp("tests/data/scene/mini.tex", &len);
  TEST_CHECK(b != NULL);
  if (!b) return;
  size_t off = 0, ilen = 0;
  unsigned int w = 0, h = 0;
  TEST_CHECK_EQ_INT(xw_tex_find_image(b, len, &off, &ilen, &w, &h), 0);
  TEST_CHECK(off + ilen <= len);
  if (off + ilen <= len && ilen >= 8) {
    static const unsigned char magic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    TEST_CHECK(memcmp(b + off, magic, 8) == 0);
  }
  TEST_CHECK(w >= 1 && h >= 1);
  free(b);
}

/* minimal TEXV builder: TEXV0005/TEXI0001/TEXB0002, one mipmap */
static unsigned char *build_texv(uint32_t format, uint32_t w, uint32_t h,
                                 const unsigned char *payload, size_t plen,
                                 int compress, size_t *out_len) {
  unsigned char *blob, *dst;
  size_t hdr = 9 + 9 + 28 + 9 + 4 + 4 + 20, total = hdr + plen;
  blob = malloc(total);
  if (!blob)
    return NULL;
  dst = blob;
  memcpy(dst, "TEXV0005\0", 9);
  dst += 9;
  memcpy(dst, "TEXI0001\0", 9);
  dst += 9;
  {
    uint32_t f[7] = {format, 0, w, h, w, h, 0};
    for (int i = 0; i < 7; i++) {
      dst[0] = (unsigned char)(f[i] & 0xff);
      dst[1] = (unsigned char)((f[i] >> 8) & 0xff);
      dst[2] = (unsigned char)((f[i] >> 16) & 0xff);
      dst[3] = (unsigned char)((f[i] >> 24) & 0xff);
      dst += 4;
    }
  }
  memcpy(dst, "TEXB0002\0", 9);
  dst += 9;
  {
    /* images, mipmaps, mw, mh, comp, usize, csize */
    uint32_t m[7] = {1, 1, w, h, (uint32_t)compress, (uint32_t)plen,
                     (uint32_t)plen};
    /* usize for compressed tested separately via LZ4 below */
    for (int i = 0; i < 7; i++) {
      dst[0] = (unsigned char)(m[i] & 0xff);
      dst[1] = (unsigned char)((m[i] >> 8) & 0xff);
      dst[2] = (unsigned char)((m[i] >> 16) & 0xff);
      dst[3] = (unsigned char)((m[i] >> 24) & 0xff);
      dst += 4;
    }
  }
  memcpy(dst, payload, plen);
  *out_len = total;
  return blob;
}

static void test_decode_raw(void) {
  TEST_BEGIN("decodes synthetic RGBA8/R8/DXT1 mipmaps");
  XWError err = {0};
  unsigned char px[64];
  for (int i = 0; i < 64; i++) px[i] = (unsigned char)i;
  size_t len = 0;
  unsigned char *blob = build_texv(0, 4, 4, px, sizeof(px), 0, &len);
  TEST_CHECK(blob != NULL);
  if (!blob)
    return;
  XWTexImage img;
  memset(&img, 0, sizeof(img));
  TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), 0);
  TEST_CHECK_EQ_INT((int)img.fmt, (int)XW_TEXFMT_RGBA8);
  TEST_CHECK_EQ_INT((int)img.w, 4);
  TEST_CHECK_EQ_INT(img.len, 64);
  TEST_CHECK(memcmp(img.px, px, 64) == 0);
  xw_tex_image_free(&img);
  free(blob);

  /* R8 single channel */
  unsigned char g[16];
  for (int i = 0; i < 16; i++) g[i] = (unsigned char)(i * 16);
  blob = build_texv(9, 4, 4, g, sizeof(g), 0, &len);
  TEST_CHECK(blob != NULL);
  if (blob) {
    memset(&img, 0, sizeof(img));
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), 0);
    TEST_CHECK_EQ_INT((int)img.fmt, (int)XW_TEXFMT_R8);
    TEST_CHECK(memcmp(img.px, g, 16) == 0);
    xw_tex_image_free(&img);
    free(blob);
  }

  /* DXT1 4x4 = one 8-byte block */
  unsigned char dxt[8] = {0, 0, 255, 255, 0, 0, 0, 0};
  blob = build_texv(7, 4, 4, dxt, sizeof(dxt), 0, &len);
  TEST_CHECK(blob != NULL);
  if (blob) {
    memset(&img, 0, sizeof(img));
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), 0);
    TEST_CHECK_EQ_INT((int)img.fmt, (int)XW_TEXFMT_DXT1);
    TEST_CHECK_EQ_INT(img.len, 8);
    xw_tex_image_free(&img);
    free(blob);
  }

#ifdef XW_HAVE_LZ4
  /* LZ4 round-trip: compressed payload decodes to the raw pattern */
  {
    unsigned char raw[64], cb[128];
    int cs;
    for (int i = 0; i < 64; i++) raw[i] = (unsigned char)(i * 3 + 1);
    cs = LZ4_compress_default((const char *)raw, (char *)cb, 64, sizeof(cb));
    TEST_CHECK(cs > 0);
    if (cs > 0) {
      blob = build_texv(0, 4, 4, cb, (size_t)cs, 1, &len);
      TEST_CHECK(blob != NULL);
      if (blob) {
        /* patch usize to the raw size (builder writes csize twice);
         * header: 9+9+28+9 images mipmaps mw mh comp [usize] csize */
        blob[75] = 64;
        blob[76] = 0;
        blob[77] = 0;
        blob[78] = 0;
        memset(&img, 0, sizeof(img));
        memset(&err, 0, sizeof(err));
        TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), 0);
        TEST_CHECK(memcmp(img.px, raw, 64) == 0);
        xw_tex_image_free(&img);
        free(blob);
      }
    }
  }
#endif
}

/* TEXB0004 builder: like build_texv plus the isVideo flag word */
static unsigned char *build_texv4(uint32_t fif, uint32_t is_video,
                                   uint32_t w, uint32_t h, uint32_t mw,
                                   uint32_t mh, uint32_t comp, int32_t usize,
                                   const unsigned char *payload, size_t plen,
                                   size_t *out_len) {
  unsigned char *blob, *dst;
  uint32_t m[8] = {1, fif, is_video, 1, mw, mh, comp, (uint32_t)usize};
  size_t hdr = 9 + 9 + 28 + 9 + 8 * 4 + 4, total = hdr + plen;
  int i;
  blob = malloc(total);
  if (!blob)
    return NULL;
  dst = blob;
  memcpy(dst, "TEXV0005\0", 9);
  dst += 9;
  memcpy(dst, "TEXI0001\0", 9);
  dst += 9;
  {
    uint32_t f[7] = {0, 0, w, h, w, h, 0};
    for (i = 0; i < 7; i++) {
      dst[0] = (unsigned char)(f[i] & 0xff);
      dst[1] = (unsigned char)((f[i] >> 8) & 0xff);
      dst[2] = (unsigned char)((f[i] >> 16) & 0xff);
      dst[3] = (unsigned char)((f[i] >> 24) & 0xff);
      dst += 4;
    }
  }
  memcpy(dst, "TEXB0004\0", 9);
  dst += 9;
  /* images, fif, isVideo, mipmaps, mw, mh, comp, usize */
  for (i = 0; i < 8; i++) {
    dst[0] = (unsigned char)(m[i] & 0xff);
    dst[1] = (unsigned char)((m[i] >> 8) & 0xff);
    dst[2] = (unsigned char)((m[i] >> 16) & 0xff);
    dst[3] = (unsigned char)((m[i] >> 24) & 0xff);
    dst += 4;
  }
  /* csize */
  dst[0] = (unsigned char)(plen & 0xff);
  dst[1] = (unsigned char)((plen >> 8) & 0xff);
  dst[2] = (unsigned char)((plen >> 16) & 0xff);
  dst[3] = (unsigned char)((plen >> 24) & 0xff);
  dst += 4;
  memcpy(dst, payload, plen);
  *out_len = total;
  return blob;
}

static void test_decode_texb0004(void) {
  TEST_BEGIN("TEXB0004 stills decode, video refused");
  XWError err = {0};
  XWTexImage img;
  unsigned char px[64];
  size_t len = 0;
  unsigned char *blob;
  for (int i = 0; i < 64; i++) px[i] = (unsigned char)(i + 7);
  /* raw still: UNKNOWN fif, not video */
  blob = build_texv4(0xFFFFFFFFu, 0, 4, 4, 4, 4, 0, 64, px, sizeof(px), &len);
  TEST_CHECK(blob != NULL);
  if (blob) {
    memset(&img, 0, sizeof(img));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), 0);
    TEST_CHECK_EQ_INT((int)img.fmt, (int)XW_TEXFMT_RGBA8);
    TEST_CHECK(memcmp(img.px, px, 64) == 0);
    xw_tex_image_free(&img);
    free(blob);
  }
  /* embedded PNG with usize=0 (as shipped by real walls) */
  {
    static const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A,
                                        0x1A, 0x0A, 0x00};
    blob = build_texv4(13, 0, 4, 4, 4, 4, 0, 0, png, sizeof(png), &len);
    TEST_CHECK(blob != NULL);
    if (blob) {
      memset(&img, 0, sizeof(img));
      memset(&err, 0, sizeof(err));
      TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), 0);
      TEST_CHECK_EQ_INT((int)img.fmt, (int)XW_TEXFMT_PNG);
      TEST_CHECK_EQ_INT(img.len, sizeof(png));
      xw_tex_image_free(&img);
      free(blob);
    }
  }
  /* video flag refused */
  blob = build_texv4(13, 1, 4, 4, 4, 4, 0, 64, px, sizeof(px), &len);
  TEST_CHECK(blob != NULL);
  if (blob) {
    memset(&img, 0, sizeof(img));
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), -1);
    free(blob);
  }
}

static void test_decode_edges(void) {
  TEST_BEGIN("rejects bad magic, truncation, formats, sizes");
  XWError err = {0};
  XWTexImage img;
  unsigned char px[64];
  memset(px, 0x80, sizeof(px));
  memset(&img, 0, sizeof(img));
  TEST_CHECK_EQ_INT(xw_tex_decode(NULL, 0, &img, &err), -1);
  TEST_CHECK_EQ_INT(xw_tex_decode(px, sizeof(px), &img, &err), -1);

  size_t len = 0;
  unsigned char *blob = build_texv(2, 4, 4, px, sizeof(px), 0, &len);
  TEST_CHECK(blob != NULL);
  if (blob) {
    /* RGB565 unsupported */
    memset(&img, 0, sizeof(img));
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), -1);
    /* corrupt magic */
    blob[4] = 'X';
    memset(&img, 0, sizeof(img));
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len, &img, &err), -1);
    /* truncated payload */
    blob[4] = 'P';
    memset(&img, 0, sizeof(img));
    memset(&err, 0, sizeof(err));
    TEST_CHECK_EQ_INT(xw_tex_decode(blob, len - 1, &img, &err), -1);
    free(blob);
  }
}

static void test_bad_inputs(void) {
  TEST_BEGIN("non-tex/truncated/NULL rejected, no crash");
  size_t len = 0;
  unsigned char *pkg = slurp("tests/data/scene/mini.pkg", &len);
  TEST_CHECK(pkg != NULL);
  if (pkg) {
    size_t off = 0, ilen = 0;
    unsigned int w = 0, h = 0;
    TEST_CHECK_EQ_INT(xw_tex_find_image(pkg, len, &off, &ilen, &w, &h), -1);
    free(pkg);
  }
  TEST_CHECK_EQ_INT(xw_tex_find_image(NULL, 0, NULL, NULL, NULL, NULL), -1);
  unsigned char tiny[4] = {'T', 'E', 'X', 'V'};
  TEST_CHECK_EQ_INT(xw_tex_find_image(tiny, sizeof(tiny), NULL, NULL, NULL, NULL), -1);
}

int main(void) {
  TEST_SUITE("scene_tex");
  test_find_mini_tex();
  test_bad_inputs();
  test_decode_raw();
  test_decode_texb0004();
  test_decode_edges();
  TEST_SUMMARY();
}
