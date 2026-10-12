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
  TEST_SUMMARY();
}
