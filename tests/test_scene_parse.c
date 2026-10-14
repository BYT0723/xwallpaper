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
      /* the engine renders flags & 4 particles with its own perspective */
      TEST_CHECK_EQ_INT(rain->particle.flags & 4, 4);
      TEST_CHECK(s.camera.fov > 49.0f && s.camera.fov < 51.0f);
      TEST_CHECK(s.camera.nearz > 0.0f && s.camera.nearz < 0.02f);
      TEST_CHECK(s.camera.farz > 9999.0f);
      /* spritetrail renderer params feed the TRAILRENDERER shader path */
      TEST_CHECK_EQ_INT(rain->particle.has_trail, 1);
      TEST_CHECK(rain->particle.trail_length > 4.99f &&
                 rain->particle.trail_length < 5.01f);
      TEST_CHECK(rain->particle.trail_max > 29.99f &&
                 rain->particle.trail_max < 30.01f);
      TEST_CHECK(rain->particle.trail_min > -0.01f &&
                 rain->particle.trail_min < 0.01f);
      /* {user} sliders resolve against the wallpaper's project.json values
       * rather than the inline fallbacks: rain_speed is 0.5, rain_intensity
       * 1 (the particle's inline fallback would be 1 and 0.39) */
      TEST_CHECK_EQ_INT(rain->particle.speed_mult.kind, XW_DYN_CONSTANT);
      TEST_CHECK(rain->particle.speed_mult.constant > 0.49f &&
                 rain->particle.speed_mult.constant < 0.51f);
      TEST_CHECK_EQ_INT(rain->particle.life_mult.kind, XW_DYN_CONSTANT);
      TEST_CHECK(rain->particle.life_mult.constant > 0.99f &&
                 rain->particle.life_mult.constant < 1.01f);
      TEST_CHECK_EQ_INT(rain->particle.count_mult.kind, XW_DYN_CONSTANT);
      TEST_CHECK(rain->particle.count_mult.constant > 0.99f &&
                 rain->particle.count_mult.constant < 1.01f);
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
      /* slot-aligned: [null, mask, normal] keeps unit indices */
      TEST_CHECK_EQ_INT(p->n_textures, 3);
      TEST_CHECK_EQ_STR(p->textures[0], "");
      TEST_CHECK_EQ_STR(p->textures[1],
                        "masks/waterripple_mask_14baecd88cd86197979e9592a3614e57bbd01235");
      TEST_CHECK_EQ_STR(p->textures[2], "effects/waterripplenormal");
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
      /* resolved against project.json (rain_intensity 1, rain_speed 0.5) */
      TEST_CHECK_EQ_INT(rain->particle.count_mult.kind, XW_DYN_CONSTANT);
      TEST_CHECK(rain->particle.count_mult.constant > 0.99f &&
                 rain->particle.count_mult.constant < 1.01f);
      TEST_CHECK(rain->particle.speed_mult.constant > 0.49f &&
                 rain->particle.speed_mult.constant < 0.51f);
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

static void test_effect_target(void) {
  TEST_BEGIN("offscreen render target, fbos and binds are captured");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene/effecttarget", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0)
    return;
  TEST_CHECK_EQ_INT(s.n_objects, 1);
  TEST_CHECK(s.objects[0].n_effects >= 1);
  if (s.objects[0].n_effects >= 1) {
    const XWSceneEffect *fx = &s.objects[0].effects[0];
    TEST_CHECK_EQ_INT(fx->n_passes, 2);
    /* effect-declared offscreen buffer */
    TEST_CHECK_EQ_INT(fx->n_fbos, 1);
    if (fx->n_fbos == 1) {
      TEST_CHECK_EQ_STR(fx->fbos[0].name, "_rt_Offscreen");
      TEST_CHECK_EQ_INT(fx->fbos[0].width, 256);
      TEST_CHECK_EQ_INT(fx->fbos[0].height, 256);
      TEST_CHECK_EQ_STR(fx->fbos[0].format, "r8");
      TEST_CHECK_EQ_STR(fx->fbos[0].uvs, "repeat");
    }
    if (fx->n_passes == 2) {
      TEST_CHECK_EQ_STR(fx->passes[0].target, "_rt_Offscreen");
      TEST_CHECK_EQ_STR(fx->passes[0].shader, "genericimage2");
      TEST_CHECK_EQ_INT(fx->passes[1].n_binds, 2);
      if (fx->passes[1].n_binds == 2) {
        TEST_CHECK_EQ_STR(fx->passes[1].binds[0].name, "previous");
        TEST_CHECK_EQ_INT(fx->passes[1].binds[0].slot, 0);
        TEST_CHECK_EQ_STR(fx->passes[1].binds[1].name, "_rt_Offscreen");
        TEST_CHECK_EQ_INT(fx->passes[1].binds[1].slot, 1);
      }
    }
  }
  xw_scene_free(&s);
}

static void test_visible_dict(void) {
  TEST_BEGIN("visible {user, value} dicts resolve via value");
  XWScene s;
  memset(&s, 0, sizeof(s));
  XWError err = {0};
  int rc = xw_scene_parse("tests/data/scene/visibledict", &s, &err);
  TEST_CHECK_EQ_INT(rc, 0);
  if (rc != 0)
    return;
  TEST_CHECK_EQ_INT(s.n_objects, 3);
  TEST_CHECK_EQ_INT(s.objects[0].visible, 1);
  TEST_CHECK_EQ_INT(s.objects[1].visible, 0);
  TEST_CHECK_EQ_INT(s.objects[2].visible, 0);
  xw_scene_free(&s);
}

static void test_scripted_vec_props(void) {
  XWScene s;
  XWError err = {0};
  TEST_BEGIN("scripted origin/scale dicts fall back to their base value");
  memset(&s, 0, sizeof(s));
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data/scene/scriptprop", &s, &err), 0);
  TEST_CHECK_EQ_INT(s.n_objects, 1);
  if (s.n_objects == 1) {
    TEST_CHECK((int)s.objects[0].origin[0] == 2700);
    TEST_CHECK((int)s.objects[0].origin[1] == 238);
    TEST_CHECK(s.objects[0].scale[0] > 0.49f && s.objects[0].scale[0] < 0.51f);
    TEST_CHECK((int)s.objects[0].size[0] == 100);
    TEST_CHECK((int)s.objects[0].size[1] == 200);
  }
  xw_scene_free(&s);
}

static void test_turbulent_parse(void) {
  XWScene s;
  XWError err = {0};
  const XWParticleCfg *p;
  TEST_BEGIN("turbulentvelocityrandom parses its field parameters");
  memset(&s, 0, sizeof(s));
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data/scene/turbpart", &s, &err), 0);
  TEST_CHECK_EQ_INT(s.n_objects, 1);
  if (s.n_objects != 1) {
    xw_scene_free(&s);
    return;
  }
  p = &s.objects[0].particle;
  TEST_CHECK_EQ_INT(s.objects[0].type, XW_OBJ_PARTICLE);
  TEST_CHECK_EQ_INT(p->has_turb, 1);
  TEST_CHECK(p->turb_speed_min > 0.49f && p->turb_speed_min < 0.51f);
  TEST_CHECK(p->turb_speed_max > 0.99f && p->turb_speed_max < 1.01f);
  TEST_CHECK(p->turb_offset > 19.9f && p->turb_offset < 20.1f);
  TEST_CHECK(p->turb_scale > 0.99f && p->turb_scale < 1.01f);
  TEST_CHECK(p->turb_time_scale > 9.9f && p->turb_time_scale < 10.1f);
  TEST_CHECK(p->turb_phase_min > 0.09f && p->turb_phase_min < 0.11f);
  TEST_CHECK(p->turb_phase_max > 0.89f && p->turb_phase_max < 0.91f);
  TEST_CHECK_EQ_INT((int)p->turb_forward[0], 20);
  TEST_CHECK_EQ_INT((int)p->turb_forward[1], 20);
  TEST_CHECK_EQ_INT((int)p->turb_right[0], 10);
  TEST_CHECK_EQ_INT((int)p->turb_right[2], 20);
  xw_scene_free(&s);
}

static void test_emitter_geometry(void) {
  XWScene s;
  XWError err = {0};
  const XWParticleCfg *box, *sp;
  TEST_BEGIN("emitter geometry: directions default, scalar distance, sphere");
  memset(&s, 0, sizeof(s));
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data/scene/sphereemitter", &s, &err), 0);
  TEST_CHECK_EQ_INT(s.n_objects, 2);
  if (s.n_objects != 2) {
    xw_scene_free(&s);
    return;
  }
  /* boxrandom carries a vector distancemax and no `directions`: the engine
   * defaults directions to (1,1,1), so the spawn box must survive intact */
  box = &s.objects[0].particle;
  TEST_CHECK_EQ_INT(box->emitter_sphere, 0);
  TEST_CHECK_EQ_INT((int)box->dist_max[0], 1200);
  TEST_CHECK_EQ_INT((int)box->dist_max[1], 700);
  TEST_CHECK(box->dirs[0] > 0.99f && box->dirs[0] < 1.01f);
  TEST_CHECK(box->dirs[1] > 0.99f && box->dirs[1] < 1.01f);
  TEST_CHECK(box->dirs[2] > 0.99f && box->dirs[2] < 1.01f);
  /* sphererandom spells its radii as plain numbers, not a vector string */
  sp = &s.objects[1].particle;
  TEST_CHECK_EQ_INT(sp->emitter_sphere, 1);
  TEST_CHECK_EQ_INT((int)sp->dist_min[0], 10);
  TEST_CHECK_EQ_INT((int)sp->dist_max[0], 1000);
  TEST_CHECK(sp->dirs[0] > 0.99f && sp->dirs[0] < 1.01f);
  TEST_CHECK(sp->dirs[1] > 0.02f && sp->dirs[1] < 0.04f);
  TEST_CHECK_EQ_INT((int)sp->sign[0], 0);
  TEST_CHECK_EQ_INT((int)sp->sign[2], 1);
  /* oscillateposition spelled with nothing but its name takes the engine's
   * defaults: frequency 0..5, scale 0..10, phase 0..2pi, mask (1,1,0) */
  TEST_CHECK_EQ_INT(box->osc_pos.present, 1);
  TEST_CHECK(box->osc_pos.freq_min == 0.0f);
  TEST_CHECK(box->osc_pos.freq_max > 4.99f && box->osc_pos.freq_max < 5.01f);
  TEST_CHECK(box->osc_pos.scale_min == 0.0f);
  TEST_CHECK(box->osc_pos.scale_max > 9.99f && box->osc_pos.scale_max < 10.01f);
  TEST_CHECK(box->osc_pos.phase_max > 6.28f && box->osc_pos.phase_max < 6.29f);
  TEST_CHECK(box->osc_pos.mask[0] > 0.99f && box->osc_pos.mask[0] < 1.01f);
  TEST_CHECK(box->osc_pos.mask[1] > 0.99f && box->osc_pos.mask[1] < 1.01f);
  TEST_CHECK(box->osc_pos.mask[2] == 0.0f);
  /* oscillatealpha defaults its scale to 0..1 and its frequency to 0..10 */
  TEST_CHECK_EQ_INT(box->osc_alpha.present, 1);
  TEST_CHECK(box->osc_alpha.freq_max > 6.99f && box->osc_alpha.freq_max < 7.01f);
  TEST_CHECK(box->osc_alpha.scale_min > 0.49f && box->osc_alpha.scale_min < 0.51f);
  TEST_CHECK(box->osc_alpha.scale_max > 0.99f && box->osc_alpha.scale_max < 1.01f);
  TEST_CHECK_EQ_INT(box->osc_size.present, 0);
  xw_scene_free(&s);
}

static void test_particle_defaults(void) {
  XWScene s;
  XWError err = {0};
  const XWParticleCfg *p;
  TEST_BEGIN("particle config defaults match the engine (rate 10, maxcount 100)");
  memset(&s, 0, sizeof(s));
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data/scene/particledefaults", &s, &err), 0);
  TEST_CHECK_EQ_INT(s.n_objects, 1);
  if (s.n_objects != 1) {
    xw_scene_free(&s);
    return;
  }
  p = &s.objects[0].particle;
  TEST_CHECK_EQ_INT(s.objects[0].type, XW_OBJ_PARTICLE);
  /* ObjectParser::parseParticleEmitter defaults an omitted rate to 10 and
   * parseParticle defaults maxcount to 100; without them the emitter has an
   * empty pool and the whole system is invisible */
  TEST_CHECK(p->rate > 9.99f && p->rate < 10.01f);
  TEST_CHECK_EQ_INT(p->max_count, 100);
  /* an omitted animationmode is "sequence" */
  TEST_CHECK_EQ_STR(p->animation_mode, "sequence");
  xw_scene_free(&s);
}

static void test_particle_angular_parse(void) {
  XWScene s;
  XWError err = {0};
  const XWParticleCfg *box, *sp;
  TEST_BEGIN("angular movement and angular velocity parse with engine defaults");
  memset(&s, 0, sizeof(s));
  TEST_CHECK_EQ_INT(xw_scene_parse("tests/data/scene/sphereemitter", &s, &err), 0);
  TEST_CHECK_EQ_INT(s.n_objects, 2);
  if (s.n_objects != 2) {
    xw_scene_free(&s);
    return;
  }
  /* the box config spells both out: a constant -0.05 rad/s spin, drag 0.5
   * and a z force of 2 */
  box = &s.objects[0].particle;
  TEST_CHECK_EQ_INT(box->has_av_random, 1);
  TEST_CHECK(box->av_min > -0.06f && box->av_min < -0.04f);
  TEST_CHECK(box->av_max > -0.06f && box->av_max < -0.04f);
  TEST_CHECK(box->av_exponent > 0.99f && box->av_exponent < 1.01f);
  TEST_CHECK_EQ_INT(box->has_angular_movement, 1);
  TEST_CHECK(box->am_drag > 0.49f && box->am_drag < 0.51f);
  TEST_CHECK(box->am_force_z > 1.99f && box->am_force_z < 2.01f);
  /* the sphere config omits both: the engine defaults to a +-5 rad/s draw
   * with no drag and no force */
  sp = &s.objects[1].particle;
  TEST_CHECK_EQ_INT(sp->has_av_random, 1);
  TEST_CHECK(sp->av_min > -5.01f && sp->av_min < -4.99f);
  TEST_CHECK(sp->av_max > 4.99f && sp->av_max < 5.01f);
  TEST_CHECK(sp->av_exponent > 0.99f && sp->av_exponent < 1.01f);
  TEST_CHECK_EQ_INT(sp->has_angular_movement, 1);
  TEST_CHECK(sp->am_drag == 0.0f);
  TEST_CHECK(sp->am_force_z == 0.0f);
  xw_scene_free(&s);
}

int main(void) {
  TEST_SUITE("scene_parse");
  test_parse_fixture();
  test_bad_inputs();
  test_full_types();
  test_full_props_script();
  test_gl_texture();
  test_realrain();
  test_effect_target();
  test_visible_dict();
  test_scripted_vec_props();
  test_turbulent_parse();
  test_emitter_geometry();
  test_particle_defaults();
  test_particle_angular_parse();
  TEST_SUMMARY();
}
