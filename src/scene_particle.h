#ifndef XW_SCENE_PARTICLE_H
#define XW_SCENE_PARTICLE_H

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
} XWParticle;

typedef struct XWEmitter {
  XWParticleCfg cfg;
  XWParticle *parts;
  int alive;
  float acc;
  unsigned int rng;
} XWEmitter;

int xw_emitter_init(XWEmitter *e, const XWParticleCfg *cfg);
void xw_emitter_free(XWEmitter *e);

/* Advance by dt seconds: spawn rate*dt (accumulated), integrate velocity
 * with gravity, age and kill the dead. Returns live count, -1 on bad args. */
int xw_emitter_update(XWEmitter *e, float dt);

#endif
