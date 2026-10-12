#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../src/scene_shader.h"

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 65536) {
    fclose(f);
    return NULL;
  }
  char *b = malloc((size_t)sz + 1);
  if (!b) {
    fclose(f);
    return NULL;
  }
  if (fread(b, 1, (size_t)sz, f) != (size_t)sz) {
    free(b);
    fclose(f);
    return NULL;
  }
  b[sz] = '\0';
  fclose(f);
  return b;
}

static void test_sample_translates(void) {
  TEST_BEGIN("sample shader translates without banned words");
  char *src = slurp("tests/data/scene/shader_sample.txt");
  TEST_CHECK(src != NULL);
  if (!src) return;
  char dst[4096];
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "gl_FragColor") == NULL);
  TEST_CHECK(strstr(dst, "attribute") == NULL);
  TEST_CHECK(strstr(dst, "varying") == NULL);
  TEST_CHECK(strstr(dst, "texture2D") == NULL);
  TEST_CHECK(strstr(dst, "xw_FragColor") != NULL);
  free(src);
}

static void test_keywords_rewritten(void) {
  TEST_BEGIN("attribute/varying/texture2D rewritten, include expanded");
  const char *src =
      "#include \"common.h\"\n"
      "attribute vec2 pos; varying vec2 uv;\n"
      "void main() { gl_FragColor = texture2D(tex, uv); }\n";
  char dst[4096];
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "#include") == NULL);
  TEST_CHECK(strstr(dst, "in vec2 pos") != NULL);
  TEST_CHECK(strstr(dst, "texture(tex, uv)") != NULL);
}

static void test_bad_inputs(void) {
  TEST_BEGIN("NULL/small-dst rejected");
  char dst[8];
  TEST_CHECK_EQ_INT(xw_shader_translate(NULL, dst, sizeof(dst)), -1);
  TEST_CHECK_EQ_INT(xw_shader_translate("void main(){}", NULL, 0), -1);
  TEST_CHECK_EQ_INT(xw_shader_translate("void main(){}", dst, 2), -1);
}

int main(void) {
  TEST_SUITE("scene_shader");
  test_sample_translates();
  test_keywords_rewritten();
  test_bad_inputs();
  TEST_SUMMARY();
}
