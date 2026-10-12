#include <string.h>

#include "harness.h"
#include "../src/scene_core.h"
#include "../src/scene_model.h"

static void test_fbo_table(void) {
  TEST_BEGIN("create/find/alias/duplicate/missing");
  XWFboTable t;
  xw_fbo_table_init(&t);
  TEST_CHECK_EQ_INT(xw_fbo_create(&t, "_rt_FullFrameBuffer", 3840, 2160, 1.0f, 7), 0);
  TEST_CHECK_EQ_INT(xw_fbo_alias(&t, "_rt_MipMappedFrameBuffer",
                                 "_rt_FullFrameBuffer"), 0);
  const XWFbo *a = xw_fbo_find(&t, "_rt_FullFrameBuffer");
  const XWFbo *b = xw_fbo_find(&t, "_rt_MipMappedFrameBuffer");
  TEST_CHECK(a != NULL && b != NULL);
  if (a && b) {
    TEST_CHECK_EQ_INT((int)a->w, 3840);
    TEST_CHECK_EQ_INT((int)a->id, 7);
    TEST_CHECK_EQ_INT((int)b->id, 7);
  }
  TEST_CHECK_EQ_INT(xw_fbo_create(&t, "_rt_FullFrameBuffer", 1, 1, 1.0f, 9), -1);
  TEST_CHECK_EQ_INT(xw_fbo_alias(&t, "x", "missing"), -1);
  TEST_CHECK(xw_fbo_find(&t, "missing") == NULL);
  TEST_CHECK(xw_fbo_find(NULL, "x") == NULL);
}

static void test_camera_ortho(void) {
  TEST_BEGIN("ortho matrix maps edges to NDC");
  float m[16];
  xw_camera_ortho(0.0f, 4.0f, 0.0f, 2.0f, m);
  TEST_CHECK(m[0] > 0.499 && m[0] < 0.501);
  TEST_CHECK(m[5] > 0.999 && m[5] < 1.001);
  TEST_CHECK(m[12] > -1.001 && m[12] < -0.999);
  TEST_CHECK(m[13] > -1.001 && m[13] < -0.999);
  TEST_CHECK(m[15] > 0.999 && m[15] < 1.001);
}

static void test_camera_auto_size(void) {
  TEST_BEGIN("auto size covers objects, falls back to window");
  XWSceneObject o[2];
  memset(o, 0, sizeof(o));
  o[0].origin[0] = 1920.0f;
  o[0].origin[1] = 1080.0f;
  o[0].size[0] = 3924.0f;
  o[0].size[1] = 2208.0f;
  float w = 0, h = 0;
  xw_camera_auto_size(o, 1, 1920.0f, 1080.0f, &w, &h);
  TEST_CHECK_EQ_INT((int)w, 7764);
  TEST_CHECK_EQ_INT((int)h, 4368);
  xw_camera_auto_size(NULL, 0, 1920.0f, 1080.0f, &w, &h);
  TEST_CHECK_EQ_INT((int)w, 1920);
  TEST_CHECK_EQ_INT((int)h, 1080);
}

static void test_blend_modes(void) {
  TEST_BEGIN("three blend states map to separate funcs");
  int sRGB, dRGB, sA, dA;
  xw_blend_funcs(XW_BLEND_NORMAL, &sRGB, &dRGB, &sA, &dA);
  TEST_CHECK_EQ_INT(sRGB, XW_GL_ONE);
  TEST_CHECK_EQ_INT(dRGB, XW_GL_ZERO);
  xw_blend_funcs(XW_BLEND_TRANSLUCENT, &sRGB, &dRGB, &sA, &dA);
  TEST_CHECK_EQ_INT(sRGB, XW_GL_SRC_ALPHA);
  TEST_CHECK_EQ_INT(dRGB, XW_GL_ONE_MINUS_SRC_ALPHA);
  xw_blend_funcs(XW_BLEND_ADDITIVE, &sRGB, &dRGB, &sA, &dA);
  TEST_CHECK_EQ_INT(sRGB, XW_GL_SRC_ALPHA);
  TEST_CHECK_EQ_INT(dRGB, XW_GL_ONE);
}

static void test_present_bloom(void) {
  TEST_BEGIN("present UVs full quad, bloom flag passthrough");
  float u0 = -1, v0 = -1, u1 = -1, v1 = -1;
  TEST_CHECK_EQ_INT(xw_core_present_uvs(&u0, &v0, &u1, &v1), 0);
  TEST_CHECK(u0 == 0.0f && v0 == 0.0f && u1 == 1.0f && v1 == 1.0f);
  XWSceneCamera on, off;
  memset(&on, 0, sizeof(on));
  memset(&off, 0, sizeof(off));
  on.bloom = 1;
  TEST_CHECK_EQ_INT(xw_bloom_enabled(&on), 1);
  TEST_CHECK_EQ_INT(xw_bloom_enabled(&off), 0);
  TEST_CHECK_EQ_INT(xw_bloom_enabled(NULL), 0);
}

int main(void) {
  TEST_SUITE("scene_core");
  test_fbo_table();
  test_camera_ortho();
  test_camera_auto_size();
  test_blend_modes();
  test_present_bloom();
  TEST_SUMMARY();
}
