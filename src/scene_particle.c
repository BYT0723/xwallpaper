#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_particle.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define XW_PARTICLE_RNG_SEED 0x9E3779B9u

static float next_unit(unsigned int *rng) {
  *rng = *rng * 1664525u + 1013904223u;
  return (float)(*rng >> 8) * (1.0f / 16777216.0f);
}

/* {user, value} instance overrides: 1.0 when unresolved. */
static float dyn_or_one(const XWDynFloat *d) {
  return d && d->kind == XW_DYN_CONSTANT ? d->constant : 1.0f;
}

/* ------------------------------------------------------------------ */
/* Perlin / curl noise for the turbulentvelocityrandom initializer      */
/* ------------------------------------------------------------------ */
/* The engine drives that initializer from an internal curl-noise field;
 * the field itself is not documented, so this is a standard Perlin curl
 * noise. The permutation is generated from a fixed seed at first use
 * rather than copied from any reference table. */

static unsigned char g_perm[512];
static int g_perm_ready;

static void perm_init(void) {
  unsigned char t[256];
  unsigned int s = 0x2545F491u;
  int i;
  if (g_perm_ready)
    return;
  for (i = 0; i < 256; i++)
    t[i] = (unsigned char)i;
  for (i = 255; i > 0; i--) {
    unsigned char tmp;
    int j;
    s = s * 1103515245u + 12345u;
    j = (int)((s >> 16) % (unsigned int)(i + 1));
    tmp = t[i];
    t[i] = t[j];
    t[j] = tmp;
  }
  for (i = 0; i < 512; i++)
    g_perm[i] = t[i & 255];
  g_perm_ready = 1;
}

/* Ken Perlin's improved-noise gradient (public domain, "Improving Noise") */
static double pgrad(int hash, double x, double y, double z) {
  int h = hash & 15;
  double u = h < 8 ? x : y;
  double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static double pease(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
static double plerp(double t, double a, double b) { return a + t * (b - a); }

static double pnoise(double x, double y, double z) {
  int X = (int)floor(x) & 255;
  int Y = (int)floor(y) & 255;
  int Z = (int)floor(z) & 255;
  int A, AA, AB, B, BA, BB;
  double u, v, w;
  x -= floor(x);
  y -= floor(y);
  z -= floor(z);
  u = pease(x);
  v = pease(y);
  w = pease(z);
  A = g_perm[X] + Y;
  AA = g_perm[A] + Z;
  AB = g_perm[A + 1] + Z;
  B = g_perm[X + 1] + Y;
  BA = g_perm[B] + Z;
  BB = g_perm[B + 1] + Z;
  return plerp(
      w,
      plerp(v,
            plerp(u, pgrad(g_perm[AA], x, y, z),
                  pgrad(g_perm[BA], x - 1, y, z)),
            plerp(u, pgrad(g_perm[AB], x, y - 1, z),
                  pgrad(g_perm[BB], x - 1, y - 1, z))),
      plerp(v,
            plerp(u, pgrad(g_perm[AA + 1], x, y, z - 1),
                  pgrad(g_perm[BA + 1], x - 1, y, z - 1)),
            plerp(u, pgrad(g_perm[AB + 1], x, y - 1, z - 1),
                  pgrad(g_perm[BB + 1], x - 1, y - 1, z - 1))));
}

/* three decorrelated samples so the curl has a vector field to rotate */
static void pnoise3(double x, double y, double z, double o[3]) {
  o[0] = pnoise(x, y, z);
  o[1] = pnoise(x + 89.2, y + 33.1, z + 57.3);
  o[2] = pnoise(x + 100.3, y + 120.1, z + 142.2);
}

static void curl_noise(double x, double y, double z, double o[3]) {
  const double e = 1e-4;
  double x0[3], x1[3], y0[3], y1[3], z0[3], z1[3];
  pnoise3(x - e, y, z, x0);
  pnoise3(x + e, y, z, x1);
  pnoise3(x, y - e, z, y0);
  pnoise3(x, y + e, z, y1);
  pnoise3(x, y, z - e, z0);
  pnoise3(x, y, z + e, z1);
  o[0] = ((y1[2] - y0[2]) - (z1[1] - z0[1])) / (2.0 * e);
  o[1] = ((z1[0] - z0[0]) - (x1[2] - x0[2])) / (2.0 * e);
  o[2] = ((x1[1] - x0[1]) - (y1[0] - y0[0])) / (2.0 * e);
}

static double v3dot(const double a[3], const double b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Rodrigues rotation of v around the unit `axis` by `ang` radians */
static void rot_axis(const double v[3], const double axis[3], double ang,
                     double out[3]) {
  double c = cos(ang), s = sin(ang), d = v3dot(v, axis);
  double cr[3];
  int i;
  cr[0] = axis[1] * v[2] - axis[2] * v[1];
  cr[1] = axis[2] * v[0] - axis[0] * v[2];
  cr[2] = axis[0] * v[1] - axis[1] * v[0];
  for (i = 0; i < 3; i++)
    out[i] = v[i] * c + cr[i] * s + axis[i] * d * (1.0 - c);
}

/* turbulentvelocityrandom: sample the curl field at the spawn position (XZ
 * in screen space, matching the engine's own basis flip), then shape the
 * direction with the scale clamp and the offset tilt. */
static void apply_turbulent(const XWParticleCfg *c, unsigned int *rng,
                            float time, float px, float py, float *vx,
                            float *vy) {
  double fwd[3], rgt[3], samp[3], res[3], tmp[3], len, speed, phase, nx, ny, nz;
  int i;
  fwd[0] = c->turb_forward[0];
  fwd[1] = -c->turb_forward[1]; /* engine space is Y-up, ours is Y-down */
  fwd[2] = c->turb_forward[2];
  rgt[0] = c->turb_right[0];
  rgt[1] = -c->turb_right[1];
  rgt[2] = c->turb_right[2];
  len = sqrt(v3dot(fwd, fwd));
  if (len > 1e-4)
    for (i = 0; i < 3; i++)
      fwd[i] /= len;
  else {
    fwd[0] = 0.0;
    fwd[1] = 1.0;
    fwd[2] = 0.0;
  }
  len = sqrt(v3dot(rgt, rgt));
  if (len > 1e-4)
    for (i = 0; i < 3; i++)
      rgt[i] /= len;
  else {
    rgt[0] = 1.0;
    rgt[1] = 0.0;
    rgt[2] = 0.0;
  }

  speed = c->turb_speed_min +
          next_unit(rng) * (c->turb_speed_max - c->turb_speed_min);
  phase = c->turb_phase_min +
          next_unit(rng) * (c->turb_phase_max - c->turb_phase_min);

  perm_init();
  nx = px * 0.1 + time * c->turb_time_scale;
  ny = py * 0.1 + time * c->turb_time_scale;
  nz = time * c->turb_time_scale;
  samp[0] = nx + phase;
  samp[1] = ny + phase * 0.7;
  samp[2] = nz + phase * 1.3;
  curl_noise(samp[0], samp[1], samp[2], res);
  len = sqrt(v3dot(res, res));
  if (len < 1e-4) {
    memcpy(res, fwd, sizeof(res));
  } else {
    for (i = 0; i < 3; i++)
      res[i] /= len;
  }

  /* scale < 2 caps how far the direction may lean away from `forward` */
  if (c->turb_scale < 2.0) {
    double cosang = v3dot(res, fwd);
    double angle, max_angle;
    if (cosang > 1.0)
      cosang = 1.0;
    if (cosang < -1.0)
      cosang = -1.0;
    angle = acos(cosang) / M_PI;
    max_angle = c->turb_scale / 2.0;
    if (angle > max_angle && max_angle > 1e-4) {
      double axis[3], alen;
      axis[0] = res[1] * fwd[2] - res[2] * fwd[1];
      axis[1] = res[2] * fwd[0] - res[0] * fwd[2];
      axis[2] = res[0] * fwd[1] - res[1] * fwd[0];
      alen = sqrt(v3dot(axis, axis));
      if (alen > 1e-4) {
        for (i = 0; i < 3; i++)
          axis[i] /= alen;
        rot_axis(res, axis, (angle - max_angle) * M_PI, tmp);
        memcpy(res, tmp, sizeof(res));
      }
    }
  }

  /* offset tilts the direction around `right` */
  if (fabs((double)c->turb_offset) > 1e-4) {
    rot_axis(res, rgt, -(double)c->turb_offset, tmp);
    memcpy(res, tmp, sizeof(res));
  }

  /* 2D particles drop the z drift (our particles live on the XY plane) */
  res[2] = 0.0;
  len = sqrt(res[0] * res[0] + res[1] * res[1]);
  if (len > 1e-4) {
    res[0] /= len;
    res[1] /= len;
  }
  *vx += (float)(res[0] * speed);
  *vy += (float)(res[1] * speed);
}

int xw_emitter_init(XWEmitter *e, const XWParticleCfg *cfg) {
  if (!e || !cfg)
    return -1;
  memset(e, 0, sizeof(*e));
  e->cfg = *cfg;
  if (e->cfg.max_count < 0)
    e->cfg.max_count = 0;
  if (e->cfg.life < 0.0f)
    e->cfg.life = 0.0f;
  if (e->cfg.rate < 0.0f)
    e->cfg.rate = 0.0f;
  /* the wallpaper's count slider scales the pool (the engine multiplies
   * maxCount by the instance override) */
  {
    float cm = dyn_or_one(&e->cfg.count_mult);
    if (cm > 0.0f && cm != 1.0f) {
      float scaled = (float)e->cfg.max_count * cm;
      e->cfg.max_count = scaled >= 1.0f ? (int)scaled : 1;
    }
  }
  e->parts = calloc(e->cfg.max_count > 0 ? (size_t)e->cfg.max_count : 1,
                    sizeof(XWParticle));
  if (!e->parts)
    return -1;
  e->rng = XW_PARTICLE_RNG_SEED;
  return 0;
}

void xw_emitter_free(XWEmitter *e) {
  if (!e)
    return;
  free(e->parts);
  e->parts = NULL;
  e->alive = 0;
  e->acc = 0.0f;
}

/* Spawn following the engine's emitters (CParticle::createBoxEmitter /
 * createSphereEmitter): positions live in centered pixel space with Y up,
 * the emitter origin is Y-flipped, `directions` scales the offset per axis
 * and `sign` forces an axis positive/negative.
 *
 * boxrandom draws a random extent per axis inside distanceMin/Max;
 * sphererandom draws a radius instead - orthographic particles fill a 2D
 * annulus (uniform by area), perspective ones a 3D shell (uniform by
 * volume). */
static void spawn_one(XWEmitter *e) {
  XWParticle *p = &e->parts[e->alive];
  float rnd[3];
  int ax;
  float speed = dyn_or_one(&e->cfg.speed_mult);
  if (e->cfg.emitter_sphere) {
    float lo = e->cfg.dist_min[0], hi = e->cfg.dist_max[0], r;
    if (lo < 0.0f)
      lo = 0.0f;
    if (hi < lo) {
      r = lo;
      lo = hi;
      hi = r;
    }
    if ((e->cfg.flags & 4) == 0) {
      float ang = next_unit(&e->rng) * 6.28318530718f;
      r = sqrtf(lo * lo + next_unit(&e->rng) * (hi * hi - lo * lo));
      rnd[0] = r * cosf(ang);
      rnd[1] = r * sinf(ang);
      rnd[2] = next_unit(&e->rng) * hi;
      if (next_unit(&e->rng) < 0.5f)
        rnd[2] = -rnd[2];
    } else {
      float th = next_unit(&e->rng) * 6.28318530718f;
      float ct = next_unit(&e->rng) * 2.0f - 1.0f;
      float st = sqrtf(1.0f - ct * ct);
      r = cbrtf(lo * lo * lo +
                next_unit(&e->rng) * (hi * hi * hi - lo * lo * lo));
      rnd[0] = st * cosf(th) * r;
      rnd[1] = st * sinf(th) * r;
      rnd[2] = ct * r;
    }
    for (ax = 0; ax < 3; ax++) {
      rnd[ax] *= e->cfg.dirs[ax];
      if (e->cfg.sign[ax] == 1.0f)
        rnd[ax] = fabsf(rnd[ax]);
      else if (e->cfg.sign[ax] == -1.0f)
        rnd[ax] = -fabsf(rnd[ax]);
    }
  } else {
    for (ax = 0; ax < 3; ax++) {
      float d = e->cfg.dist_min[ax] +
                next_unit(&e->rng) * (e->cfg.dist_max[ax] - e->cfg.dist_min[ax]);
      if (next_unit(&e->rng) < 0.5f)
        d = -d;
      /* directions.y is flipped along with the emitter origin */
      rnd[ax] = d * (ax == 1 ? -e->cfg.dirs[1] : e->cfg.dirs[ax]);
    }
  }
  p->x = e->cfg.origin[0] + rnd[0];
  p->y = -e->cfg.origin[1] + rnd[1];
  p->vx = (e->cfg.vel_min[0] +
           next_unit(&e->rng) * (e->cfg.vel_max[0] - e->cfg.vel_min[0])) *
          speed;
  p->vy = -(e->cfg.vel_min[1] +
            next_unit(&e->rng) * (e->cfg.vel_max[1] - e->cfg.vel_min[1])) *
          speed;
  /* turbulentvelocityrandom adds a curl-noise direction on top */
  if (e->cfg.has_turb)
    apply_turbulent(&e->cfg, &e->rng, e->t, p->x, p->y, &p->vx, &p->vy);
  p->age = 0.0f;
  p->life = e->cfg.life * dyn_or_one(&e->cfg.life_mult);
  /* sizerandom: exponent-shaped draw, halved like the engine does */
  {
    float t = next_unit(&e->rng);
    float exp = e->cfg.size_exponent > 0.0f ? e->cfg.size_exponent : 1.0f;
    float adj = powf(t, exp);
    p->size = (e->cfg.size_min +
               adj * (e->cfg.size_max - e->cfg.size_min)) /
              2.0f * dyn_or_one(&e->cfg.size_mult);
  }
  p->r = e->cfg.color[0];
  p->g = e->cfg.color[1];
  p->b = e->cfg.color[2];
  if (e->cfg.has_color_random) {
    int k;
    for (k = 0; k < 3; k++) {
      float lo = e->cfg.color_min[k];
      float hi = e->cfg.color_max[k];
      float v = lo + next_unit(&e->rng) * (hi - lo);
      if (k == 0)
        p->r = v;
      else if (k == 1)
        p->g = v;
      else
        p->b = v;
    }
  }
  p->a = e->cfg.color[3];
  if (e->cfg.alpha_max > e->cfg.alpha_min)
    p->a = e->cfg.alpha_min +
           next_unit(&e->rng) * (e->cfg.alpha_max - e->cfg.alpha_min);
  p->a0 = p->a;
  p->rot = 0.0f;
  if (e->cfg.rot_max != e->cfg.rot_min)
    p->rot = e->cfg.rot_min +
             next_unit(&e->rng) * (e->cfg.rot_max - e->cfg.rot_min);
  /* angularvelocityrandom: an exponent-shaped draw, scaled by the speed
   * override (the engine draws all three axes; we keep the in-plane one) */
  if (e->cfg.has_av_random) {
    float ex = e->cfg.av_exponent > 0.0f ? e->cfg.av_exponent : 1.0f;
    p->av = (e->cfg.av_min +
             powf(next_unit(&e->rng), ex) * (e->cfg.av_max - e->cfg.av_min)) *
            speed;
  }
  p->rnd = next_unit(&e->rng);
  /* oscillator operators draw their per-particle frequency/scale/phase once
   * (the engine initialises the state on the operator's first run); the
   * phase spans phaseMin..phaseMax+2pi and the base is the post-initializer
   * value the oscillator scales */
  if (e->cfg.osc_pos.present) {
    for (ax = 0; ax < 3; ax++) {
      p->osc_pf[ax] =
          e->cfg.osc_pos.freq_min +
          next_unit(&e->rng) * (e->cfg.osc_pos.freq_max - e->cfg.osc_pos.freq_min);
      p->osc_ps[ax] =
          e->cfg.osc_pos.scale_min +
          next_unit(&e->rng) * (e->cfg.osc_pos.scale_max - e->cfg.osc_pos.scale_min);
      p->osc_pp[ax] = e->cfg.osc_pos.phase_min +
                      next_unit(&e->rng) *
                          (e->cfg.osc_pos.phase_max + 6.28318530718f -
                           e->cfg.osc_pos.phase_min);
    }
  }
  if (e->cfg.osc_alpha.present) {
    p->osc_af = e->cfg.osc_alpha.freq_min +
                next_unit(&e->rng) *
                    (e->cfg.osc_alpha.freq_max - e->cfg.osc_alpha.freq_min);
    p->osc_ap = e->cfg.osc_alpha.phase_min +
                next_unit(&e->rng) *
                    (e->cfg.osc_alpha.phase_max + 6.28318530718f -
                     e->cfg.osc_alpha.phase_min);
  }
  if (e->cfg.osc_size.present) {
    p->osc_sf = e->cfg.osc_size.freq_min +
                next_unit(&e->rng) *
                    (e->cfg.osc_size.freq_max - e->cfg.osc_size.freq_min);
    p->osc_sp = e->cfg.osc_size.phase_min +
                next_unit(&e->rng) *
                    (e->cfg.osc_size.phase_max + 6.28318530718f -
                     e->cfg.osc_size.phase_min);
  }
  p->osc_abase = p->a;
  p->osc_sbase = p->size;
  e->alive++;
}

int xw_emitter_update(XWEmitter *e, float dt) {
  int i, room;
  if (!e || !e->parts || dt < 0.0f)
    return -1;
  e->t += dt;
  e->acc += e->cfg.rate * dt;
  room = e->cfg.max_count - e->alive;
  while (e->acc >= 1.0f && room > 0) {
    spawn_one(e);
    e->acc -= 1.0f;
    room--;
  }
  if (e->acc >= 1.0f)
    e->acc = 0.0f;
  for (i = 0; i < e->alive; i++) {
    XWParticle *p = &e->parts[i];
    float speed = dyn_or_one(&e->cfg.speed_mult);
    /* movement operator: integrate, then apply gravity (Y flipped) + drag */
    p->x += p->vx * dt;
    p->y += p->vy * dt;
    p->vx += e->cfg.gravity_v[0] * dt * speed;
    p->vy += -e->cfg.gravity_v[1] * dt * speed;
    if (e->cfg.drag > 0.0f) {
      float df = 1.0f - e->cfg.drag * dt;
      if (df < 0.0f)
        df = 0.0f;
      p->vx *= df;
      p->vy *= df;
    }
    p->age += dt;
    /* alphafade operator: fade in / out across the lifetime position */
    if (e->cfg.has_alpha_fade && p->life > 0.0f) {
      float lf = p->age / p->life;
      float fi = e->cfg.fade_in;
      float fo = e->cfg.fade_out;
      if (lf <= fi && fi > 0.0f) {
        p->a = p->a0 * (lf / fi);
      } else if (lf > fo && fo < 1.0f) {
        p->a = p->a0 * (1.0f - (lf - fo) / (1.0f - fo));
      } else {
        p->a = p->a0;
      }
      /* the engine keeps the oscillator's alpha base in sync here so
       * oscillatealpha scales the faded value */
      p->osc_abase = p->a;
    }
    /* oscillateposition: integrate -scale*w*sin(w*age + phase)*dt per axis
     * (the engine's w collapses to the frequency) */
    if (e->cfg.osc_pos.present) {
      p->x += -p->osc_ps[0] * p->osc_pf[0] *
              sinf(p->osc_pf[0] * p->age + p->osc_pp[0]) * dt *
              e->cfg.osc_pos.mask[0] * speed;
      p->y += -p->osc_ps[1] * p->osc_pf[1] *
              sinf(p->osc_pf[1] * p->age + p->osc_pp[1]) * dt *
              e->cfg.osc_pos.mask[1] * speed;
    }
    if (e->cfg.osc_alpha.present) {
      float cv = (cosf(p->osc_af * p->age + p->osc_ap) + 1.0f) * 0.5f;
      p->a = p->osc_abase *
             (e->cfg.osc_alpha.scale_min +
              cv * (e->cfg.osc_alpha.scale_max - e->cfg.osc_alpha.scale_min));
    }
    if (e->cfg.osc_size.present) {
      float cv = (cosf(p->osc_sf * p->age + p->osc_sp) + 1.0f) * 0.5f;
      p->size = p->osc_sbase *
                (e->cfg.osc_size.scale_min +
                 cv * (e->cfg.osc_size.scale_max - e->cfg.osc_size.scale_min));
    }
    /* controlpointattract: a radial force for particles within threshold/2
     * of the control point (the engine halves the threshold first, and a
     * negative scale pushes them away) */
    if (e->cfg.has_cp_attract && e->cfg.cp_index >= 0 &&
        e->cfg.cp_index < e->cfg.n_control_points) {
      float thr = e->cfg.cp_threshold * 0.5f;
      float cx = e->cfg.cp_offsets[e->cfg.cp_index][0] + e->cfg.cp_origin[0];
      float cy = e->cfg.cp_offsets[e->cfg.cp_index][1] + e->cfg.cp_origin[1];
      float dx = cx - p->x, dy = cy - p->y;
      float d = sqrtf(dx * dx + dy * dy);
      if (d > 0.001f && d < thr) {
        p->vx += (dx / d) * e->cfg.cp_scale * dt * speed;
        p->vy += (dy / d) * e->cfg.cp_scale * dt * speed;
      }
    }
    /* angularmovement: spin by the current angular velocity, accelerate with
     * the force, decay with drag, then wrap the angle (the engine keeps it in
     * (-pi, pi] so the sprite rotation stays well conditioned) */
    if (e->cfg.has_angular_movement) {
      float df;
      p->rot += p->av * dt * speed;
      p->av += e->cfg.am_force_z * dt * speed;
      df = 1.0f - e->cfg.am_drag * dt;
      if (df < 0.0f)
        df = 0.0f;
      p->av *= df;
      while (p->rot > 3.14159265f)
        p->rot -= 6.28318530718f;
      while (p->rot < -3.14159265f)
        p->rot += 6.28318530718f;
    }
    if (p->age > p->life) {
      e->alive--;
      if (i != e->alive)
        e->parts[i] = e->parts[e->alive];
      i--;
    }
  }
  return e->alive;
}

