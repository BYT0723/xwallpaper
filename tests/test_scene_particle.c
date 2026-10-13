#include <math.h>
#include <string.h>

#include "harness.h"
#include "../src/scene_particle.h"

/* angle in degrees between two 2D directions */
static float dir_angle(float ax, float ay, float bx, float by) {
  float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
  float c;
  if (la < 1e-6f || lb < 1e-6f)
    return 90.0f;
  c = (ax * bx + ay * by) / (la * lb);
  if (c > 1.0f)
    c = 1.0f;
  if (c < -1.0f)
    c = -1.0f;
  return acosf(c) * 180.0f / 3.14159265f;
}

/* Mirrors the engine's box emitter: centered pixel space with Y up, the
 * emitter origin Y-flipped, spawn box = distanceMin/Max extent, velocity
 * from velocityrandom (Y flipped) and gravity from the movement operator
 * (Y flipped too). */
/* The instance overrides are real inputs now (a wallpaper's sliders); a
 * zero-initialised struct is 'constant 0', so set the neutral value. */
static void mult_defaults(XWParticleCfg *c) {
  c->count_mult.kind = c->life_mult.kind = c->speed_mult.kind = XW_DYN_CONSTANT;
  c->size_mult.kind = XW_DYN_CONSTANT;
  c->count_mult.constant = c->life_mult.constant = c->speed_mult.constant = 1.0f;
  c->size_mult.constant = 1.0f;
}

static XWParticleCfg cfg60(void) {
  XWParticleCfg c;
  memset(&c, 0, sizeof(c));
  mult_defaults(&c);
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
    /* sizerandom halves the draw like the engine does: 2..4 -> 1..2 */
    TEST_CHECK(e.parts[i].size >= 1.0f && e.parts[i].size <= 2.0f);
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

static void test_initializers(void) {
  TEST_BEGIN("alpha/colour/rotation random plus a stable per-particle draw");
  XWEmitter e;
  XWParticleCfg c = cfg60();
  c.alpha_min = 0.25f;
  c.alpha_max = 0.75f;
  c.color_min[0] = 0.2f;
  c.color_max[0] = 0.8f;
  c.color_min[1] = 0.3f;
  c.color_max[1] = 0.9f;
  c.color_min[2] = 0.1f;
  c.color_max[2] = 0.5f;
  c.has_color_random = 1;
  c.rot_min = -0.5f;
  c.rot_max = 0.5f;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_update(&e, 1.0f), 60);
  TEST_CHECK(e.alive > 1);
  int distinct_rnd = 0;
  for (int i = 0; i < e.alive; i++) {
    const XWParticle *p = &e.parts[i];
    TEST_CHECK(p->a >= 0.25f && p->a <= 0.75f);
    TEST_CHECK(p->r >= 0.2f && p->r <= 0.8f);
    TEST_CHECK(p->g >= 0.3f && p->g <= 0.9f);
    TEST_CHECK(p->b >= 0.1f && p->b <= 0.5f);
    TEST_CHECK(p->rot >= -0.5f && p->rot <= 0.5f);
    TEST_CHECK(p->rnd >= 0.0f && p->rnd < 1.0f);
    if (i > 0 && p->rnd != e.parts[0].rnd)
      distinct_rnd = 1;
  }
  TEST_CHECK(distinct_rnd);
  xw_emitter_free(&e);
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

/* one particle per ~2px of box, all spawned in a single step */
static XWParticleCfg cfg_turb(void) {
  XWParticleCfg c;
  memset(&c, 0, sizeof(c));
  mult_defaults(&c);
  c.rate = 200.0f;
  c.max_count = 400;
  c.life = 100.0f;
  c.size_min = c.size_max = 1.0f;
  c.speed_mult.kind = XW_DYN_CONSTANT;
  c.speed_mult.constant = 1.0f;
  c.dist_max[0] = 400.0f;
  c.dist_max[1] = 400.0f;
  c.dirs[0] = c.dirs[1] = c.dirs[2] = 1.0f;
  c.has_turb = 1;
  c.turb_speed_min = 2.0f;
  c.turb_speed_max = 2.0f;
  c.turb_scale = 2.0f; /* >= 2 turns the forward clamp off */
  c.turb_forward[1] = 1.0f;
  c.turb_right[0] = 1.0f;
  return c;
}

static void test_turbulent_speed(void) {
  XWEmitter e;
  XWParticleCfg c = cfg_turb();
  int i, distinct = 0;
  TEST_BEGIN("turbulentvelocityrandom sets the sampled speed everywhere");
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK(xw_emitter_update(&e, 1.0f) >= 0);
  TEST_CHECK(e.alive > 20);
  for (i = 0; i < e.alive; i++) {
    float len = hypotf(e.parts[i].vx, e.parts[i].vy);
    TEST_CHECK(fabsf(len - 2.0f) < 0.05f);
    if (i && dir_angle(e.parts[i].vx, e.parts[i].vy, e.parts[0].vx,
                       e.parts[0].vy) > 1.0f)
      distinct++;
  }
  /* the field varies in space, so directions are not all identical */
  TEST_CHECK(distinct > 10);
  xw_emitter_free(&e);
}

static void test_turbulent_coherence(void) {
  XWEmitter e;
  XWParticleCfg narrow = cfg_turb(), wide = cfg_turb();
  float far = 0.0f, near = 0.0f;
  int i;
  TEST_BEGIN("turbulentvelocityrandom is spatially coherent");
  narrow.dist_max[0] = narrow.dist_max[1] = 0.05f;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &narrow), 0);
  TEST_CHECK(xw_emitter_update(&e, 1.0f) >= 0);
  TEST_CHECK(e.alive > 20);
  for (i = 1; i < e.alive; i++) {
    float a = dir_angle(e.parts[i].vx, e.parts[i].vy, e.parts[0].vx,
                        e.parts[0].vy);
    if (a > near)
      near = a;
  }
  xw_emitter_free(&e);
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &wide), 0);
  TEST_CHECK(xw_emitter_update(&e, 1.0f) >= 0);
  for (i = 1; i < e.alive; i++) {
    float a = dir_angle(e.parts[i].vx, e.parts[i].vy, e.parts[0].vx,
                        e.parts[0].vy);
    if (a > far)
      far = a;
  }
  xw_emitter_free(&e);
  TEST_CHECK(near < 8.0f);  /* 1px apart: nearly the same sample */
  TEST_CHECK(far > 30.0f);  /* 400px apart: clearly different */
}

static void test_turbulent_forward_clamp(void) {
  XWEmitter e;
  XWParticleCfg c = cfg_turb();
  int i, worst = 0;
  TEST_BEGIN("turbulentvelocityrandom scale < 2 clamps to `forward`");
  c.dist_max[0] = c.dist_max[1] = 4000.0f;
  c.turb_scale = 0.5f; /* half of pi/2 -> 45 degrees max deviation */
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK(xw_emitter_update(&e, 1.0f) >= 0);
  TEST_CHECK(e.alive > 20);
  for (i = 0; i < e.alive; i++) {
    /* forward (0,1,0) is Y-flipped into screen space -> (0,-1) */
    float a = dir_angle(e.parts[i].vx, e.parts[i].vy, 0.0f, -1.0f);
    if (a > worst + 0.5f)
      worst = (int)a;
    TEST_CHECK(a <= 45.5f);
  }
  TEST_CHECK(worst >= 30); /* the clamp really had to act */
  xw_emitter_free(&e);
}

static void test_turbulent_offset_and_determinism(void) {
  XWEmitter e1, e2, e3;
  XWParticleCfg c = cfg_turb(), c2 = cfg_turb(), c3 = cfg_turb();
  int i, same = 1, rotated = 0;
  TEST_BEGIN("turbulentvelocityrandom honours offset and a fixed seed");
  c3.turb_offset = 1.5707963f; /* pi/2 around `right` */
  TEST_CHECK_EQ_INT(xw_emitter_init(&e1, &c), 0);
  TEST_CHECK_EQ_INT(xw_emitter_init(&e2, &c2), 0);
  TEST_CHECK_EQ_INT(xw_emitter_init(&e3, &c3), 0);
  TEST_CHECK(xw_emitter_update(&e1, 1.0f) >= 0);
  TEST_CHECK(xw_emitter_update(&e2, 1.0f) >= 0);
  TEST_CHECK(xw_emitter_update(&e3, 1.0f) >= 0);
  for (i = 0; i < e1.alive; i++) {
    if (e1.parts[i].vx != e2.parts[i].vx || e1.parts[i].vy != e2.parts[i].vy)
      same = 0;
    /* the rotation preserves length */
    TEST_CHECK(fabsf(hypotf(e3.parts[i].vx, e3.parts[i].vy) - 2.0f) < 0.05f);
    if (dir_angle(e1.parts[i].vx, e1.parts[i].vy, e3.parts[i].vx,
                  e3.parts[i].vy) > 5.0f)
      rotated++;
  }
  TEST_CHECK(same); /* deterministic per seed */
  TEST_CHECK(rotated > 10);
  xw_emitter_free(&e1);
  xw_emitter_free(&e2);
  xw_emitter_free(&e3);
}

static void test_no_turbulent_velocity(void) {
  XWEmitter e;
  XWParticleCfg c = cfg_turb();
  int i;
  TEST_BEGIN("no turbulentvelocityrandom keeps the plain velocity");
  c.has_turb = 0;
  TEST_CHECK_EQ_INT(xw_emitter_init(&e, &c), 0);
  TEST_CHECK(xw_emitter_update(&e, 1.0f) >= 0);
  TEST_CHECK(e.alive > 20);
  for (i = 0; i < e.alive; i++)
    TEST_CHECK(e.parts[i].vx == 0.0f && e.parts[i].vy == 0.0f);
  xw_emitter_free(&e);
}

int main(void) {
  TEST_SUITE("scene_particle");
  test_spawn_and_die();
  test_box_spawn();
  test_cap();
  test_gravity_and_determinism();
  test_initializers();
  test_turbulent_speed();
  test_turbulent_coherence();
  test_turbulent_forward_clamp();
  test_turbulent_offset_and_determinism();
  test_no_turbulent_velocity();
  test_bad_inputs();
  TEST_SUMMARY();
}
