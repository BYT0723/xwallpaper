#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/scene_text.h"

static const char *find_font(void) {
  static const char *paths[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    NULL,
  };
  for (int i = 0; paths[i]; i++)
    if (access(paths[i], R_OK) == 0)
      return paths[i];
  return NULL;
}

static void test_rasterize(void) {
  TEST_BEGIN("rasterizes Hello with inked pixels");
  const char *font = find_font();
  if (!font) {
    printf("  SKIP: no test font found\n");
    return;
  }
  unsigned char *px = NULL;
  int w = 0, h = 0;
  XWError err = {0};
  TEST_CHECK_EQ_INT(xw_text_rasterize(font, "Hello", 48.0f, &px, &w, &h, &err), 0);
  if (!px)
    return;
  TEST_CHECK(w > 50 && w < 600);
  TEST_CHECK(h > 20 && h < 200);
  int ink = 0;
  for (int i = 0; i < w * h; i++)
    if (px[i * 4 + 3] > 128)
      ink++;
  TEST_CHECK(ink > 50);
  free(px);
}

static void test_empty_and_size_clamp(void) {
  TEST_BEGIN("empty content blanks, tiny size clamps");
  const char *font = find_font();
  if (!font) {
    printf("  SKIP: no test font found\n");
    return;
  }
  unsigned char *px = NULL;
  int w = 0, h = 0;
  XWError err = {0};
  TEST_CHECK_EQ_INT(xw_text_rasterize(font, "", 48.0f, &px, &w, &h, &err), 0);
  if (px) {
    int ink = 0;
    for (int i = 0; i < w * h; i++)
      if (px[i * 4 + 3] > 0)
        ink++;
    TEST_CHECK_EQ_INT(ink, 0);
    free(px);
  }
  px = NULL;
  TEST_CHECK_EQ_INT(xw_text_rasterize(font, "A", 1.0f, &px, &w, &h, &err), 0);
  if (px) {
    TEST_CHECK(w >= 2 && h >= 2);
    free(px);
  }
}

static void test_bad_inputs(void) {
  TEST_BEGIN("missing font and NULL rejected");
  unsigned char *px = NULL;
  int w = 0, h = 0;
  XWError err = {0};
  TEST_CHECK_EQ_INT(
      xw_text_rasterize("/nonexistent/font.ttf", "Hi", 32.0f, &px, &w, &h, &err),
      -1);
  TEST_CHECK(err.code != 0);
  TEST_CHECK_EQ_INT(xw_text_rasterize(NULL, "Hi", 32.0f, &px, &w, &h, &err), -1);
  TEST_CHECK_EQ_INT(xw_text_rasterize("/x.ttf", NULL, 32.0f, &px, &w, &h, &err),
                    -1);
  TEST_CHECK_EQ_INT(xw_text_rasterize("/x.ttf", "Hi", 32.0f, NULL, &w, &h, &err),
                    -1);
}

static const char *find_cjk_font(void) {
  static const char *paths[] = {
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Light.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-DemiLight.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    NULL,
  };
  for (int i = 0; paths[i]; i++)
    if (access(paths[i], R_OK) == 0)
      return paths[i];
  return NULL;
}

static void test_utf8_decode(void) {
  unsigned int cp = 0;
  TEST_BEGIN("UTF-8 code points decode one at a time");
  TEST_CHECK_EQ_INT(xw_utf8_next("A", &cp), 1);
  TEST_CHECK_EQ_INT((int)cp, 'A');
  TEST_CHECK_EQ_INT(xw_utf8_next("\xc3\xa9", &cp), 2); /* e-acute */
  TEST_CHECK_EQ_INT((int)cp, 0xE9);
  /* 凯 is three bytes and lands in the CJK block */
  TEST_CHECK_EQ_INT(xw_utf8_next("\xe5\x87\xaf", &cp), 3);
  TEST_CHECK_EQ_INT((int)cp, 0x51EF);
  TEST_CHECK_EQ_INT(xw_utf8_next("", &cp), 0);
  TEST_CHECK_EQ_INT(xw_utf8_next("\xff", &cp), 1); /* malformed: skip a byte */
}

static void test_multiline(void) {
  const char *font = find_font();
  unsigned char *one = NULL, *two = NULL;
  int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
  XWError err = {0};
  TEST_BEGIN("a newline starts a second line instead of drawing a glyph");
  if (!font) {
    printf("  SKIP: no test font found\n");
    return;
  }
  TEST_CHECK_EQ_INT(xw_text_rasterize(font, "A", 48.0f, &one, &w1, &h1, &err), 0);
  TEST_CHECK_EQ_INT(xw_text_rasterize(font, "A\nB", 48.0f, &two, &w2, &h2, &err),
                    0);
  if (one && two) {
    TEST_CHECK(w2 <= w1 * 2 + 4); /* the widest line is still one glyph */
    TEST_CHECK(h2 > h1 * 3 / 2);  /* but the block is two lines tall */
  }
  free(one);
  free(two);
}

static void test_cjk_rasterize(void) {
  const char *font = find_cjk_font();
  unsigned char *px = NULL;
  int w = 0, h = 0, ink = 0, i;
  XWError err = {0};
  TEST_BEGIN("CJK content rasterises with ink (not byte-wise garbage)");
  if (!font) {
    printf("  SKIP: no CJK font found\n");
    return;
  }
  TEST_CHECK_EQ_INT(xw_text_rasterize(font, "\xe5\x87\xaf\xe5\xb0\x94\xe5\xb8\x8c",
                                      48.0f, &px, &w, &h, &err),
                    0);
  if (!px)
    return;
  for (i = 0; i < w * h; i++)
    if (px[i * 4 + 3] > 128)
      ink++;
  TEST_CHECK(ink > 50);
  /* three full-width glyphs at 48px are wider than a couple of Latin ones */
  TEST_CHECK(w > 100 && w < 300);
  free(px);
}

int main(void) {
  TEST_SUITE("scene_text");
  test_rasterize();
  test_utf8_decode();
  test_multiline();
  test_cjk_rasterize();
  test_empty_and_size_clamp();
  test_bad_inputs();
  TEST_SUMMARY();
}
