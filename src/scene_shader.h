#ifndef XW_SCENE_SHADER_H
#define XW_SCENE_SHADER_H

#include <stddef.h>

#include "scene_model.h"

/* Wallpaper Engine shader preprocessing.
 *
 * Workshop shaders are HLSL-flavoured snippets: no #version line, `varying`/
 * `attribute` qualifiers, `texture2D`/`mul`/`CASTn`/`frac` helpers, a
 * `// [COMBO] {...}` annotation per combi feature, and `#include` of engine
 * headers (common.h, common_blending.h, ...) that are NOT shipped inside the
 * package. This module turns one such snippet into plain GLSL 3.30 that
 * glslang accepts, following the engine's documented header semantics:
 *   https://docs.wallpaperengine.io/en/scene/shader/headers.html
 *   https://docs.wallpaperengine.io/en/scene/shader/syntax.html
 *
 * Layout of the emitted program:
 *   #version 330 / precision / engine dialect defines / combo defines /
 *   stage defines / <body with #include content inlined>
 */

/* External include resolver: return the source of `name`, or NULL when it is
 * unavailable. Called before the built-in headers; NULL resolver is fine. */
typedef const char *(*XWShaderInclude)(void *user, const char *name);

/* Preprocess `src` into `dst` (NUL-terminated, truncated -> -1).
 * is_vertex selects the attribute/varying and output mapping.
 * combos are emitted as `#define NAME value` before the body.
 * texture_slots is a bitmask of bound sampler slots (bit N = g_TextureN has
 * a texture): the engine enables a sampler's `combo` (e.g. MASK) when its
 * texture is actually present, which shaders rely on. */
int xw_shader_preprocess(const char *src, char *dst, size_t n, int is_vertex,
                         const XWSceneCombo *combos, int n_combos,
                         unsigned int texture_slots, XWShaderInclude inc,
                         void *user);

/* Convenience wrappers: no combos, built-in headers only. */
int xw_shader_translate_stage(const char *src, char *dst, size_t n,
                              int is_vertex);
int xw_shader_translate(const char *src, char *dst, size_t n);

/* Workshop shaders occasionally disagree between the linked units: the
 * vertex declares `varying vec2 X` while the fragment wants `varying vec4 X`
 * (glitter_prepare is a shipped example). Widen the vertex declaration in
 * place and pad bare `X = expr;` assignments to vec4 so the pair links.
 * `vert` is rewritten in place within n bytes; best effort, returns 0.
 * `combos` are the active defines, so #if branches the engine turns off are
 * not mistaken for real declarations. */
int xw_shader_fix_linked_varyings(const char *frag, char *vert, size_t n,
                                  const XWSceneCombo *combos, int n_combos);

/* Built-in engine header source ("common.h", "common_fragment.h",
 * "common_vertex.h", "common_blending.h"), or NULL when unknown. */
const char *xw_shader_builtin_header(const char *name);

/* Optional Wallpaper Engine asset directory (its shaders/ folder holds the
 * genuine common.h and the built-in effect shaders such as
 * genericparticle). When set, #include resolution prefers it over our
 * reimplemented headers. Set once at daemon start. */
void xw_shader_set_assets_dir(const char *dir);
const char *xw_shader_assets_dir(void);
/* Decide which asset directory to use: `env` (XW_ASSETS_DIR) wins when set,
 * otherwise the Steam libraries under `home` are probed for a
 * wallpaper_engine/assets that holds shaders/common.h. Returns 0 and writes
 * the directory into `out` when one is found, -1 otherwise. Read-only. */
int xw_shader_resolve_assets_dir(const char *env, const char *home, char *out,
                                 size_t n);
/* Resolve a built-in shader name ("genericparticle") to a file under the
 * assets directory; returns 0 and fills out when found. */
int xw_shader_assets_shader(const char *name, char *out, size_t n);

#define XW_SHADER_DEFS_MAX 32

/* One annotated uniform:
 *   uniform float g_Speed; // {"material":"speed","default":1}
 * kind is the component count (1-4); v holds the default (missing comps are
 * zero, a scalar default is splatted); material is "" when unannotated. */
typedef struct XWShaderDef {
  char name[64];
  char material[64];
  int kind; /* 0 = sampler with a texture default, else component count 1-4 */
  float v[4];
  char tex[64]; /* kind == 0: the annotated `default` texture ref */
} XWShaderDef;

/* Scan shader source for annotated uniforms; appends up to cap entries.
 * Returns 0 on success, -1 on bad args (never fails on content). */
int xw_shader_collect_defaults(const char *src, XWShaderDef *defs, int *ndefs,
                               int cap);

#endif
