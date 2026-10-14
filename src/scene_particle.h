#ifndef XW_SCENE_PARTICLE_H
#define XW_SCENE_PARTICLE_H

#include "scene_gl.h"
#include "scene_model.h"

/* CPU particle emitter (GL-free; rendering lands in Task 9b).
 * Simulation runs in object-local space (origin applied at draw).
 * Spawn uses a fixed-seed LCG so frames are deterministic. Particles are
 * stored compacted (swap-remove on death); alive count = live prefix. */
typedef struct XWParticle {
  float x, y;
  float vx, vy;
  float age, life, size;
  float r, g, b, a;
  float a0;    /* alpha before the alphafade operator */
  float frame; /* sprite-sheet frame index (set by the renderer) */
  float rot;   /* in-plane rotation (radians) */
  float rnd;   /* stable per-particle random draw (randomframe) */
  /* per-particle oscillator state, drawn once at spawn like the engine's
   * lazy per-particle initialisation */
  float osc_pf[3], osc_ps[3], osc_pp[3]; /* oscillateposition: freq/scale/phase */
  float osc_af, osc_ap;                  /* oscillatealpha: frequency, phase */
  float osc_sf, osc_sp;                  /* oscillatesize: frequency, phase */
  float osc_abase;                       /* alpha base the oscillator scales */
  float osc_sbase;                       /* size base the oscillator scales */
  float av;                              /* angular velocity (in-plane, rad/s) */
} XWParticle;

typedef struct XWEmitter {
  XWParticleCfg cfg;
  XWParticle *parts;
  int alive;
  float acc;
  unsigned int rng;
  /* screen size for the emitter-origin transform (engine converts the
   * emitter origin from screen space into centered space at setup) */
  float screen_w, screen_h;
  float t; /* animation clock for sprite-sheet sequences */
} XWEmitter;

int xw_emitter_init(XWEmitter *e, const XWParticleCfg *cfg);
void xw_emitter_free(XWEmitter *e);

/* Advance by dt seconds: spawn rate*dt (accumulated), integrate velocity
 * with gravity, age and kill the dead. Returns live count, -1 on bad args. */
int xw_emitter_update(XWEmitter *e, float dt);

#endif
