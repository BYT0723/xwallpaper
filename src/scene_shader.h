#ifndef XW_SCENE_SHADER_H
#define XW_SCENE_SHADER_H

#include <stddef.h>

/* WE dialect to GLSL3 translation. Fragment shorthand below; vertex files
 * need is_vertex=1 so `varying` becomes `out`. */
int xw_shader_translate(const char *src, char *dst, size_t n);
int xw_shader_translate_stage(const char *src, char *dst, size_t n,
                              int is_vertex);

#endif
