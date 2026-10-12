#include <string.h>

#include "harness.h"
#include "../src/scene_particle.h"

static XWParticleCfg cfg60(void) {
  XWParticleCfg c;
  memset(&c, 0, sizeof(c));
  c.rate = 60.0f;
  c.max_count = 500;
  c.life = 2.0f;
  c.speed = 120.0f;
  c.spread = 0.0f;
  c.size_min = 2.0f;
  c.size_max = 4.0f;
  c.color[0] = 1.0f;
  c.color[1] = 1.0f;
  c.color[2] = 1.0f;
  c.color[3] = 1.0f;
  c.gravity = 0.0f;
  return c;
}

static void test_spawn_and_die(void) {
  TEST_BEGIN("spawns at rate, dies after life");
  XWEmitter e;
  XWParticleCfg c = cfg60();
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 1.0f / 60.0f), 1);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 1.0f), 61);
  /* after 2 more seconds the first 61 are dead, rate keeps 120 alive */
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 2.0f), 120);
  xw_emitter_free(&e);
}

static void test_cap_and_motion(void) {
  TEST_BEGIN("caps at max_count, moves upward without gravity");
  XWEmitter e;
  XWParticleCfg c = cfg60();
  c.max_count = 10;
  c.life = 100.0f;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 10.0f), 10);
  TEST_CHECK(e.parts[0].y < 0.0f);
  TEST_CHECK(e.parts[0].vy < 0.0f);
  xw_emitter_free(&e);
}

static void test_gravity_and_determinism(void) {
  TEST_BEGIN("gravity bends paths, same seed replays identically");
  XWParticleCfg c = cfg60();
  c.gravity = 50.0f;
  XWEmitter a, b;
  TEST_CHECK_EQ_INT(xw_emitter_init(&a, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_init(&b, &c), 0);
  xw_emitter_update(&a, 0.5f);
  xw_emitter_update(&b, 0.5f);
  TEST_CHECK(a.alive == b.alive && a.alive > 0);
  TEST_CHECK(a.parts[0].x == b.parts[0].x && a.parts[0].y == b.parts[0].y);
  /* gravity: vy grows (falls back down) vs zero-gravity twin */
  XWEmitter z;
  XWParticleCfg cz = cfg60();
  TEST_CHECK_EQ_INT(xw_emitter_init(&z, &cz), 0);
  xw_emitter_update(&z, 0.5f);
  TEST_CHECK(a.parts[0].vy > z.parts[0].vy);
  xw_emitter_free(&a);
  xw_emitter_free(&b);
  xw_emitter_free(&z);
}

static void test_bad_inputs(void) {
  TEST_BEGIN("zero rate spawns nothing, NULL rejected");
  XWEmitter e;
  XWParticleCfg c = cfg60();
  c.rate = 0.0f;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 1.0f), 0);
  xw_emitter_free(&e);
  TEST_CHECK_EQ_INT(xw_emitter_init(NULL, &c), -1);
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, NULL), -1);
  TEST_CHECK_EQ_INT(xw_emitter_update(NULL, 0.1f), -1);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, -1.0f), -1);
  xw_emitter_free(NULL);
}

int main(void) {
  TEST_SUITE("scene_particle");
  test_spawn_and_die();
  test_cap_and_motion();
  test_gravity_and_determinism();
  test_bad_inputs();
  TEST_SUMMARY();
}
