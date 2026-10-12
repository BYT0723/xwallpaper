#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../src/scene_model.h"
#include "../src/scene_parse.h"

static void test_parse_fixture(void) {
  TEST_BEGIN("fixture parses camera + 2 objects");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0) return;
  TEST_CHECK_EQ_INT((int)s.camera.ortho_w, 3840);
  TEST_CHECK_EQ_INT((int)s.camera.ortho_h, 2160);
  TEST_CHECK(s.camera.clearcolor[0] > 0.69 && s.camera.clearcolor[0] < 0.71);
  TEST_CHECK_EQ_INT(s.camera.bloom, 0);
  TEST_CHECK_EQ_INT(s.camera.parallax, 0);
  TEST_CHECK_EQ_INT(s.n_objects, 2);
  TEST_CHECK_EQ_STR(s.objects[0].id, "bg");
  TEST_CHECK_EQ_STR(s.objects[0].model, "models/bg.json");
  TEST_CHECK_EQ_STR(s.objects[0].material, "materials/bg.json");
  TEST_CHECK_EQ_INT((int)s.objects[0].origin[0], 1920);
  TEST_CHECK_EQ_INT((int)s.objects[0].origin[1], 1080);
  TEST_CHECK_EQ_INT((int)s.objects[0].size[0], 3924);
  TEST_CHECK_EQ_INT((int)s.objects[0].size[1], 2208);
  TEST_CHECK_EQ_INT(s.objects[0].visible, 1);
  TEST_CHECK_EQ_INT(s.objects[0].n_effects, 2);
  TEST_CHECK_EQ_STR(s.objects[0].effects[0].name, "shake");
  TEST_CHECK_EQ_INT(s.objects[0].effects[0].n_passes, 1);
  TEST_CHECK_EQ_STR(s.objects[0].effects[0].passes[0].shader,
                    "shaders/shake.frag");
  TEST_CHECK_EQ_INT(s.objects[0].effects[0].passes[0].n_combos, 1);
  TEST_CHECK_EQ_STR(s.objects[0].effects[0].passes[0].combos[0].key, "MASK");
  TEST_CHECK_EQ_INT(s.objects[0].effects[1].n_passes, 2);
  TEST_CHECK_EQ_INT(s.objects[0].effects[1].passes[0].n_binds, 2);
  TEST_CHECK_EQ_INT(s.objects[1].visible, 0);
  TEST_CHECK_EQ_INT(s.objects[1].n_effects, 0);
  xw_scene_free(&s);
}

static void test_full_types(void) {
  TEST_BEGIN("full dir parses 4 typed objects with payloads");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene/full", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0) return;
  TEST_CHECK_EQ_INT(s.n_objects, 4);
  if (s.n_objects != 4) {
    xw_scene_free(&s);
    return;
  }
  TEST_CHECK_EQ_INT(s.objects[0].type, XW_OBJ_IMAGE);
  TEST_CHECK_EQ_INT(s.objects[0].alpha.kind, XW_DYN_USER);
  TEST_CHECK_EQ_STR(s.objects[0].alpha.ref, "opacity");
  TEST_CHECK_EQ_INT(s.objects[1].type, XW_OBJ_PARTICLE);
  TEST_CHECK_EQ_STR(s.objects[1].id, "sparks");
  TEST_CHECK(s.objects[1].particle.rate > 59.0 &&
             s.objects[1].particle.rate < 61.0);
  TEST_CHECK_EQ_INT(s.objects[1].particle.max_count, 500);
  TEST_CHECK_EQ_STR(s.objects[1].particle.sprite, "particles/spark.png");
  TEST_CHECK_EQ_INT(s.objects[1].alpha.kind, XW_DYN_CONSTANT);
  TEST_CHECK_EQ_INT(s.objects[2].type, XW_OBJ_TEXT);
  TEST_CHECK_EQ_STR(s.objects[2].text.content, "Hello");
  TEST_CHECK_EQ_STR(s.objects[2].text.font, "fonts/roboto.ttf");
  TEST_CHECK_EQ_INT(s.objects[3].type, XW_OBJ_SOUND);
  TEST_CHECK_EQ_STR(s.objects[3].sound.file, "audio/bgm.ogg");
  TEST_CHECK_EQ_INT(s.objects[3].sound.loop, 1);
  xw_scene_free(&s);
}

static void test_full_props_script(void) {
  TEST_BEGIN("project.json props + scene script entry");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene/full", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0) return;
  TEST_CHECK_EQ_STR(s.script, "main.js");
  TEST_CHECK_EQ_INT(s.n_props, 3);
  if (s.n_props == 3) {
    TEST_CHECK_EQ_STR(s.props[0].name, "opacity");
    TEST_CHECK_EQ_STR(s.props[0].type, "slider");
    TEST_CHECK_EQ_STR(s.props[2].name, "schemecolor");
    TEST_CHECK_EQ_STR(s.props[2].value, "0.15 0.23 0.4");
  }
  xw_scene_free(&s);

  TEST_BEGIN("dir without project.json parses with empty props");
  memset(&s, 0, sizeof(s));
  rc = xw_scene_parse("tests/data/scene", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0) return;
  TEST_CHECK_EQ_INT(s.n_props, 0);
  TEST_CHECK_EQ_STR(s.script, "");
  TEST_CHECK_EQ_INT(s.objects[0].alpha.kind, XW_DYN_CONSTANT);
  xw_scene_free(&s);
}

static void test_bad_inputs(void) {
  TEST_BEGIN("missing dir/file rejected, no crash");
  XWScene s;
  XWError err = {0};
  TEST_CHECK_EQ_INT(xw_scene_parse("/nonexistent/dir", &s, &err), -1);
  TEST_CHECK_EQ_INT(xw_scene_parse(NULL, &s, &err), -1);
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data/scene", NULL, &err), -1);
  xw_scene_free(NULL);
  memset(&s, 0, sizeof(s));
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data", &s, &err), -1);
}

int main(void) {
  TEST_SUITE("scene_parse");
  test_parse_fixture();
  test_bad_inputs();
  test_full_types();
  test_full_props_script();
  TEST_SUMMARY();
}
