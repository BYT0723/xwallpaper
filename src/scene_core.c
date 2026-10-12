#include "scene_core.h"

int xw_core_present_uvs(float *u0, float *v0, float *u1, float *v1) {
  if (u0) *u0 = 0.0f;
  if (v0) *v0 = 0.0f;
  if (u1) *u1 = 1.0f;
  if (v1) *v1 = 1.0f;
  return 0;
}
