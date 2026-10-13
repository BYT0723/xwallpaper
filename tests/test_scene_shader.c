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

static void test_header_shape(void) {
  TEST_BEGIN("emits GLSL 330 header, dialect and stage defines");
  char dst[8192];
  const char *src = "void main() { gl_FragColor = vec4(1.0); }\n";
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strncmp(dst, "#version 330", 12) == 0);
  TEST_CHECK(strstr(dst, "#define texSample2D texture") != NULL);
  TEST_CHECK(strstr(dst, "#define mul(x, y)") != NULL);
  TEST_CHECK(strstr(dst, "#define saturate(x)") != NULL);
  TEST_CHECK(strstr(dst, "#define CAST4(x)") != NULL);
  /* fragment stage mapping */
  TEST_CHECK(strstr(dst, "#define varying in") != NULL);
  TEST_CHECK(strstr(dst, "out vec4 out_FragColor") != NULL);
  TEST_CHECK(strstr(dst, "#define gl_FragColor out_FragColor") != NULL);
}

static void test_vertex_stage(void) {
  TEST_BEGIN("vertex stage maps attribute/varying to in/out");
  char dst[8192];
  const char *src =
      "attribute vec3 a_Position;\n"
      "varying vec2 vUv;\n"
      "void main() { vUv = a_TexCoord; }\n";
  TEST_CHECK_EQ_INT(xw_shader_translate_stage(src, dst, sizeof(dst), 1), 0);
  TEST_CHECK(strstr(dst, "#define attribute in") != NULL);
  TEST_CHECK(strstr(dst, "#define varying out") != NULL);
  TEST_CHECK(strstr(dst, "out vec4 out_FragColor") == NULL);
  TEST_CHECK(strstr(dst, "attribute vec3 a_Position") != NULL);
}

static void test_constants_documented(void) {
  TEST_BEGIN("common.h constants match the published values");
  char dst[16384];
  const char *src =
      "#include \"common.h\"\n"
      "void main() { float a = M_PI_2 + M_PI_HALF + SQRT_2; }\n";
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  /* M_PI_2 is 2*pi in Wallpaper Engine, M_PI_HALF is pi/2 */
  TEST_CHECK(strstr(dst, "#define M_PI_2 6.28318530718") != NULL);
  TEST_CHECK(strstr(dst, "#define M_PI_HALF 1.57079632679") != NULL);
  TEST_CHECK(strstr(dst, "#define M_PI 3.14159265359") != NULL);
  TEST_CHECK(strstr(dst, "#define SQRT_2 1.41421356237") != NULL);
  /* function bodies come along with the header */
  TEST_CHECK(strstr(dst, "vec2 rotateVec2(") != NULL);
  TEST_CHECK(strstr(dst, "vec3 hsv2rgb(") != NULL);
  TEST_CHECK(strstr(dst, "#include") == NULL);
}

static void test_include_resolution(void) {
  TEST_BEGIN("builtin headers resolve, unknown includes degrade");
  char dst[16384];
  const char *src =
      "#include \"common_blending.h\"\n"
      "void main() { vec3 c = ApplyBlending(BLENDMODE, vec3(0), vec3(1), 0.5); }\n";
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "vec3 ApplyBlending(") != NULL);
  TEST_CHECK(strstr(dst, "#include") == NULL);

  const char *missing = "#include \"does_not_exist.h\"\nvoid main() {}\n";
  TEST_CHECK_EQ_INT(xw_shader_translate(missing, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "#include") == NULL);
  TEST_CHECK(strstr(dst, "does_not_exist.h") != NULL);
}

static const char *resolver_cb(void *user, const char *name) {
  (void)user;
  if (strcmp(name, "custom.h") == 0)
    return "float custom_helper(float x) { return x * 2.0; }\n";
  return NULL;
}

static void test_resolver_precedence(void) {
  TEST_BEGIN("external resolver wins and is used for unknown names");
  char dst[16384];
  const char *src =
      "#include \"custom.h\"\n"
      "#include \"common.h\"\n"
      "void main() { float y = custom_helper(M_PI); }\n";
  TEST_CHECK_EQ_INT(
      xw_shader_preprocess(src, dst, sizeof(dst), 0, NULL, 0, resolver_cb, NULL),
      0);
  TEST_CHECK(strstr(dst, "custom_helper") != NULL);
  TEST_CHECK(strstr(dst, "vec2 rotateVec2(") != NULL);
}

static void test_combos(void) {
  TEST_BEGIN("combo annotations and material combos become defines");
  char dst[16384];
  const char *src =
      "// [COMBO] {\"material\":\"m\",\"combo\":\"BLENDMODE\",\"type\":\"imageblending\",\"default\":32}\n"
      "// [COMBO] {\"material\":\"m\",\"combo\":\"MASK\",\"type\":\"options\",\"default\":0}\n"
      "void main() {}\n";
  XWSceneCombo combos[1];
  memset(combos, 0, sizeof(combos));
  snprintf(combos[0].key, sizeof(combos[0].key), "BLENDMODE");
  snprintf(combos[0].value, sizeof(combos[0].value), "3");
  TEST_CHECK_EQ_INT(
      xw_shader_preprocess(src, dst, sizeof(dst), 0, combos, 1, NULL, NULL), 0);
  /* explicit material combo wins over the annotation default */
  TEST_CHECK(strstr(dst, "#define BLENDMODE 3") != NULL);
  TEST_CHECK(strstr(dst, "#define BLENDMODE 32") == NULL);
  TEST_CHECK(strstr(dst, "#define MASK 0") != NULL);
}

static void test_version_line_dropped(void) {
  TEST_BEGIN("a source #version is dropped in favour of the generated one");
  char dst[8192];
  const char *src = "#version 120\nvoid main() {}\n";
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  TEST_CHECK(strstr(dst, "#version 120") == NULL);
  TEST_CHECK(strstr(dst, "#version 330") != NULL);
}

static void test_linked_varyings(void) {
  TEST_BEGIN("vertex varying widens to vec4 when the fragment wants vec4");
  const char *frag =
      "varying vec4 v_TexCoord;\n"
      "void main() {}\n";
  char vert[512];
  snprintf(vert, sizeof(vert),
           "attribute vec2 a_TexCoord;\n"
           "varying vec2 v_TexCoord;\n"
           "void main() { v_TexCoord = a_TexCoord; }\n");
  TEST_CHECK_EQ_INT(xw_shader_fix_linked_varyings(frag, vert, sizeof(vert)), 0);
  TEST_CHECK(strstr(vert, "varying vec4 v_TexCoord;") != NULL);
  TEST_CHECK(strstr(vert, "varying vec2 v_TexCoord;") == NULL);
  /* a bare assignment gets padded to vec4 so the type stays valid */
  TEST_CHECK(strstr(vert, "v_TexCoord = vec4(a_TexCoord, 0.0, 1.0);") != NULL);

  /* swizzle assignments are already valid on a vec4 and stay untouched */
  char vert2[512];
  snprintf(vert2, sizeof(vert2),
           "varying vec2 v_TexCoord;\n"
           "void main() { v_TexCoord.xy = a_TexCoord.xy; }\n");
  TEST_CHECK_EQ_INT(xw_shader_fix_linked_varyings(frag, vert2, sizeof(vert2)), 0);
  TEST_CHECK(strstr(vert2, "varying vec4 v_TexCoord;") != NULL);
  TEST_CHECK(strstr(vert2, "v_TexCoord.xy = a_TexCoord.xy;") != NULL);

  /* matching types are left alone */
  const char *frag2 = "varying vec2 v_TexCoord;\nvoid main() {}\n";
  char vert3[512];
  snprintf(vert3, sizeof(vert3), "varying vec2 v_TexCoord;\nvoid main() {}\n");
  TEST_CHECK_EQ_INT(xw_shader_fix_linked_varyings(frag2, vert3, sizeof(vert3)), 0);
  TEST_CHECK(strstr(vert3, "varying vec2 v_TexCoord;") != NULL);
}

static void test_bad_inputs(void) {
  TEST_BEGIN("NULL/small-dst rejected");
  char dst[8];
  TEST_CHECK_EQ_INT(xw_shader_translate(NULL, dst, sizeof(dst)), -1);
  TEST_CHECK_EQ_INT(xw_shader_translate("void main(){}", NULL, 0), -1);
  TEST_CHECK_EQ_INT(xw_shader_translate("void main(){}", dst, 2), -1);
}

static void test_builtin_lookup(void) {
  TEST_BEGIN("builtin header lookup by name");
  TEST_CHECK(xw_shader_builtin_header("common.h") != NULL);
  TEST_CHECK(xw_shader_builtin_header("common_fragment.h") != NULL);
  TEST_CHECK(xw_shader_builtin_header("common_vertex.h") != NULL);
  TEST_CHECK(xw_shader_builtin_header("common_blending.h") != NULL);
  TEST_CHECK(xw_shader_builtin_header("nope.h") == NULL);
  TEST_CHECK(xw_shader_builtin_header(NULL) == NULL);
}

static void test_sample_translates(void) {
  TEST_BEGIN("sample shader translates into GLSL 330");
  char *src = slurp("tests/data/scene/shader_sample.txt");
  TEST_CHECK(src != NULL);
  if (!src) return;
  char dst[8192];
  TEST_CHECK_EQ_INT(xw_shader_translate(src, dst, sizeof(dst)), 0);
  /* legacy spellings are mapped through defines, not rewritten in place */
  TEST_CHECK(strstr(dst, "#define texture2D texture") != NULL);
  TEST_CHECK(strstr(dst, "#define texSample2D texture") != NULL);
  TEST_CHECK(strstr(dst, "#include") == NULL);
  TEST_CHECK(strstr(dst, "#version 330") != NULL);
  free(src);
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

int main(void) {
  TEST_SUITE("scene_shader");
  test_header_shape();
  test_vertex_stage();
  test_constants_documented();
  test_include_resolution();
  test_resolver_precedence();
  test_combos();
  test_version_line_dropped();
  test_linked_varyings();
  test_bad_inputs();
  test_builtin_lookup();
  test_sample_translates();
  test_collect_defaults();
  TEST_SUMMARY();
}
