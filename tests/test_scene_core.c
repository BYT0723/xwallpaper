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
  TEST_BEGIN("bloom flag passthrough");
  XWSceneCamera on, off;
  memset(&on, 0, sizeof(on));
  memset(&off, 0, sizeof(off));
  on.bloom = 1;
  TEST_CHECK_EQ_INT(xw_bloom_enabled(&on), 1);
  TEST_CHECK_EQ_INT(xw_bloom_enabled(&off), 0);
  TEST_CHECK_EQ_INT(xw_bloom_enabled(NULL), 0);
}

static void test_particle_texture_ratio(void) {
  TEST_BEGIN("sprite-sheet texture ratio is the per-frame aspect");
  /* drizzle: a 1024x512 atlas of 8 frames, 4x2, so each frame is 256x256 and
   * the ratio the shader wants is 1.0 - not the atlas' 512/1024 = 0.5, which
   * would halve the trail's length axis */
  TEST_CHECK(xw_particle_texture_ratio(1024, 512, 1024, 512, 0.25f, 0.5f, 1) >
             0.99f);
  TEST_CHECK(xw_particle_texture_ratio(1024, 512, 1024, 512, 0.25f, 0.5f, 1) <
             1.01f);
  /* a plain (non-sheet) texture keeps the real height/width */
  TEST_CHECK(xw_particle_texture_ratio(512, 256, 512, 256, 0.0f, 0.0f, 0) >
             0.49f);
  TEST_CHECK(xw_particle_texture_ratio(512, 256, 512, 256, 0.0f, 0.0f, 0) <
             0.51f);
  /* a square plain texture is 1.0 */
  TEST_CHECK(xw_particle_texture_ratio(256, 256, 256, 256, 0.0f, 0.0f, 0) >
             0.99f);
  /* a wide sheet of 2x1 frames on a 512x256 atlas: frames are 256x256 */
  TEST_CHECK(xw_particle_texture_ratio(512, 256, 512, 256, 0.5f, 1.0f, 1) >
             0.99f);
  TEST_CHECK(xw_particle_texture_ratio(512, 256, 512, 256, 0.5f, 1.0f, 1) <
             1.01f);
}

static void test_render_order(void) {
  XWSceneObject o[6];
  int order[6];
  unsigned char scratch[6];
  TEST_BEGIN("dependencies render before their dependents (DFS post-order)");
  memset(o, 0, sizeof(o));
  strcpy(o[0].id, "10");
  strcpy(o[1].id, "11");
  strcpy(o[1].deps[0], "13");
  o[1].n_deps = 1;
  strcpy(o[2].id, "12");
  strcpy(o[2].deps[0], "11");
  strcpy(o[2].deps[1], "13");
  o[2].n_deps = 2;
  strcpy(o[3].id, "13");
  strcpy(o[4].id, "14"); /* self-dependency is ignored */
  strcpy(o[4].deps[0], "14");
  o[4].n_deps = 1;
  strcpy(o[5].id, "15"); /* unknown dependency is ignored */
  strcpy(o[5].deps[0], "999");
  strcpy(o[5].deps[1], "10"); /* and an already-emitted one is not repeated */
  o[5].n_deps = 2;
  TEST_CHECK_EQ_INT(xw_scene_render_order(o, 6, order, scratch), 6);
  TEST_CHECK_EQ_INT(order[0], 0);
  TEST_CHECK_EQ_INT(order[1], 3); /* 11's dependency, emitted before 11 */
  TEST_CHECK_EQ_INT(order[2], 1);
  TEST_CHECK_EQ_INT(order[3], 2);
  TEST_CHECK_EQ_INT(order[4], 4);
  TEST_CHECK_EQ_INT(order[5], 5);
  /* a plain scene keeps its order */
  o[1].n_deps = 0;
  o[2].n_deps = 0;
  TEST_CHECK_EQ_INT(xw_scene_render_order(o, 6, order, scratch), 6);
  TEST_CHECK_EQ_INT(order[0], 0);
  TEST_CHECK_EQ_INT(order[1], 1);
  TEST_CHECK_EQ_INT(order[5], 5);
}

static void test_text_place(void) {
  float dx = 0, dy = 0, sc = 0;
  TEST_BEGIN("text anchors scale from canvas pixels into window pixels");
  /* the engine's own comment in CText: anchors are canvas pixels with Y
   * down, so a 3440x1440 canvas in a 1920x1080 window scales them straight */
  xw_text_place(0.0f, 0.0f, 3440.0f, 1440.0f, 1920, 1080, &dx, &dy, &sc);
  TEST_CHECK(dx > -0.01f && dx < 0.01f);
  TEST_CHECK(dy > -0.01f && dy < 0.01f);
  TEST_CHECK(sc > 0.74f && sc < 0.76f); /* 1080/1440 */
  /* 2887099508's clock sits at canvas (5476, 475) on a 6080x3420 canvas */
  xw_text_place(5476.0f, 475.0f, 6080.0f, 3420.0f, 1920, 1080, &dx, &dy, &sc);
  TEST_CHECK(dx > 1728.0f && dx < 1730.0f);
  TEST_CHECK(dy > 149.0f && dy < 151.0f);
  /* a canvas that matches the window maps 1:1 */
  xw_text_place(200.0f, 150.0f, 1920.0f, 1080.0f, 1920, 1080, &dx, &dy, &sc);
  TEST_CHECK(dx > 199.9f && dx < 200.1f);
  TEST_CHECK(dy > 149.9f && dy < 150.1f);
  TEST_CHECK(sc > 0.99f && sc < 1.01f);
  /* degenerate canvases fall back to a 1:1 mapping */
  xw_text_place(10.0f, 20.0f, 0.0f, 0.0f, 1920, 1080, &dx, &dy, &sc);
  TEST_CHECK(dx > 9.9f && dx < 10.1f);
  TEST_CHECK(dy > 19.9f && dy < 20.1f);
  TEST_CHECK(sc > 0.99f && sc < 1.01f);
}

static void test_object_rect(void) {
  XWSceneObject o;
  int x = 0, y = 0, w = 0, h = 0;
  TEST_BEGIN("image quads use the texture size, centred on origin with Y up");
  /* measured against the engine with a probe wallpaper: a 3840x2160 texture
   * scaled 0.1, origin (480, 270), 1920x1080 canvas in a 1920x1080 window.
   * The scene's own "size" field is ignored - the texture decides. */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 0.1f;
  o.origin[0] = 480.0f;
  o.origin[1] = 270.0f;
  xw_scene_object_rect(&o, 3840, 2160, 1920.0f, 1080.0f, 1920, 1080, &x, &y,
                       &w, &h);
  TEST_CHECK_EQ_INT(x, 288);
  TEST_CHECK_EQ_INT(y, 702);
  TEST_CHECK_EQ_INT(w, 384);
  TEST_CHECK_EQ_INT(h, 216);
  /* probe: same texture at scale 0.25 on a 3840x2160 canvas shown in a
   * 1920x1080 window - canvas coordinates and sizes both halve */
  o.scale[0] = o.scale[1] = 0.25f;
  o.origin[0] = 960.0f;
  o.origin[1] = 540.0f;
  xw_scene_object_rect(&o, 3840, 2160, 3840.0f, 2160.0f, 1920, 1080, &x, &y,
                       &w, &h);
  TEST_CHECK_EQ_INT(x, 240);
  TEST_CHECK_EQ_INT(y, 675);
  TEST_CHECK_EQ_INT(w, 480);
  TEST_CHECK_EQ_INT(h, 270);
  /* 2178468632's night blue: origin.x 948.114 is 12px left of centre, so the
   * quad starts outside the canvas (the engine clips it there) */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.origin[0] = 948.114f;
  o.origin[1] = 540.0f;
  xw_scene_object_rect(&o, 1920, 1080, 1920.0f, 1080.0f, 1920, 1080, &x, &y,
                       &w, &h);
  TEST_CHECK_EQ_INT(x, -12);
  TEST_CHECK_EQ_INT(y, 0);
  TEST_CHECK_EQ_INT(w, 1920);
  TEST_CHECK_EQ_INT(h, 1080);
  /* A compose layer carries no texture of its own and declares its size; the
   * engine returns image.size there (CImage::getSize). Sizing those from the
   * window instead is what left 3808922316 as narrow strips: its
   * projectlayer and composelayers are 3840x2160 and are the layers that
   * cover the canvas. */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.origin[0] = 1920.0f;
  o.origin[1] = 1080.0f;
  o.size[0] = 3840.0f;
  o.size[1] = 2160.0f;
  xw_scene_object_rect(&o, 0, 0, 3840.0f, 2160.0f, 1920, 1080, &x, &y, &w, &h);
  TEST_CHECK_EQ_INT(x, 0);
  TEST_CHECK_EQ_INT(y, 0);
  TEST_CHECK_EQ_INT(w, 1920);
  TEST_CHECK_EQ_INT(h, 1080);
  /* with no texture at all the quad falls back to the window */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.origin[0] = 960.0f;
  o.origin[1] = 540.0f;
  xw_scene_object_rect(&o, 0, 0, 1920.0f, 1080.0f, 1920, 1080, &x, &y, &w, &h);
  TEST_CHECK_EQ_INT(x, 0);
  TEST_CHECK_EQ_INT(y, 0);
  TEST_CHECK_EQ_INT(w, 1920);
  TEST_CHECK_EQ_INT(h, 1080);
  /* negative scale mirrors upstream (vertices from the inverted quad, same
   * UVs), so the covered rect is identical to the positive one; refusing it
   * aborted the whole frame on 2889253135's lens-flare layers */
  memset(&o, 0, sizeof(o));
  o.scale[0] = -1.0f;
  o.scale[1] = -1.0f;
  o.origin[0] = 948.114f;
  o.origin[1] = 540.0f;
  xw_scene_object_rect(&o, 1920, 1080, 1920.0f, 1080.0f, 1920, 1080, &x, &y,
                       &w, &h);
  TEST_CHECK_EQ_INT(x, -12);
  TEST_CHECK_EQ_INT(y, 0);
  TEST_CHECK_EQ_INT(w, 1920);
  TEST_CHECK_EQ_INT(h, 1080);
}

static void test_object_ndc_rect(void) {
  XWSceneObject o;
  float ndc[8];
  TEST_BEGIN("passthrough layers see their own rectangle in NDC");
  /* a canvas-sized object centred on a matching canvas is the identity, which
   * is why the compose layers of 2178468632 do not move the frame */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.size[0] = 1920.0f;
  o.size[1] = 1080.0f;
  o.origin[0] = 960.0f;
  o.origin[1] = 540.0f;
  xw_scene_object_ndc_rect(&o, 1920.0f, 1080.0f, ndc);
  TEST_CHECK(ndc[0] > -1.001f && ndc[0] < -0.999f);
  TEST_CHECK(ndc[1] > -1.001f && ndc[1] < -0.999f);
  TEST_CHECK(ndc[6] > 0.999f && ndc[6] < 1.001f);
  TEST_CHECK(ndc[7] > 0.999f && ndc[7] < 1.001f);
  /* 2887099508's "adjustable compose layer": size 100x100 scaled to about
   * 3928x934, sitting near the bottom-left of a 6080x3420 canvas */
  memset(&o, 0, sizeof(o));
  o.size[0] = 100.0f;
  o.size[1] = 100.0f;
  o.scale[0] = 39.28412f;
  o.scale[1] = 9.338f;
  o.origin[0] = 3229.89136f;
  o.origin[1] = 189.82330f;
  xw_scene_object_ndc_rect(&o, 6080.0f, 3420.0f, ndc);
  TEST_CHECK(ndc[0] > -0.60f && ndc[0] < -0.57f);
  TEST_CHECK(ndc[2] > 0.69f && ndc[2] < 0.72f);
  TEST_CHECK(ndc[1] > -1.18f && ndc[1] < -1.14f); /* below the canvas */
  TEST_CHECK(ndc[5] > -0.63f && ndc[5] < -0.60f);
  /* a degenerate canvas falls back to the whole quad */
  xw_scene_object_ndc_rect(&o, 0.0f, 0.0f, ndc);
  TEST_CHECK(ndc[0] > -1.001f && ndc[0] < -0.999f);
  TEST_CHECK(ndc[7] > 0.999f && ndc[7] < 1.001f);
}

static void test_object_ndc_rect_rotation(void) {
  XWSceneObject o;
  float ndc[8];
  TEST_BEGIN("object angles rotate the rectangle about its own centre");
  /* 2889253135 "Dust motes" is a 400x200 object on a 1920x1080 canvas with
   * angles "0.000 0.000 1.5708" — a quarter turn, so the half extents swap
   * axes. The engine negates the sign to account for the Y-flipped
   * coordinate system (see CImage::updateScreenSpacePosition). */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.size[0] = 400.0f;
  o.size[1] = 200.0f;
  o.origin[0] = 960.0f;
  o.origin[1] = 540.0f;
  o.angles[2] = 1.5707963f;
  xw_scene_object_ndc_rect(&o, 1920.0f, 1080.0f, ndc);
  /* half extents before rotation: 200/960 = 0.208333, 100/540 = 0.185185 */
  TEST_CHECK(ndc[0] > -0.1872f && ndc[0] < -0.1832f); /* -0.185185 */
  TEST_CHECK(ndc[1] > 0.2063f && ndc[1] < 0.2103f);   /* +0.208333 */
  TEST_CHECK(ndc[2] > -0.1872f && ndc[2] < -0.1832f);
  TEST_CHECK(ndc[3] > -0.2103f && ndc[3] < -0.2063f);
  TEST_CHECK(ndc[4] > 0.1832f && ndc[4] < 0.1872f);
  TEST_CHECK(ndc[5] > 0.2063f && ndc[5] < 0.2103f);
  TEST_CHECK(ndc[6] > 0.1832f && ndc[6] < 0.1872f);
  TEST_CHECK(ndc[7] > -0.2103f && ndc[7] < -0.2063f);
  /* an eighth turn pins the direction: a sign error would put corner 0 on
   * the opposite side of the centre */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.size[0] = 200.0f;
  o.size[1] = 200.0f;
  o.origin[0] = 960.0f;
  o.origin[1] = 540.0f;
  o.angles[2] = 0.7853982f; /* pi/4 */
  xw_scene_object_ndc_rect(&o, 1920.0f, 1080.0f, ndc);
  TEST_CHECK(ndc[0] > -0.2066f && ndc[0] < -0.2026f); /* -0.204610 */
  TEST_CHECK(ndc[1] > -0.0593f && ndc[1] < -0.0553f);  /* -0.057296 */
  /* zero angles leaves the rectangle exactly where it was */
  memset(&o, 0, sizeof(o));
  o.scale[0] = o.scale[1] = 1.0f;
  o.size[0] = 400.0f;
  o.size[1] = 200.0f;
  o.origin[0] = 960.0f;
  o.origin[1] = 540.0f;
  xw_scene_object_ndc_rect(&o, 1920.0f, 1080.0f, ndc);
  TEST_CHECK(ndc[0] > -0.2104f && ndc[0] < -0.2063f); /* cx - hw = -0.208333 */
  TEST_CHECK(ndc[1] > -0.1872f && ndc[1] < -0.1832f); /* cy - hh = -0.185185 */
  TEST_CHECK(ndc[6] > 0.2063f && ndc[6] < 0.2104f);
  TEST_CHECK(ndc[7] > 0.1832f && ndc[7] < 0.1872f);
}

int main(void) {
  TEST_SUITE("scene_core");
  test_fbo_table();
  test_camera_ortho();
  test_camera_auto_size();
  test_blend_modes();
  test_present_bloom();
  test_particle_texture_ratio();
  test_render_order();
  test_text_place();
  test_object_rect();
  test_object_ndc_rect();
  test_object_ndc_rect_rotation();
  TEST_SUMMARY();
}
