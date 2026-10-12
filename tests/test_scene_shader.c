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

static void test_prelude_unconditional(void) {
  TEST_BEGIN("builtins present even without #include");
  const char *src =
      "uniform float g_Time;\n"
      "void main() { float a = saturate(frac(M_PI_2 * g_Time)); }\n";
  char dst[4096];
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "#define saturate(x)") != NULL);
  TEST_CHECK(strstr(dst, "#define frac(x)") != NULL);
  TEST_CHECK(strstr(dst, "#define M_PI_2") != NULL);
  TEST_CHECK(strstr(dst, "ApplyBlending") != NULL);
}

static void test_combo_defaults(void) {
  TEST_BEGIN("combo annotation defaults become defines");
  const char *src =
      "// [COMBO] {\"material\":\"ui_editor_properties_blend_mode\",\"combo\":\"BLENDMODE\",\"type\":\"imageblending\",\"default\":32}\n"
      "// [COMBO] {\"material\":\"x\",\"combo\":\"MASK\",\"type\":\"options\",\"default\":0}\n"
      "void main() {}\n";
  char dst[4096];
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "#define BLENDMODE 32") != NULL);
  TEST_CHECK(strstr(dst, "#define MASK 0") != NULL);
}

static void test_collect_defaults(void) {
  TEST_BEGIN("annotated uniform defaults collected with material keys");
  const char *src =
      "uniform float g_Speed; // {\"material\":\"speed\",\"default\":1}\n"
      "uniform vec2 g_Friction; // {\"material\":\"friction\",\"default\":\"1 1\"}\n"
      "uniform vec3 g_Color; // {\"material\":\"color\",\"default\":\"1 0.5 0.25\"}\n"
      "uniform sampler2D g_Texture0; // {\"material\":\"framebuffer\"}\n"
      "uniform float g_Plain;\n";
  XWShaderDef defs[XW_SHADER_DEFS_MAX];
  int n = 0, i;
  const XWShaderDef *sp = NULL, *fr = NULL, *co = NULL;
  TEST_CHECK_EQ_INT(xw_shader_collect_defaults(src, defs, &n, XW_SHADER_DEFS_MAX), 0);
  TEST_CHECK_EQ_INT(n, 3);
  for (i = 0; i < n; i++) {
    if (strcmp(defs[i].name, "g_Speed") == 0)
      sp = &defs[i];
    if (strcmp(defs[i].name, "g_Friction") == 0)
      fr = &defs[i];
    if (strcmp(defs[i].name, "g_Color") == 0)
      co = &defs[i];
  }
  TEST_CHECK(sp != NULL && fr != NULL && co != NULL);
  if (sp) {
    TEST_CHECK_EQ_INT(sp->kind, 1);
    TEST_CHECK_EQ_STR(sp->material, "speed");
    TEST_CHECK(sp->v[0] > 0.99 && sp->v[0] < 1.01);
  }
  if (fr) {
    TEST_CHECK_EQ_INT(fr->kind, 2);
    TEST_CHECK(fr->v[0] > 0.99 && fr->v[1] > 0.99);
  }
  if (co) {
    TEST_CHECK_EQ_INT(co->kind, 3);
    TEST_CHECK(co->v[0] > 0.99 && co->v[1] > 0.49 && co->v[1] < 0.51 &&
               co->v[2] > 0.24 && co->v[2] < 0.26);
  }
  TEST_CHECK_EQ_INT(xw_shader_collect_defaults(NULL, defs, &n, 4), -1);
}

static void test_vertex_stage(void) {
  TEST_BEGIN("vertex stage maps varying to out, injects prelude");
  const char *src =
      "#include \"common.h\"\n"
      "attribute vec3 a_Position;\n"
      "varying vec2 vUv;\n"
      "void main() { vUv = a_TexCoord; gl_Position = mul(vec4(a_Position, 1.0), m); }\n";
  char dst[4096];
  TEST_CHECK_EQ_INT(xw_shader_translate_stage(src, dst, sizeof(dst), 1), 0);
  TEST_CHECK(strstr(dst, "out vec2 vUv") != NULL);
  TEST_CHECK(strstr(dst, "texSample2D(s, uv)") != NULL);
  TEST_CHECK(strstr(dst, "#include") == NULL);
  /* fragment default keeps the old mapping */
  TEST_CHECK_EQ_INT(
      xw_shader_translate("varying vec2 vUv;", dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "in vec2 vUv") != NULL);
}

int main(void) {
  TEST_SUITE("scene_shader");
  test_sample_translates();
  test_keywords_rewritten();
  test_bad_inputs();
  test_vertex_stage();
  test_prelude_unconditional();
  test_combo_defaults();
  test_collect_defaults();
  TEST_SUMMARY();
}
