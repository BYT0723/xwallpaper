#ifndef XW_SCENE_SHADER_H
#define XW_SCENE_SHADER_H

#include <stddef.h>

/* WE dialect to GLSL3 translation. Fragment shorthand below; vertex files
 * need is_vertex=1 so `varying` becomes `out`. */
int xw_shader_translate(const char *src, char *dst, size_t n);
int xw_shader_translate_stage(const char *src, char *dst, size_t n,
                              int is_vertex);

#define XW_SHADER_DEFS_MAX 32

/* One annotated uniform: `uniform float g_Speed; // {"material":"speed",
 * "default":1, ...}`. kind is the component count (1-4); v holds the
 * default (missing comps are zero); material is "" when unannotated. */
typedef struct XWShaderDef {
  char name[64];
  char material[64];
  int kind;
  float v[4];
} XWShaderDef;

/* Scan (translated or raw) sources for annotated uniforms; appends up to
 * cap entries. Returns 0 ok, -1 on bad args (never fails on content). */
int xw_shader_collect_defaults(const char *src, XWShaderDef *defs, int *ndefs,
                               int cap);

#endif
