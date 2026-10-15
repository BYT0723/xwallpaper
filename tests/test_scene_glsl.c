#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../src/scene_glsl.h"

static void test_available(void) {
  TEST_BEGIN("pipeline availability matches the build");
  int avail = xw_glsl_available();
  TEST_CHECK(avail == 0 || avail == 1);
  if (!avail)
    printf("  SKIP: glslang/spirv-cross not compiled in\n");
}

static void test_roundtrip(void) {
  TEST_BEGIN("WE GLSL 330 round trips through glslang and SPIRV-Cross");
  if (!xw_glsl_available()) {
    printf("  SKIP: pipeline not compiled in\n");
    return;
  }
  const char *vert =
      "#version 330\n"
      "layout(location=0) attribute vec3 a_Position;\n"
      "layout(location=1) attribute vec2 a_TexCoord;\n"
      "#define attribute in\n"
      "out vec2 v_TexCoord;\n"
      "uniform mat4 g_ModelViewProjectionMatrix;\n"
      "void main() { v_TexCoord = a_TexCoord; "
      "gl_Position = g_ModelViewProjectionMatrix * vec4(a_Position, 1.0); }\n";
  const char *frag =
      "#version 330\n"
      "in vec2 v_TexCoord;\n"
      "uniform sampler2D g_Texture0;\n"
      "uniform float g_Time;\n"
      "out vec4 out_FragColor;\n"
      "void main() { out_FragColor = texture(g_Texture0, v_TexCoord) "
      "* (0.5 + 0.5 * sin(g_Time)); }\n";
  char *vout = NULL, *fout = NULL;
  XWError err = {0};
  TEST_CHECK_EQ_INT(xw_glsl_roundtrip(vert, frag, &vout, &fout, &err), 0);
  if (!vout || !fout) {
    TEST_FAIL("round trip produced no source: %s", err.msg);
    free(vout);
    free(fout);
    return;
  }
  TEST_CHECK(strstr(vout, "#version 330") != NULL);
  TEST_CHECK(strstr(fout, "#version 330") != NULL);
  TEST_CHECK(strstr(fout, "g_Texture0") != NULL);
  TEST_CHECK(strstr(fout, "g_Time") != NULL);
  /* the normaliser assigns explicit locations/bindings for SPIR-V */
  TEST_CHECK(strstr(vout, "location") != NULL);
  free(vout);
  free(fout);
}

static void test_bad_shader(void) {
  TEST_BEGIN("invalid GLSL fails with a compiler message");
  if (!xw_glsl_available()) {
    printf("  SKIP: pipeline not compiled in\n");
    return;
  }
  const char *vert = "#version 330\nvoid main() { gl_Position = vec4(0); }\n";
  const char *frag = "#version 330\nvoid main() { this is not glsl }\n";
  char *vout = NULL, *fout = NULL;
  XWError err = {0};
  TEST_CHECK_EQ_INT(xw_glsl_roundtrip(vert, frag, &vout, &fout, &err), -1);
  TEST_CHECK(vout == NULL && fout == NULL);
  TEST_CHECK(err.code != 0 && err.msg[0] != '\0');
  TEST_CHECK_EQ_INT(xw_glsl_roundtrip(NULL, frag, &vout, &fout, &err), -1);
}

int main(void) {
  TEST_SUITE("scene_glsl");
  test_available();
  test_roundtrip();
  test_bad_shader();
  TEST_SUMMARY();
}
