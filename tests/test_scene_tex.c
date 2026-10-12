#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  TEST_SUMMARY();
}
