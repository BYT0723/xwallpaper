#ifndef XW_SCENE_GL_H
#define XW_SCENE_GL_H

#include <X11/Xlib.h>

#include "scene_core.h"
#include "scene_model.h"
#include "xw_err.h"

/* Desktop-GL runtime for scene wallpapers (daemon-only).
 *
 * Context: GLX direct context bound to the wallpaper X window, created on
 * the FBConfig matching the window visual (same approach as the mpv --wid
 * backend). All calls run on the daemon main thread (tick path).
 *
 * Fragment contract: sources pass through xw_shader_translate, so WE
 * dialect (gl_FragColor/texture2D/varying/#include) is accepted. The
 * renderer binds textures and time by uniform name, trying WE names
 * first (g_Texture0, g_Time) then xw_ names (xw_tex0, xw_time); a missing
 * time uniform is fine, a missing sampler renders black. The builtin
 * vertex shader feeds v_TexCoord/vUv/v_uv; FBO ping-pong runs at window
 * size (object-size FBOs are a later task); image passes render opaque,
 * particle passes enable blending transiently (see xw_gl_blend_begin).
 */

typedef struct XWGl XWGl;

/* Create + make current on win. Graceful -1 when GLX is unavailable
 * (caller falls back to the clear-color fill). */
int xw_gl_create(XWGl **out, Display *dpy, Window win, int w, int h,
                 XWError *err);
void xw_gl_destroy(XWGl *g);

/* Upload a PNG file (imlib2), cached by path. */
int xw_gl_upload(XWGl *g, const char *path, unsigned int *tex, XWError *err);

/* Upload raw RGBA pixels (top row first), uncached. */
int xw_gl_upload_rgba(XWGl *g, const unsigned char *px, int w, int h,
                      unsigned int *tex, XWError *err);

/* Delete an uploaded texture (0 is a no-op). */
int xw_gl_delete_texture(XWGl *g, unsigned int tex);

/* Compile+link a WE-dialect fragment file with the builtin vertex shader,
 * cached by path. */
int xw_gl_program_we(XWGl *g, const char *frag_path,
                     const XWSceneCombo *combos, int n_combos,
                     unsigned int *prog, XWError *err);

/* Builtin base-copy program (samples xw_tex0). */
int xw_gl_program_copy(XWGl *g, unsigned int *prog, XWError *err);

/* Builtin composite program: samples xw_tex0, scales alpha by xw_alpha.
 * Final pass to the window goes through this (with blending) so resolved
 * per-object alpha modulates arbitrary effect chains. */
int xw_gl_program_composite(XWGl *g, unsigned int *prog, XWError *err);

/* One particle splat in window pixels (x/y) with size px and alpha. */
typedef struct XWPoint {
  float x, y;
  float size, alpha;
} XWPoint;

/* Builtin point-sprite program (samples xw_tex0, multiplies alpha). */
int xw_gl_program_points(XWGl *g, unsigned int *prog, XWError *err);

/* Draw n point sprites on the bound target (positions window pixels). */
int xw_gl_draw_points(XWGl *g, unsigned int prog, unsigned int tex,
                      const XWPoint *pts, int n);

/* Transient blending around particle draws (core blend-state mapping). */
int xw_gl_blend_begin(XWGl *g, XWBlend mode);
int xw_gl_blend_end(XWGl *g);

/* Bind render target: -1 = window backbuffer, 0/1 = internal FBO. */
int xw_gl_bind_target(XWGl *g, int fbo);

/* Clear the bound target to an opaque color. */
int xw_gl_clear(XWGl *g, float r, float gg, float b);

/* Draw the fullscreen quad with prog/tex/time. */
int xw_gl_draw(XWGl *g, unsigned int prog, unsigned int tex, float time);

/* Like draw, also binding xw_alpha when the program declares it. */
int xw_gl_draw_alpha(XWGl *g, unsigned int prog, unsigned int tex, float time,
                     float alpha);

/* Effect-pass draw: binds g_Texture0..7 (unit 0 = input, the rest = slots
 * with input as fallback), g_Texture{i}Resolution vec4s, identity MVP
 * matrices, g_Time, and annotated uniform defaults overridden by the
 * scene pass constants (matched by material name). slots[0] overrides
 * the input when its tex is nonzero. res layout: {texW,texH,realW,realH}. */
#define XW_GL_WE_SLOTS 8
typedef struct XWWeSlot {
  unsigned int tex;
  float res[4];
} XWWeSlot;
int xw_gl_draw_we(XWGl *g, unsigned int prog, unsigned int input,
                  const float input_res[4], const XWWeSlot *slots,
                  const XWSceneConst *consts, int n_consts, float time);

/* Pixel dims of a cached texture (texW/texH, realW/realH). */
int xw_gl_texture_size(XWGl *g, unsigned int tex, int *tw, int *th, int *rw,
                       int *rh);

/* Current render target size (window/FBO pixels). */
int xw_gl_target_size(XWGl *g, int *w, int *h);

/* WE builtin util/ textures (black/white/noflow/perlin_256), cached. */
int xw_gl_util_texture(XWGl *g, const char *name, unsigned int *tex,
                       float res[4]);

/* Texture id backing internal FBO idx (for ping-pong sources). */
int xw_gl_fbo_texture(XWGl *g, int idx, unsigned int *tex);

/* Draw prog/tex as an opaque window-pixel rect (top-left dx/dy, size dw/dh).
 * Used for text and other UI-space quads; effect chains keep their own
 * fullscreen path. */
int xw_gl_draw_rect(XWGl *g, unsigned int prog, unsigned int tex, float time,
                    int dx, int dy, int dw, int dh);

/* Like draw_rect, also binding xw_alpha when the program declares it. */
int xw_gl_draw_rect_alpha(XWGl *g, unsigned int prog, unsigned int tex,
                          float time, float alpha, int dx, int dy, int dw,
                          int dh);

/* Swap window buffers. */
int xw_gl_present(XWGl *g);

#endif /* XW_SCENE_GL_H */
