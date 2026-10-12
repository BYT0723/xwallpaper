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

int main(void) {
  TEST_SUITE("scene_text");
  test_rasterize();
  test_empty_and_size_clamp();
  test_bad_inputs();
  TEST_SUMMARY();
}
