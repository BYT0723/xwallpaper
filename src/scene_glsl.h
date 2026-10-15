#ifndef XW_SCENE_GLSL_H
#define XW_SCENE_GLSL_H

#include "xw_err.h"

/* Shader compilation through glslang -> SPIR-V -> SPIRV-Cross, the pipeline
 * linux-wallpaperengine uses (GLSLContext::toGlsl): parsing the preprocessed
 * GLSL with glslang and re-emitting it normalises the dialect, maps
 * locations/bindings and catches errors with proper messages instead of
 * leaving them to the driver. Both libraries are permissively licensed
 * (BSD/Apache-2.0) so this stays clean under the project's MIT license.
 *
 * Optional: when the libraries are absent the module is a no-op and the
 * renderer falls back to handing the preprocessed source to the driver. */

/* 1 when the pipeline is compiled in. */
int xw_glsl_available(void);

/* Round-trip a preprocessed GLSL 330 vertex/fragment pair. On success
 * *vert_out / *frag_out hold NUL-terminated GLSL 330 the caller frees with
 * free(), and 0 is returned. On failure -1 is returned and err (optional)
 * carries the compiler log. */
int xw_glsl_roundtrip(const char *vert_src, const char *frag_src,
                      char **vert_out, char **frag_out, XWError *err);

#endif
