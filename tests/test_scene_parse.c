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
  TEST_CHECK(s.camera.clearcolor[1] > 0.69 && s.camera.clearcolor[1] < 0.71);
  TEST_CHECK(s.camera.clearcolor[2] > 0.69 && s.camera.clearcolor[2] < 0.71);
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
  TEST_CHECK(s.objects[1].particle.life > 1.99 &&
             s.objects[1].particle.life < 2.01);
  TEST_CHECK(s.objects[1].particle.speed > 119.0 &&
             s.objects[1].particle.speed < 121.0);
  TEST_CHECK(s.objects[1].particle.size_min > 1.99 &&
             s.objects[1].particle.size_min < 2.01);
  TEST_CHECK(s.objects[1].particle.size_max > 4.99 &&
             s.objects[1].particle.size_max < 5.01);
  TEST_CHECK(s.objects[1].particle.gravity > 49.0 &&
             s.objects[1].particle.gravity < 51.0);
  TEST_CHECK(s.objects[1].particle.color[0] > 0.99 &&
             s.objects[1].particle.color[1] > 0.79 &&
             s.objects[1].particle.color[1] < 0.81);
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

static void test_gl_texture(void) {
  TEST_BEGIN("gl fixture parses texture ref and effect shader path");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene/gl", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0) return;
  TEST_CHECK_EQ_INT(s.n_objects, 1);
  if (s.n_objects != 1) {
    xw_scene_free(&s);
    return;
  }
  TEST_CHECK_EQ_INT(s.objects[0].type, XW_OBJ_IMAGE);
  TEST_CHECK_EQ_STR(s.objects[0].texture, "textures/bg.png");
  TEST_CHECK_EQ_INT(s.objects[0].n_effects, 1);
  TEST_CHECK_EQ_STR(s.objects[0].effects[0].name, "pulse");
  TEST_CHECK_EQ_INT(s.objects[0].effects[0].n_passes, 1);
  TEST_CHECK_EQ_STR(s.objects[0].effects[0].passes[0].shader,
                    "shaders/pulse.frag");
  xw_scene_free(&s);
}

static void test_realrain(void) {
  TEST_BEGIN("real workshop dialect: 13 objects, camera, chains");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene/realrain", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0)
    return;
  TEST_CHECK_EQ_INT(s.n_objects, 13);
  TEST_CHECK_EQ_INT((int)s.camera.ortho_w, 1920);
  TEST_CHECK_EQ_INT((int)s.camera.ortho_h, 1080);
  TEST_CHECK(s.camera.clearcolor[0] > 0.69 && s.camera.clearcolor[0] < 0.71);
  TEST_CHECK_EQ_INT(s.camera.bloom, 0);
  /* night blue: name id, model->material chain, tex ref, blend */
  TEST_CHECK_EQ_STR(s.objects[0].id, "night blue");
  TEST_CHECK_EQ_INT(s.objects[0].type, XW_OBJ_IMAGE);
  TEST_CHECK_EQ_STR(s.objects[0].model, "models/night blue.json");
  TEST_CHECK_EQ_STR(s.objects[0].material, "materials/night blue.json");
  TEST_CHECK_EQ_STR(s.objects[0].mat_tex, "night blue");
  TEST_CHECK_EQ_INT(s.objects[0].blend, XW_BLEND_TRANSLUCENT);
  TEST_CHECK_EQ_INT((int)s.objects[0].origin[0], 948);
  TEST_CHECK_EQ_INT((int)s.objects[0].origin[1], 540);
  TEST_CHECK_EQ_INT((int)s.objects[0].size[0], 1920);
  /* rain particle + sound shape */
  {
    const XWSceneObject *rain = NULL, *snd = NULL;
    for (int i = 0; i < s.n_objects; i++) {
      if (strcmp(s.objects[i].id, "Rain Downfall light") == 0)
        rain = &s.objects[i];
      if (strcmp(s.objects[i].id, "the-rain-sound_102bpm.wav") == 0)
        snd = &s.objects[i];
    }
    TEST_CHECK(rain != NULL && snd != NULL);
    if (rain) {
      TEST_CHECK_EQ_INT(rain->type, XW_OBJ_PARTICLE);
      TEST_CHECK_EQ_INT(rain->visible, 1);
    }
    if (snd) {
      TEST_CHECK_EQ_INT(snd->type, XW_OBJ_SOUND);
      TEST_CHECK_EQ_STR(snd->sound.file, "sounds/the-rain-sound_102bpm.wav");
      TEST_CHECK_EQ_INT(snd->sound.loop, 1);
      TEST_CHECK(snd->sound.volume > 0.49 && snd->sound.volume < 0.51);
    }
  }
  /* project.json props incl. user defaults */
  TEST_CHECK(s.n_props >= 3);
  /* night blue effect: constants + textures parsed, shader resolved
   * through effect.json -> material */
  {
    const XWSceneEffect *fx = NULL;
    if (s.objects[0].n_effects >= 1)
      fx = &s.objects[0].effects[0];
    TEST_CHECK(fx != NULL && fx->n_passes >= 1);
    if (fx && fx->n_passes >= 1) {
      const XWScenePass *p = &fx->passes[0];
      TEST_CHECK_EQ_STR(p->shader, "effects/waterripple");
      TEST_CHECK(p->n_consts >= 3);
      TEST_CHECK_EQ_INT(p->n_textures, 2);
      TEST_CHECK_EQ_STR(p->textures[0],
                        "masks/waterripple_mask_14baecd88cd86197979e9592a3614e57bbd01235");
    }
  }
  /* rain particle config + instanceoverride multipliers */
  {
    const XWSceneObject *rain = NULL;
    for (int i = 0; i < s.n_objects; i++) {
      if (strcmp(s.objects[i].id, "Rain Downfall light") == 0)
        rain = &s.objects[i];
    }
    TEST_CHECK(rain != NULL);
    if (rain) {
      TEST_CHECK_EQ_INT((int)rain->particle.rate, 5000);
      TEST_CHECK_EQ_INT(rain->particle.max_count, 1000);
      TEST_CHECK(rain->particle.life > 0.19 && rain->particle.life < 0.21);
      TEST_CHECK(rain->particle.speed > 9999.0 &&
                 rain->particle.speed < 10001.0);
      TEST_CHECK(rain->particle.gravity > 49.0 &&
                 rain->particle.gravity < 51.0);
      TEST_CHECK_EQ_INT(rain->alpha.kind, XW_DYN_USER);
      TEST_CHECK_EQ_STR(rain->alpha.ref, "opacity");
      TEST_CHECK_EQ_INT(rain->particle.count_mult.kind, XW_DYN_USER);
      TEST_CHECK_EQ_STR(rain->particle.count_mult.ref, "rain_intensity");
      TEST_CHECK_EQ_STR(rain->particle.speed_mult.ref, "rain_speed");
      TEST_CHECK(rain->particle.sprite[0] != '\0');
    }
  }
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
  test_gl_texture();
  test_realrain();
  TEST_SUMMARY();
}
