#include <string.h>

#include "harness.h"
#include "../src/scene_particle.h"

/* Mirrors the engine's box emitter: centered pixel space with Y up, the
 * emitter origin Y-flipped, spawn box = distanceMin/Max extent, velocity
 * from velocityrandom (Y flipped) and gravity from the movement operator
 * (Y flipped too). */
static XWParticleCfg cfg60(void) {
  XWParticleCfg c;
  memset(&c, 0, sizeof(c));
  c.rate = 60.0f;
  c.max_count = 500;
  c.life = 2.0f;
  c.size_min = 2.0f;
  c.size_max = 4.0f;
  c.color[0] = 1.0f;
  c.color[1] = 1.0f;
  c.color[2] = 1.0f;
  c.color[3] = 1.0f;
  c.speed_mult.kind = XW_DYN_CONSTANT;
  c.speed_mult.constant = 1.0f;
  c.origin[0] = 100.0f;
  c.origin[1] = 200.0f; /* flipped to -200 at spawn */
  c.dist_max[0] = 50.0f;
  c.dist_max[1] = 30.0f;
  c.dirs[0] = c.dirs[1] = c.dirs[2] = 1.0f;
  c.vel_min[1] = -1000.0f; /* flipped to +1000 */
  c.vel_max[1] = -2000.0f; /* flipped to +2000 */
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

static void test_box_spawn(void) {
  TEST_BEGIN("spawns inside the distance box around the flipped origin");
  XWEmitter e;
  XWParticleCfg c = cfg60();
  c.life = 100.0f;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  /* one short step: x has no velocity so it stays exact, y drifts by
   * vy*dt (<= ~34px) so allow that tolerance on the box */
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 1.0f / 60.0f), 1);
  TEST_CHECK(e.alive > 0);
  for (int i = 0; i < e.alive; i++) {
    TEST_CHECK(e.parts[i].x >= 100.0f - 50.0f && e.parts[i].x <= 100.0f + 50.0f);
    /* origin.y 200 flips to -200, box +/-30, plus one integration step */
    TEST_CHECK(e.parts[i].y >= -270.0f && e.parts[i].y <= -130.0f);
    /* velocityrandom Y is flipped: -1000..-2000 becomes +1000..+2000 */
    TEST_CHECK(e.parts[i].vy >= 1000.0f && e.parts[i].vy <= 2000.0f);
    TEST_CHECK(e.parts[i].size >= 2.0f && e.parts[i].size <= 4.0f);
  }
  xw_emitter_free(&e);
}

static void test_cap(void) {
  TEST_BEGIN("caps at max_count");
  XWEmitter e;
  XWParticleCfg c = cfg60();
  c.max_count = 10;
  c.life = 100.0f;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 10.0f), 10);
  xw_emitter_free(&e);
}

static void test_gravity_and_determinism(void) {
  TEST_BEGIN("gravity bends paths (Y flipped), same seed replays identically");
  XWParticleCfg c = cfg60();
  c.gravity_v[1] = 100.0f; /* flipped: applied as -100 */
  XWEmitter a, b, z;
  XWParticleCfg cz = cfg60();
  TEST_CHECK_EQ_INT(xw_emitter_init(&a, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_init(&b, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_init(&z, &cz), 0);
  xw_emitter_update(&a, 0.5f);
  xw_emitter_update(&b, 0.5f);
  xw_emitter_update(&z, 0.5f);
  TEST_CHECK(a.alive == b.alive && a.alive > 0);
  TEST_CHECK(a.parts[0].x == b.parts[0].x && a.parts[0].y == b.parts[0].y);
  /* gravity pulls velocity back down against the +Y spawn velocity */
  TEST_CHECK(a.parts[0].vy < z.parts[0].vy);
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
  test_box_spawn();
  test_cap();
  test_gravity_and_determinism();
  test_bad_inputs();
  TEST_SUMMARY();
}
