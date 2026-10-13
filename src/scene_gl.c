#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XW_HAVE_GL
#include <GL/glew.h>
#include <GL/glx.h>
#endif

#include <Imlib2.h>

#include "scene_gl.h"
#include "scene_glsl.h"
#include "scene_shader.h"
#include "scene_tex.h"

#define XW_GL_PROG_MAX 16
#define XW_GL_TEXCACHE_MAX 24
#define XW_GL_DEFS_MAX 24
#define XW_GL_TARGETS_MAX 8

#ifdef XW_HAVE_GL

struct XWGl {
  Display *dpy;
  Window win;
  GLXContext ctx;
  int w, h;
  GLuint quad_vao;
  GLuint quad_vbo;
  GLuint points_vao;
  GLuint points_vbo;
  GLuint rect_vao;
  GLuint rect_vbo;
  GLuint fbos[2];
  GLuint fbo_tex[2];
  struct {
    char key[512];
    GLuint prog;
    XWShaderDef defs[XW_GL_DEFS_MAX];
    int ndefs;
  } progs[XW_GL_PROG_MAX];
  int nprogs;
  struct {
    char path[1024];
    GLuint tex;
    int tw, th, rw, rh;
    unsigned int flags; /* WE TextureFlags of the uploaded texture */
  } texcache[XW_GL_TEXCACHE_MAX];
  int ntex;
  GLuint utils[4];
  int flip; /* 1 while rendering into an FBO (see bind_target) */
  /* named offscreen targets declared by effects ("fbos") */
  struct {
    char name[64];
    GLuint fbo, tex;
    int w, h;
  } targets[XW_GL_TARGETS_MAX];
  int ntargets;
};

static const char *vert_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 uv;\n"
    "out vec2 v_TexCoord;\n"
    "out vec2 vUv;\n"
    "out vec2 v_uv;\n"
    "uniform float xw_flip;\n"
    "void main() {\n"
    "  v_TexCoord = uv;\n"
    "  vUv = uv;\n"
    "  v_uv = uv;\n"
    "  gl_Position = vec4(pos.x, pos.y * xw_flip, 0.0, 1.0);\n"
    "}\n";

static const char *copy_frag_src =
    "#version 330 core\n"
    "in vec2 v_TexCoord;\n"
    "uniform sampler2D xw_tex0;\n"
    "out vec4 xw_out;\n"
    "void main() { xw_out = texture(xw_tex0, v_TexCoord); }\n";

static const char *composite_frag_src =
    "#version 330 core\n"
    "in vec2 v_TexCoord;\n"
    "uniform sampler2D xw_tex0;\n"
    "uniform float xw_alpha;\n"
    "out vec4 xw_out;\n"
    "void main() {\n"
    "  vec4 c = texture(xw_tex0, v_TexCoord);\n"
    "  xw_out = vec4(c.rgb, c.a * xw_alpha);\n"
    "}\n";

static const char *points_vert_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 ps;\n"
    "out float v_alpha;\n"
    "uniform float xw_flip;\n"
    "void main() {\n"
    "  v_alpha = ps.y;\n"
    "  gl_PointSize = ps.x;\n"
    "  gl_Position = vec4(pos.x, pos.y * xw_flip, 0.0, 1.0);\n"
    "}\n";

static const char *streaks_vert_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 am;\n" /* alpha, along */
    "uniform float xw_flip;\n"
    "uniform vec2 xw_size;\n"
    "out float v_alpha;\n"
    "out float v_along;\n"
    "void main() {\n"
    "  v_alpha = am.x;\n"
    "  v_along = am.y;\n"
    "  vec2 ndc = vec2(pos.x / xw_size.x * 2.0 - 1.0,\n"
    "                  1.0 - pos.y / xw_size.y * 2.0);\n"
    "  gl_Position = vec4(ndc.x, ndc.y * xw_flip, 0.0, 1.0);\n"
    "}\n";

/* Streaks sample a vertical slice of the sprite: the particle trails are
 * drawn as thin quads stretched along their velocity (the engine's
 * spritetrail renderer), with the sprite's alpha shaping the tail. */
static const char *streaks_frag_src =
    "#version 330 core\n"
    "in float v_alpha;\n"
    "in float v_along;\n"
    "uniform sampler2D xw_tex0;\n"
    "out vec4 xw_out;\n"
    "void main() {\n"
    "  vec4 c = texture(xw_tex0, vec2(0.5, v_along));\n"
    "  xw_out = vec4(c.rgb, c.a * v_alpha);\n"
    "}\n";

static const char *points_frag_src =
    "#version 330 core\n"
    "in float v_alpha;\n"
    "uniform sampler2D xw_tex0;\n"
    "out vec4 xw_out;\n"
    "void main() {\n"
    "  vec2 pc = vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y);\n"
    "  vec4 c = texture(xw_tex0, pc);\n"
    "  xw_out = vec4(c.rgb, c.a * v_alpha);\n"
    "}\n";

/* Fullscreen quad, V flipped (imlib2 rows are top-first, GL expects
 * bottom-first). Positions NDC, uvs mapped so the image lands upright. */
static const float quad_verts[] = {
    -1.0f, -1.0f, 0.0f, 1.0f, //
    1.0f, -1.0f, 1.0f, 1.0f, //
    -1.0f, 1.0f, 0.0f, 0.0f, //
    1.0f, 1.0f, 1.0f, 0.0f, //
};

static int gl_error(const char *what, XWError *err) {
  XW_ERROR_SET(err, 1, "gl: %s", what);
  return -1;
}

static int make_fbo(XWGl *g, int idx, XWError *err) {
  glGenTextures(1, &g->fbo_tex[idx]);
  glBindTexture(GL_TEXTURE_2D, g->fbo_tex[idx]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g->w, g->h, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glGenFramebuffers(1, &g->fbos[idx]);
  glBindFramebuffer(GL_FRAMEBUFFER, g->fbos[idx]);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, g->fbo_tex[idx], 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return gl_error("incomplete FBO", err);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return 0;
}

int xw_gl_create(XWGl **out, Display *dpy, Window win, int w, int h,
                 XWError *err) {
  XWindowAttributes wa;
  XVisualInfo tmpl, *vi;
  int nvi = 0, i, ncfg = 0;
  GLXFBConfig *cfgs, match = NULL;
  XWGl *g;
  if (!out || !dpy || w <= 0 || h <= 0)
    return gl_error("invalid args", err);
  *out = NULL;
  if (!XGetWindowAttributes(dpy, win, &wa))
    return gl_error("cannot query window", err);
  tmpl.visualid = XVisualIDFromVisual(wa.visual);
  vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &nvi);
  if (!vi || nvi < 1) {
    if (vi)
      XFree(vi);
    return gl_error("no visual info", err);
  }
  XFree(vi);
  cfgs = glXChooseFBConfig(dpy, XScreenNumberOfScreen(wa.screen), NULL, &ncfg);
  if (!cfgs || ncfg < 1)
    return gl_error("no GLX configs", err);
  for (i = 0; i < ncfg; i++) {
    XVisualInfo *cvi = glXGetVisualFromFBConfig(dpy, cfgs[i]);
    if (cvi) {
      if (cvi->visual == wa.visual)
        match = cfgs[i];
      XFree(cvi);
    }
    if (match)
      break;
  }
  if (!match) {
    XFree(cfgs);
    return gl_error("no FBConfig for window visual", err);
  }
  g = calloc(1, sizeof(*g));
  if (!g) {
    XFree(cfgs);
    return gl_error("out of memory", err);
  }
  g->dpy = dpy;
  g->win = win;
  g->w = w;
  g->h = h;
  g->ctx = glXCreateNewContext(dpy, match, GLX_RGBA_TYPE, NULL, True);
  XFree(cfgs);
  if (!g->ctx) {
    free(g);
    return gl_error("cannot create GL context", err);
  }
  if (!glXMakeCurrent(dpy, win, g->ctx)) {
    glXDestroyContext(dpy, g->ctx);
    free(g);
    return gl_error("cannot bind GL context", err);
  }
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    glXDestroyContext(dpy, g->ctx);
    free(g);
    return gl_error("cannot init GLEW", err);
  }
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glGenVertexArrays(1, &g->quad_vao);
  glGenBuffers(1, &g->quad_vbo);
  glBindVertexArray(g->quad_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->quad_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glBindVertexArray(0);
  glGenVertexArrays(1, &g->points_vao);
  glGenBuffers(1, &g->points_vbo);
  glBindVertexArray(g->points_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->points_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glBindVertexArray(0);
  glGenVertexArrays(1, &g->rect_vao);
  glGenBuffers(1, &g->rect_vbo);
  glBindVertexArray(g->rect_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->rect_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glBindVertexArray(0);
  if (make_fbo(g, 0, err) != 0 || make_fbo(g, 1, err) != 0) {
    xw_gl_destroy(g);
    return -1;
  }
  *out = g;
  return 0;
}

void xw_gl_destroy(XWGl *g) {
  int i;
  if (!g)
    return;
  if (g->ctx) {
    glXMakeCurrent(g->dpy, g->win, g->ctx);
    for (i = 0; i < g->nprogs; i++) glDeleteProgram(g->progs[i].prog);
    for (i = 0; i < g->ntex; i++) glDeleteTextures(1, &g->texcache[i].tex);
    glDeleteTextures(4, g->utils);
    for (i = 0; i < g->ntargets; i++) {
      glDeleteFramebuffers(1, &g->targets[i].fbo);
      glDeleteTextures(1, &g->targets[i].tex);
    }
    glDeleteFramebuffers(2, g->fbos);
    glDeleteTextures(2, g->fbo_tex);
    glDeleteBuffers(1, &g->quad_vbo);
    glDeleteVertexArrays(1, &g->quad_vao);
    glDeleteBuffers(1, &g->points_vbo);
    glDeleteVertexArrays(1, &g->points_vao);
    glDeleteBuffers(1, &g->rect_vbo);
    glDeleteVertexArrays(1, &g->rect_vao);
    glXMakeCurrent(g->dpy, None, NULL);
    glXDestroyContext(g->dpy, g->ctx);
  }
  free(g);
}


static int cache_store(XWGl *g, const char *path, GLuint t, int tw, int th,
                     int rw, int rh, unsigned int flags, XWError *err) {
  if (g->ntex >= XW_GL_TEXCACHE_MAX) {
    if (t)
      glDeleteTextures(1, &t);
    XW_ERROR_SET(err, 1, "texture cache full");
    return -1;
  }
  snprintf(g->texcache[g->ntex].path, sizeof(g->texcache[g->ntex].path), "%s",
           path);
  g->texcache[g->ntex].tex = t;
  g->texcache[g->ntex].tw = tw;
  g->texcache[g->ntex].th = th;
  g->texcache[g->ntex].rw = rw;
  g->texcache[g->ntex].rh = rh;
  g->texcache[g->ntex].flags = flags;
  g->ntex++;
  return 0;
}

static int cache_fail(XWGl *g, const char *path, XWError *err, const char *msg) {
  if (g->ntex < XW_GL_TEXCACHE_MAX) {
    snprintf(g->texcache[g->ntex].path, sizeof(g->texcache[g->ntex].path),
             "%s", path);
    g->texcache[g->ntex].tex = 0;
    g->texcache[g->ntex].tw = g->texcache[g->ntex].th = 0;
    g->texcache[g->ntex].rw = g->texcache[g->ntex].rh = 0;
    g->texcache[g->ntex].flags = 0;
    g->ntex++;
  }
  XW_ERROR_SET(err, 1, "%s '%s'", msg, path);
  return -1;
}

static int upload_rgba_bytes(XWGl *g, const unsigned char *px, int w, int h,
                             GLuint *t) {
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glGenTextures(1, t);
  glBindTexture(GL_TEXTURE_2D, *t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               px);
  return 0;
}

/* current imlib image must already be set by the caller */
static int upload_imlib_current(XWGl *g, const char *path, GLuint *t,
                                int *wp, int *hp, XWError *err) {
  int iw, ih, x, y;
  DATA32 *data;
  unsigned char *px;
  iw = imlib_image_get_width();
  ih = imlib_image_get_height();
  if (iw <= 0 || ih <= 0 || iw > 8192 || ih > 8192) {
    XW_ERROR_SET(err, 1, "bad texture size '%s'", path);
    return -1;
  }
  data = imlib_image_get_data_for_reading_only();
  px = malloc((size_t)iw * ih * 4);
  if (!px)
    return gl_error("out of memory", err);
  for (y = 0; y < ih; y++)
    for (x = 0; x < iw; x++) {
      DATA32 d = data[y * iw + x];
      unsigned char *o = px + ((size_t)y * iw + x) * 4;
      o[0] = (unsigned char)((d >> 16) & 0xff);
      o[1] = (unsigned char)((d >> 8) & 0xff);
      o[2] = (unsigned char)(d & 0xff);
      o[3] = (unsigned char)((d >> 24) & 0xff);
    }
  upload_rgba_bytes(g, px, iw, ih, t);
  free(px);
  if (wp)
    *wp = iw;
  if (hp)
    *hp = ih;
  return 0;
}

static int upload_texv(XWGl *g, const unsigned char *file, size_t flen,
                       GLuint *t, int *twp, int *thp, int *rwp, int *rhp,
                       unsigned int *flagsp, XWError *err) {
  XWTexImage img;
  int rc = -1;
  memset(&img, 0, sizeof(img));
  if (xw_tex_decode(file, flen, &img, err) != 0)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  switch (img.fmt) {
  case XW_TEXFMT_RGBA8:
    /* byte order exactly as the reference uploads it */
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (int)img.tex_w, (int)img.tex_h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px);
    rc = 0;
    break;
  case XW_TEXFMT_R8:
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (int)img.tex_w, (int)img.tex_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, img.px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    rc = 0;
    break;
  case XW_TEXFMT_RG88:
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, (int)img.tex_w, (int)img.tex_h, 0,
                 GL_RG, GL_UNSIGNED_BYTE, img.px);
    rc = 0;
    break;
  case XW_TEXFMT_DXT1:
  case XW_TEXFMT_DXT3:
  case XW_TEXFMT_DXT5: {
    GLenum internal = img.fmt == XW_TEXFMT_DXT1
                          ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                      : img.fmt == XW_TEXFMT_DXT3
                          ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                          : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal, (int)img.tex_w,
                           (int)img.tex_h, 0, (GLsizei)img.len, img.px);
    rc = glGetError() == GL_NO_ERROR ? 0 : -1;
    if (rc != 0)
      XW_ERROR_SET(err, 1, "driver rejected compressed texture");
    break;
  }
  case XW_TEXFMT_PNG: {
    Imlib_Context ictx = imlib_context_new();
    Imlib_Image im;
    if (!ictx) {
      XW_ERROR_SET(err, 1, "out of memory");
      break;
    }
    imlib_context_push(ictx);
    im = imlib_load_image_mem("x.png", img.px, img.len);
    if (!im) {
      imlib_context_pop();
      imlib_context_free(ictx);
      XW_ERROR_SET(err, 1, "cannot decode embedded png");
      break;
    }
    imlib_context_set_image(im);
    rc = upload_imlib_current(g, "embedded", t, NULL, NULL, err);
    imlib_free_image();
    imlib_context_pop();
    imlib_context_free(ictx);
    break;
  }
  }
  if (rc == 0) {
    if (twp)
      *twp = (int)img.tex_w;
    if (thp)
      *thp = (int)img.tex_h;
    if (rwp)
      *rwp = (int)img.w;
    if (rhp)
      *rhp = (int)img.h;
    if (flagsp)
      *flagsp = img.flags;
    /* WE TextureFlags: ClampUVs (2) keeps offset effects from wrapping */
    if (*t) {
      GLint wrap = (img.flags & 2u) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
      glBindTexture(GL_TEXTURE_2D, *t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    }
  }
  xw_tex_image_free(&img);
  return rc;
}

static unsigned char *slurp_file(const char *path, size_t *len, size_t cap) {
  FILE *f = fopen(path, "rb");
  long sz;
  unsigned char *b;
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  if (sz <= 0 || (size_t)sz > cap) {
    fclose(f);
    return NULL;
  }
  b = malloc((size_t)sz);
  if (!b) {
    fclose(f);
    return NULL;
  }
  if (fread(b, 1, (size_t)sz, f) != (size_t)sz) {
    free(b);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *len = (size_t)sz;
  return b;
}

int xw_gl_upload(XWGl *g, const char *path, unsigned int *tex, XWError *err) {
  Imlib_Context ictx;
  Imlib_Image img;
  GLuint t = 0;
  unsigned char *file = NULL;
  size_t flen = 0;
  int i, rc = -1;
  if (!g || !path || !tex)
    return gl_error("invalid args", err);
  for (i = 0; i < g->ntex; i++)
    if (strcmp(g->texcache[i].path, path) == 0) {
      if (!g->texcache[i].tex) {
        XW_ERROR_SET(err, 1, "cached texture failure '%s'", path);
        return -1;
      }
      *tex = g->texcache[i].tex;
      return 0;
    }
  file = slurp_file(path, &flen, 64 * 1024 * 1024);
  if (!file)
    return cache_fail(g, path, err, "cannot read texture");
  if (flen >= 4 && memcmp(file, "TEXV", 4) == 0) {
    int tw = 0, th = 0, rw = 0, rh = 0;
    unsigned int flags = 0;
    rc = upload_texv(g, file, flen, &t, &tw, &th, &rw, &rh, &flags, err);
    free(file);
    if (rc != 0) {
      cache_fail(g, path, err, "cannot decode texture");
      return -1;
    }
    if (cache_store(g, path, t, tw, th, rw, rh, flags, err) != 0)
      return -1;
    *tex = t;
    return 0;
  }
  free(file);
  ictx = imlib_context_new();
  if (!ictx)
    return gl_error("imlib_context_new failed", err);
  imlib_context_push(ictx);
  img = imlib_load_image_immediately_without_cache(path);
  if (!img) {
    imlib_context_pop();
    imlib_context_free(ictx);
    return cache_fail(g, path, err, "cannot load texture");
  }
  imlib_context_set_image(img);
  {
    int iw = 0, ih = 0;
    rc = upload_imlib_current(g, path, &t, &iw, &ih, err);
    imlib_free_image();
    imlib_context_pop();
    imlib_context_free(ictx);
    if (rc != 0) {
      cache_fail(g, path, err, "cannot decode texture");
      return -1;
    }
    if (cache_store(g, path, t, iw, ih, iw, ih, 2u, err) != 0)
      return -1;
  }
  *tex = t;
  return 0;
}

static int compile_stage(GLuint sh, const char *src, XWError *err,
                         const char *kind) {
  GLint ok = 0;
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    GLsizei n = 0;
    glGetShaderInfoLog(sh, sizeof(log) - 1, &n, log);
    log[n > 0 ? n : 0] = '\0';
    XW_ERROR_SET(err, 1, "%s compile: %.200s", kind, log);
    return -1;
  }
  return 0;
}

static int link_program(const char *key, const char *vert, const char *frag_src,
                        XWGl *g, unsigned int *prog, XWError *err) {
  GLuint vs, fs, p;
  int i;
  char log[1024];
  GLsizei n = 0;
  GLint ok = 0;
  for (i = 0; i < g->nprogs; i++)
    if (strcmp(g->progs[i].key, key) == 0) {
      *prog = g->progs[i].prog;
      return 0;
    }
  if (g->nprogs >= XW_GL_PROG_MAX) {
    XW_ERROR_SET(err, 1, "program cache full");
    return -1;
  }
  vs = glCreateShader(GL_VERTEX_SHADER);
  if (compile_stage(vs, vert, err, "vertex") != 0) {
    glDeleteShader(vs);
    return -1;
  }
  fs = glCreateShader(GL_FRAGMENT_SHADER);
  if (compile_stage(fs, frag_src, err, "fragment") != 0) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return -1;
  }
  p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  /* WE vertex files declare a_Position/a_TexCoord without locations;
   * pin them to the fullscreen-quad VAO slots (harmless when absent). */
  glBindAttribLocation(p, 0, "a_Position");
  glBindAttribLocation(p, 1, "a_TexCoord");
  glLinkProgram(p);
  glDetachShader(p, vs);
  glDetachShader(p, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    glGetProgramInfoLog(p, sizeof(log) - 1, &n, log);
    log[n > 0 ? n : 0] = '\0';
    glDeleteProgram(p);
    XW_ERROR_SET(err, 1, "link: %.200s", log);
    return -1;
  }
  snprintf(g->progs[g->nprogs].key, sizeof(g->progs[g->nprogs].key), "%s",
           key);
  g->progs[g->nprogs].prog = p;
  g->nprogs++;
  *prog = p;
  return 0;
}

static int slurp(const char *path, char **out, XWError *err) {
  FILE *f = fopen(path, "rb");
  long sz;
  char *b;
  if (!f) {
    XW_ERROR_SET(err, 1, "cannot open shader '%s'", path);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 256 * 1024) {
    fclose(f);
    XW_ERROR_SET(err, 1, "bad shader size '%s'", path);
    return -1;
  }
  b = malloc((size_t)sz + 4096);
  if (!b) {
    fclose(f);
    return gl_error("out of memory", err);
  }
  if (fread(b, 1, (size_t)sz, f) != (size_t)sz) {
    free(b);
    fclose(f);
    XW_ERROR_SET(err, 1, "cannot read shader '%s'", path);
    return -1;
  }
  fclose(f);
  b[sz] = '\0';
  *out = b;
  return 0;
}

static void program_key(char *key, size_t n, const char *frag_path,
                        const XWSceneCombo *combos, int n_combos,
                        unsigned int texture_slots) {
  size_t len;
  int i;
  snprintf(key, n, "%s|%u", frag_path, texture_slots);
  if (!combos)
    return;
  for (i = 0; i < n_combos; i++) {
    if (!combos[i].key[0])
      continue;
    len = strlen(key);
    snprintf(key + len, n > len ? n - len : 0, ";%s=%s", combos[i].key,
             combos[i].value);
    if (strlen(key) >= n - 1)
      break;
  }
}

int xw_gl_program_we(XWGl *g, const char *frag_path,
                     const XWSceneCombo *combos, int n_combos,
                     unsigned int texture_slots, unsigned int *prog,
                     XWError *err) {
  char *src = NULL, *dst = NULL;
  char vert_path[1152];
  char key[512];
  char *vsrc = NULL, *vdst = NULL;
  const char *vert_use = vert_src;
  size_t cap, vcap;
  size_t flen;
  int rc, i, slot = -1;
  if (!g || !frag_path || !prog)
    return gl_error("invalid args", err);
  program_key(key, sizeof(key), frag_path, combos, n_combos, texture_slots);
  for (i = 0; i < g->nprogs; i++)
    if (strcmp(g->progs[i].key, key) == 0) {
      *prog = g->progs[i].prog;
      return 0;
    }
  if (g->nprogs >= XW_GL_PROG_MAX) {
    XW_ERROR_SET(err, 1, "program cache full");
    return -1;
  }
  slot = g->nprogs;
  if (slurp(frag_path, &src, err) != 0)
    return -1;
  cap = strlen(src) + 65536;
  dst = malloc(cap);
  if (!dst) {
    free(src);
    return gl_error("out of memory", err);
  }
  if (xw_shader_preprocess(src, dst, cap, 0, combos, n_combos, texture_slots,
                           NULL, NULL) != 0) {
    free(src);
    free(dst);
    XW_ERROR_SET(err, 1, "cannot translate shader '%s'", frag_path);
    return -1;
  }
  /* WE effects ship a sibling vertex file (same basename, .vert) with
   * custom varyings; use it when present, else the builtin quad shader. */
  snprintf(vert_path, sizeof(vert_path), "%.1000s", frag_path);
  flen = strlen(vert_path);
  if (flen > 5 && strcmp(vert_path + flen - 5, ".frag") == 0) {
    XWError verr;
    memset(&verr, 0, sizeof(verr));
    memcpy(vert_path + flen - 5, ".vert", 6);
    if (slurp(vert_path, &vsrc, &verr) == 0) {
      /* the shipped pair may disagree on varying widths; align the vertex
       * side with the fragment before preprocessing */
      xw_shader_fix_linked_varyings(src, vsrc, strlen(vsrc) + 4096);
      vcap = strlen(vsrc) + 65536;
      vdst = malloc(vcap);
      if (!vdst) {
        free(vsrc);
        free(src);
        free(dst);
        return gl_error("out of memory", err);
      }
      if (xw_shader_preprocess(vsrc, vdst, vcap, 1, combos, n_combos,
                               texture_slots, NULL, NULL) != 0) {
        free(vsrc);
        free(vdst);
        free(src);
        free(dst);
        XW_ERROR_SET(err, 1, "cannot translate shader '%s'", vert_path);
        return -1;
      }
      free(vsrc);
      vert_use = vdst;
    }
  }
  free(src);
  /* collect annotated uniform defaults from the preprocessed source: the
   * SPIRV-Cross round trip below strips the // {"material":...} comments */
  g->progs[slot].ndefs = 0;
  xw_shader_collect_defaults(vert_use, g->progs[slot].defs,
                             &g->progs[slot].ndefs, XW_GL_DEFS_MAX);
  xw_shader_collect_defaults(dst, g->progs[slot].defs, &g->progs[slot].ndefs,
                             XW_GL_DEFS_MAX);
  /* optional normalisation through glslang -> SPIR-V -> SPIRV-Cross; the
   * preprocessed source is already valid GLSL 330, so a failure here just
   * falls back to handing it to the driver */
  if (xw_glsl_available()) {
    char *rv = NULL, *rf = NULL;
    XWError gerr;
    memset(&gerr, 0, sizeof(gerr));
    if (xw_glsl_roundtrip(vert_use, dst, &rv, &rf, &gerr) == 0) {
      free(vdst);
      free(dst);
      vdst = rv;
      dst = rf;
      vert_use = rv;
    }
  }
  rc = link_program(key, vert_use, dst, g, prog, err);
  free(dst);
  free(vdst);
  return rc;
}

int xw_gl_program_copy(XWGl *g, unsigned int *prog, XWError *err) {
  if (!g || !prog)
    return gl_error("invalid args", err);
  return link_program("xw_copy", vert_src, copy_frag_src, g, prog, err);
}

int xw_gl_program_composite(XWGl *g, unsigned int *prog, XWError *err) {
  if (!g || !prog)
    return gl_error("invalid args", err);
  return link_program("xw_composite", vert_src, composite_frag_src, g, prog,
                      err);
}

int xw_gl_program_points(XWGl *g, unsigned int *prog, XWError *err) {
  if (!g || !prog)
    return gl_error("invalid args", err);
  return link_program("xw_points", points_vert_src, points_frag_src, g, prog,
                      err);
}

static GLint sampler_location(GLuint prog);

int xw_gl_program_streaks(XWGl *g, unsigned int *prog, XWError *err) {
  if (!g || !prog)
    return gl_error("invalid args", err);
  return link_program("xw_streaks", streaks_vert_src, streaks_frag_src, g, prog,
                      err);
}

int xw_gl_draw_streaks(XWGl *g, unsigned int prog, unsigned int tex,
                       const XWStreak *st, int n) {
  GLint loc;
  float *verts;
  int i, rc = -1;
  if (!g || !prog || !st || n <= 0 || g->w <= 0 || g->h <= 0)
    return -1;
  /* two vertices per streak: head (along 0) and tail (along 1) */
  verts = malloc((size_t)n * 8 * sizeof(float));
  if (!verts)
    return -1;
  for (i = 0; i < n; i++) {
    float *v = verts + (size_t)i * 8;
    v[0] = st[i].x;
    v[1] = st[i].y;
    v[2] = st[i].alpha;
    v[3] = 0.0f;
    v[4] = st[i].x + st[i].dx;
    v[5] = st[i].y + st[i].dy;
    v[6] = st[i].alpha;
    v[7] = 1.0f;
  }
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glUseProgram(prog);
  loc = sampler_location(prog);
  if (loc >= 0 && tex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(loc, 0);
  }
  loc = glGetUniformLocation(prog, "xw_flip");
  if (loc >= 0)
    glUniform1f(loc, g->flip ? -1.0f : 1.0f);
  loc = glGetUniformLocation(prog, "xw_size");
  if (loc >= 0)
    glUniform2f(loc, (float)g->w, (float)g->h);
  glBindVertexArray(g->points_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->points_vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * 8 * sizeof(float)),
               verts, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glDrawArrays(GL_LINES, 0, n * 2);
  glBindVertexArray(0);
  glUseProgram(0);
  free(verts);
  rc = 0;
  return rc;
}

int xw_gl_bind_target(XWGl *g, int fbo) {
  if (!g)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  if (fbo < 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->w, g->h);
    g->flip = 0;
  } else if (fbo < 2) {
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbos[fbo]);
    glViewport(0, 0, g->w, g->h);
    /* FBO textures are sampled with v=0 at the top (WE convention) but the
     * GL framebuffer origin is bottom-left: flip Y on the way in so any
     * number of FBO round trips keeps the image upright. */
    g->flip = 1;
  } else {
    return -1;
  }
  return 0;
}

/* Create (or fetch) a named offscreen target. w/h <= 0 mean the window
 * size; format "r8" gets a single-channel buffer (the engine uses it for
 * effect tiles), anything else RGBA8. */
int xw_gl_named_target(XWGl *g, const char *name, int w, int h,
                       const char *format, const char *uvs,
                       unsigned int *tex) {
  int i;
  GLenum internal = GL_RGBA8;
  GLint wrap = GL_CLAMP_TO_EDGE;
  if (!g || !name || !name[0] || !tex)
    return -1;
  for (i = 0; i < g->ntargets; i++)
    if (strcmp(g->targets[i].name, name) == 0) {
      *tex = g->targets[i].tex;
      return 0;
    }
  if (g->ntargets >= XW_GL_TARGETS_MAX)
    return -1;
  if (w <= 0)
    w = g->w;
  if (h <= 0)
    h = g->h;
  if (w <= 0 || h <= 0)
    return -1;
  if (format && strcmp(format, "r8") == 0)
    internal = GL_R8;
  if (uvs && strcmp(uvs, "repeat") == 0)
    wrap = GL_REPEAT;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  i = g->ntargets;
  glGenTextures(1, &g->targets[i].tex);
  glBindTexture(GL_TEXTURE_2D, g->targets[i].tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, w, h, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glGenFramebuffers(1, &g->targets[i].fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g->targets[i].fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, g->targets[i].tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &g->targets[i].fbo);
    glDeleteTextures(1, &g->targets[i].tex);
    return -1;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  snprintf(g->targets[i].name, sizeof(g->targets[i].name), "%s", name);
  g->targets[i].w = w;
  g->targets[i].h = h;
  g->ntargets++;
  *tex = g->targets[i].tex;
  return 0;
}

/* Bind a named target (or the window when name is NULL/empty) for
 * rendering. Mirrors bind_target's flip handling. */
int xw_gl_bind_named(XWGl *g, const char *name) {
  int i;
  if (!g)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  if (!name || !name[0]) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->w, g->h);
    g->flip = 0;
    return 0;
  }
  for (i = 0; i < g->ntargets; i++)
    if (strcmp(g->targets[i].name, name) == 0) {
      glBindFramebuffer(GL_FRAMEBUFFER, g->targets[i].fbo);
      glViewport(0, 0, g->targets[i].w, g->targets[i].h);
      g->flip = 1;
      return 0;
    }
  return -1;
}

static GLint sampler_location(GLuint prog) {
  static const char *names[] = {"xw_tex0", "g_Texture0", "tex0", NULL};
  GLint loc;
  for (int i = 0; names[i]; i++) {
    loc = glGetUniformLocation(prog, names[i]);
    if (loc >= 0)
      return loc;
  }
  /* fallback: first active sampler */
  GLint count = 0;
  glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &count);
  for (int i = 0; i < count && i < 16; i++) {
    char name[64];
    GLsizei len = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(prog, (GLuint)i, sizeof(name) - 1, &len, &size,
                       &type, name);
    if (type == GL_SAMPLER_2D)
      return glGetUniformLocation(prog, name);
  }
  return -1;
}

static GLint time_location(GLuint prog) {
  static const char *names[] = {"xw_time", "g_Time", NULL};
  GLint loc;
  for (int i = 0; names[i]; i++) {
    loc = glGetUniformLocation(prog, names[i]);
    if (loc >= 0)
      return loc;
  }
  return -1;
}

static GLint alpha_location(GLuint prog) {
  return glGetUniformLocation(prog, "xw_alpha");
}

static int draw_full(XWGl *g, unsigned int prog, unsigned int tex, float time,
                     float alpha, int use_alpha) {
  GLint sloc, tloc, aloc;
  if (!g || !prog)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glUseProgram(prog);
  sloc = sampler_location(prog);
  if (sloc >= 0 && tex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sloc, 0);
  }
  tloc = time_location(prog);
  if (tloc >= 0)
    glUniform1f(tloc, time);
  if (use_alpha) {
    aloc = alpha_location(prog);
    if (aloc >= 0)
      glUniform1f(aloc, alpha);
  }
  aloc = glGetUniformLocation(prog, "xw_flip");
  if (aloc >= 0)
    glUniform1f(aloc, g->flip ? -1.0f : 1.0f);
  glBindVertexArray(g->quad_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  return 0;
}

int xw_gl_draw(XWGl *g, unsigned int prog, unsigned int tex, float time) {
  return draw_full(g, prog, tex, time, 1.0f, 0);
}

int xw_gl_draw_alpha(XWGl *g, unsigned int prog, unsigned int tex, float time,
                     float alpha) {
  return draw_full(g, prog, tex, time, alpha, 1);
}

int xw_gl_draw_points(XWGl *g, unsigned int prog, unsigned int tex,
                      const XWPoint *pts, int n) {
  GLint sloc;
  XWPoint *ndc = NULL;
  int i, rc = -1;
  if (!g || !prog || !pts || n <= 0 || g->w <= 0 || g->h <= 0)
    return -1;
  ndc = malloc((size_t)n * sizeof(XWPoint));
  if (!ndc)
    return -1;
  for (i = 0; i < n; i++) {
    ndc[i].x = pts[i].x / (float)g->w * 2.0f - 1.0f;
    ndc[i].y = 1.0f - pts[i].y / (float)g->h * 2.0f;
    ndc[i].size = pts[i].size;
    ndc[i].alpha = pts[i].alpha;
  }
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glUseProgram(prog);
  sloc = sampler_location(prog);
  if (sloc >= 0 && tex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sloc, 0);
  }
  sloc = glGetUniformLocation(prog, "xw_flip");
  if (sloc >= 0)
    glUniform1f(sloc, g->flip ? -1.0f : 1.0f);
  glBindVertexArray(g->points_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->points_vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * sizeof(XWPoint)), ndc,
               GL_DYNAMIC_DRAW);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glDrawArrays(GL_POINTS, 0, n);
  glDisable(GL_PROGRAM_POINT_SIZE);
  glBindVertexArray(0);
  glUseProgram(0);
  rc = 0;
  free(ndc);
  return rc;
}

int xw_gl_blend_begin(XWGl *g, XWBlend mode) {
  int sRGB, dRGB, sA, dA;
  if (!g)
    return -1;
  xw_blend_funcs(mode, &sRGB, &dRGB, &sA, &dA);
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glEnable(GL_BLEND);
  glBlendFuncSeparate((GLenum)sRGB, (GLenum)dRGB, (GLenum)sA, (GLenum)dA);
  return 0;
}

int xw_gl_blend_end(XWGl *g) {
  if (!g)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glDisable(GL_BLEND);
  return 0;
}

int xw_gl_upload_rgba(XWGl *g, const unsigned char *px, int w, int h,
                      unsigned int *tex, XWError *err) {
  GLuint t;
  if (!g || !px || !tex || w <= 0 || h <= 0 || w > 8192 || h > 8192)
    return gl_error("invalid args", err);
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               px);
  *tex = t;
  return 0;
}

int xw_gl_delete_texture(XWGl *g, unsigned int tex) {
  if (!g || !tex)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glDeleteTextures(1, &tex);
  return 0;
}

static int draw_rect_full(XWGl *g, unsigned int prog, unsigned int tex,
                          float time, float alpha, int use_alpha, int dx,
                          int dy, int dw, int dh) {
  float x0, x1, y0, y1, verts[16];
  GLint sloc, tloc, aloc;
  if (!g || !prog || dw <= 0 || dh <= 0 || g->w <= 0 || g->h <= 0)
    return -1;
  x0 = (float)dx / (float)g->w * 2.0f - 1.0f;
  x1 = (float)(dx + dw) / (float)g->w * 2.0f - 1.0f;
  y0 = 1.0f - (float)dy / (float)g->h * 2.0f;
  y1 = 1.0f - (float)(dy + dh) / (float)g->h * 2.0f;
  verts[0] = x0;
  verts[1] = y1;
  verts[2] = 0.0f;
  verts[3] = 1.0f;
  verts[4] = x1;
  verts[5] = y1;
  verts[6] = 1.0f;
  verts[7] = 1.0f;
  verts[8] = x0;
  verts[9] = y0;
  verts[10] = 0.0f;
  verts[11] = 0.0f;
  verts[12] = x1;
  verts[13] = y0;
  verts[14] = 1.0f;
  verts[15] = 0.0f;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glUseProgram(prog);
  sloc = sampler_location(prog);
  if (sloc >= 0 && tex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sloc, 0);
  }
  tloc = time_location(prog);
  if (tloc >= 0)
    glUniform1f(tloc, time);
  if (use_alpha) {
    aloc = alpha_location(prog);
    if (aloc >= 0)
      glUniform1f(aloc, alpha);
  }
  aloc = glGetUniformLocation(prog, "xw_flip");
  if (aloc >= 0)
    glUniform1f(aloc, g->flip ? -1.0f : 1.0f);
  glBindVertexArray(g->rect_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->rect_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  return 0;
}

int xw_gl_draw_rect(XWGl *g, unsigned int prog, unsigned int tex, float time,
                    int dx, int dy, int dw, int dh) {
  return draw_rect_full(g, prog, tex, time, 1.0f, 0, dx, dy, dw, dh);
}

int xw_gl_draw_rect_alpha(XWGl *g, unsigned int prog, unsigned int tex,
                          float time, float alpha, int dx, int dy, int dw,
                          int dh) {
  return draw_rect_full(g, prog, tex, time, alpha, 1, dx, dy, dw, dh);
}

int xw_gl_fbo_texture(XWGl *g, int idx, unsigned int *tex) {
  if (!g || !tex || idx < 0 || idx > 1)
    return -1;
  *tex = g->fbo_tex[idx];
  return 0;
}

int xw_gl_clear(XWGl *g, float r, float gg, float b) {
  if (!g)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glClearColor(r, gg, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  return 0;
}

int xw_gl_present(XWGl *g) {
  if (!g)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glXSwapBuffers(g->dpy, g->win);
  return 0;
}

#else /* !XW_HAVE_GL */

struct XWGl {
  int unused;
};

int xw_gl_create(XWGl **out, Display *dpy, Window win, int w, int h,
                 XWError *err) {
  (void)out;
  (void)dpy;
  (void)win;
  (void)w;
  (void)h;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

void xw_gl_destroy(XWGl *g) { (void)g; }

int xw_gl_upload(XWGl *g, const char *path, unsigned int *tex, XWError *err) {
  (void)g;
  (void)path;
  (void)tex;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

int xw_gl_program_we(XWGl *g, const char *frag_path,
                     const XWSceneCombo *combos, int n_combos,
                     unsigned int texture_slots, unsigned int *prog,
                     XWError *err) {
  (void)g;
  (void)frag_path;
  (void)combos;
  (void)n_combos;
  (void)texture_slots;
  (void)prog;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

int xw_gl_draw_we(XWGl *g, unsigned int prog, unsigned int input,
                  const float input_res[4], const XWWeSlot *slots,
                  const XWSceneConst *consts, int n_consts, float time) {
  (void)g;
  (void)prog;
  (void)input;
  (void)input_res;
  (void)slots;
  (void)consts;
  (void)n_consts;
  (void)time;
  return -1;
}

int xw_gl_named_target(XWGl *g, const char *name, int w, int h,
                       const char *format, const char *uvs,
                       unsigned int *tex) {
  (void)g;
  (void)name;
  (void)w;
  (void)h;
  (void)format;
  (void)uvs;
  (void)tex;
  return -1;
}

int xw_gl_bind_named(XWGl *g, const char *name) {
  (void)g;
  (void)name;
  return -1;
}

int xw_gl_texture_size(XWGl *g, unsigned int tex, int *tw, int *th, int *rw,
                       int *rh) {
  (void)g;
  (void)tex;
  (void)tw;
  (void)th;
  (void)rw;
  (void)rh;
  return -1;
}

int xw_gl_target_size(XWGl *g, int *w, int *h) {
  (void)g;
  (void)w;
  (void)h;
  return -1;
}

int xw_gl_util_texture(XWGl *g, const char *name, unsigned int *tex,
                       float res[4]) {
  (void)g;
  (void)name;
  (void)tex;
  (void)res;
  return -1;
}

int xw_gl_program_copy(XWGl *g, unsigned int *prog, XWError *err) {
  (void)g;
  (void)prog;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

int xw_gl_program_points(XWGl *g, unsigned int *prog, XWError *err) {
  (void)g;
  (void)prog;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

int xw_gl_program_composite(XWGl *g, unsigned int *prog, XWError *err) {
  (void)g;
  (void)prog;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

int xw_gl_draw_points(XWGl *g, unsigned int prog, unsigned int tex,
                      const XWPoint *pts, int n) {
  (void)g;
  (void)prog;
  (void)tex;
  (void)pts;
  (void)n;
  return -1;
}

int xw_gl_draw_alpha(XWGl *g, unsigned int prog, unsigned int tex, float time,
                     float alpha) {
  (void)g;
  (void)prog;
  (void)tex;
  (void)time;
  (void)alpha;
  return -1;
}

int xw_gl_blend_begin(XWGl *g, XWBlend mode) {
  (void)g;
  (void)mode;
  return -1;
}

int xw_gl_blend_end(XWGl *g) {
  (void)g;
  return -1;
}

int xw_gl_upload_rgba(XWGl *g, const unsigned char *px, int w, int h,
                      unsigned int *tex, XWError *err) {
  (void)g;
  (void)px;
  (void)w;
  (void)h;
  (void)tex;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
  return -1;
}

int xw_gl_delete_texture(XWGl *g, unsigned int tex) {
  (void)g;
  (void)tex;
  return -1;
}

int xw_gl_draw_rect(XWGl *g, unsigned int prog, unsigned int tex, float time,
                    int dx, int dy, int dw, int dh) {
  (void)g;
  (void)prog;
  (void)tex;
  (void)time;
  (void)dx;
  (void)dy;
  (void)dw;
  (void)dh;
  return -1;
}

int xw_gl_draw_rect_alpha(XWGl *g, unsigned int prog, unsigned int tex,
                          float time, float alpha, int dx, int dy, int dw,
                          int dh) {
  (void)g;
  (void)prog;
  (void)tex;
  (void)time;
  (void)alpha;
  (void)dx;
  (void)dy;
  (void)dw;
  (void)dh;
  return -1;
}

int xw_gl_bind_target(XWGl *g, int fbo) {
  (void)g;
  (void)fbo;
  return -1;
}

int xw_gl_draw(XWGl *g, unsigned int prog, unsigned int tex, float time) {
  (void)g;
  (void)prog;
  (void)tex;
  (void)time;
  return -1;
}

int xw_gl_fbo_texture(XWGl *g, int idx, unsigned int *tex) {
  (void)g;
  (void)idx;
  (void)tex;
  return -1;
}

int xw_gl_clear(XWGl *g, float r, float gg, float b) {
  (void)g;
  (void)r;
  (void)gg;
  (void)b;
  return -1;
}

int xw_gl_present(XWGl *g) {
  (void)g;
  return -1;
}

#endif /* XW_HAVE_GL */

#ifdef XW_HAVE_GL
static int parse_const_floats(const char *s, float *v) {
  int n = 0;
  if (!s)
    return 0;
  while (*s && n < 4) {
    char *e = NULL;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s)
      break;
    v[n] = strtof(s, &e);
    if (!e || e == s)
      break;
    n++;
    s = e;
  }
  return n;
}

static void bind_mvp(XWGl *g, GLuint prog) {
  static const char *names[] = {
    "g_ModelViewProjectionMatrix",
    "g_ModelViewProjectionMatrixInverse",
    "g_EffectModelViewProjectionMatrix",
    "g_ModelMatrix",
    "g_EffectModelMatrix",
    "g_ViewProjectionMatrix",
    NULL,
  };
  static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};
  static const float yflip[16] = {1, 0, 0, 0, 0, -1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};
  const float *m = (g && g->flip) ? yflip : ident;
  int i;
  for (i = 0; names[i]; i++) {
    GLint loc = glGetUniformLocation(prog, names[i]);
    if (loc >= 0)
      glUniformMatrix4fv(loc, 1, GL_FALSE, m);
  }
}

static void bind_prog_defaults(XWGl *g, GLuint prog,
                               const XWSceneConst *consts, int n_consts) {
  int p, i;
  for (p = 0; p < g->nprogs; p++) {
    if (g->progs[p].prog != prog)
      continue;
    for (i = 0; i < g->progs[p].ndefs; i++) {
      const XWShaderDef *d = &g->progs[p].defs[i];
      GLint loc = glGetUniformLocation(prog, d->name);
      float v[4];
      int c;
      if (loc < 0)
        continue;
      memcpy(v, d->v, sizeof(v));
      if (consts) {
        for (c = 0; c < n_consts; c++) {
          if (d->material[0] && strcmp(d->material, consts[c].key) == 0) {
            float pv[4];
            int pn = parse_const_floats(consts[c].value, pv);
            int k;
            if (pn == 1) {
              for (k = 0; k < 4; k++) v[k] = pv[0];
            } else if (pn > 1) {
              for (k = 0; k < pn && k < 4; k++) v[k] = pv[k];
            }
            break;
          }
        }
      }
      switch (d->kind) {
      case 1:
        glUniform1f(loc, v[0]);
        break;
      case 2:
        glUniform2f(loc, v[0], v[1]);
        break;
      case 3:
        glUniform3f(loc, v[0], v[1], v[2]);
        break;
      default:
        glUniform4f(loc, v[0], v[1], v[2], v[3]);
        break;
      }
    }
    return;
  }
}

int xw_gl_draw_we(XWGl *g, unsigned int prog, unsigned int input,
                  const float input_res[4], const XWWeSlot *slots,
                  const XWSceneConst *consts, int n_consts, float time) {
  char uname[48];
  int i;
  GLint loc;
  if (!g || !prog)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glUseProgram(prog);
  for (i = 0; i < XW_GL_WE_SLOTS; i++) {
    unsigned int t = input;
    const float *res = input_res;
    if (i == 0 && slots && slots[0].tex) {
      t = slots[0].tex;
      res = slots[0].res;
    } else if (i > 0 && slots && slots[i].tex) {
      t = slots[i].tex;
      res = slots[i].res;
    }
    snprintf(uname, sizeof(uname), "g_Texture%d", i);
    loc = glGetUniformLocation(prog, uname);
    if (loc >= 0 && t) {
      /* cached textures carry their WE flags (ClampUVs -> clamp, else
       * repeat); FBO inputs clamp, which is how we create them */
      GLint wrap = GL_CLAMP_TO_EDGE;
      int ci;
      for (ci = 0; ci < g->ntex; ci++)
        if (g->texcache[ci].tex == t) {
          wrap = (g->texcache[ci].flags & 2u) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
          break;
        }
      glActiveTexture((GLenum)(GL_TEXTURE0 + i));
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
      glUniform1i(loc, i);
    }
    snprintf(uname, sizeof(uname), "g_Texture%dResolution", i);
    loc = glGetUniformLocation(prog, uname);
    if (loc >= 0 && res)
      glUniform4f(loc, res[0], res[1], res[2], res[3]);
  }
  glActiveTexture(GL_TEXTURE0);
  bind_mvp(g, prog);
  loc = glGetUniformLocation(prog, "xw_flip");
  if (loc >= 0)
    glUniform1f(loc, g->flip ? -1.0f : 1.0f);
  loc = glGetUniformLocation(prog, "g_Time");
  if (loc >= 0)
    glUniform1f(loc, time);
  if (g->w > 0 && g->h > 0) {
    loc = glGetUniformLocation(prog, "g_TexelSize");
    if (loc >= 0)
      glUniform2f(loc, 1.0f / g->w, 1.0f / g->h);
    loc = glGetUniformLocation(prog, "g_TexelSizeHalf");
    if (loc >= 0)
      glUniform2f(loc, 0.5f / g->w, 0.5f / g->h);
  }
  bind_prog_defaults(g, prog, consts, n_consts);
  glBindVertexArray(g->quad_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  return 0;
}

int xw_gl_texture_size(XWGl *g, unsigned int tex, int *tw, int *th, int *rw,
                       int *rh) {
  int i;
  if (!g || !tex)
    return -1;
  for (i = 0; i < g->ntex; i++) {
    if (g->texcache[i].tex == tex) {
      if (tw)
        *tw = g->texcache[i].tw;
      if (th)
        *th = g->texcache[i].th;
      if (rw)
        *rw = g->texcache[i].rw;
      if (rh)
        *rh = g->texcache[i].rh;
      return 0;
    }
  }
  /* offscreen targets are not in the path cache */
  for (i = 0; i < g->ntargets; i++) {
    if (g->targets[i].tex == tex) {
      if (tw)
        *tw = g->targets[i].w;
      if (th)
        *th = g->targets[i].h;
      if (rw)
        *rw = g->targets[i].w;
      if (rh)
        *rh = g->targets[i].h;
      return 0;
    }
  }
  return -1;
}

int xw_gl_target_size(XWGl *g, int *w, int *h) {
  if (!g)
    return -1;
  if (w)
    *w = g->w;
  if (h)
    *h = g->h;
  return 0;
}

static int make_solid(XWGl *g, unsigned char r, unsigned char gg,
                      unsigned char b, unsigned char a, GLuint *t) {
  unsigned char px[4] = {r, gg, b, a};
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glGenTextures(1, t);
  glBindTexture(GL_TEXTURE_2D, *t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               px);
  return 0;
}

int xw_gl_util_texture(XWGl *g, const char *name, unsigned int *tex,
                       float res[4]) {
  /* 0 black, 1 white, 2 noflow (neutral flowmask), 3 perlin noise */
  int idx = -1;
  if (!g || !name || !tex)
    return -1;
  if (strcmp(name, "util/black") == 0)
    idx = 0;
  else if (strcmp(name, "util/white") == 0)
    idx = 1;
  else if (strcmp(name, "util/noflow") == 0)
    idx = 2;
  else if (strcmp(name, "util/perlin_256") == 0)
    idx = 3;
  else
    return -1;
  if (!g->utils[idx]) {
    if (idx == 3) {
      unsigned char *px;
      int x, y;
      unsigned int seed = 0x12345678u;
      px = malloc(256 * 256 * 4);
      if (!px)
        return -1;
      for (y = 0; y < 256; y++) {
        for (x = 0; x < 256; x++) {
          unsigned char *o = px + ((size_t)y * 256 + x) * 4;
          seed = seed * 1664525u + 1013904223u;
          o[0] = (unsigned char)(seed >> 24);
          seed = seed * 1664525u + 1013904223u;
          o[1] = (unsigned char)(seed >> 24);
          seed = seed * 1664525u + 1013904223u;
          o[2] = (unsigned char)(seed >> 24);
          o[3] = 255;
        }
      }
      glXMakeCurrent(g->dpy, g->win, g->ctx);
      glGenTextures(1, &g->utils[idx]);
      glBindTexture(GL_TEXTURE_2D, g->utils[idx]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, px);
      free(px);
    } else if (idx == 0) {
      make_solid(g, 0, 0, 0, 255, &g->utils[idx]);
    } else if (idx == 1) {
      make_solid(g, 255, 255, 255, 255, &g->utils[idx]);
    } else {
      make_solid(g, 127, 127, 0, 255, &g->utils[idx]);
    }
  }
  *tex = g->utils[idx];
  if (res) {
    if (idx == 3) {
      res[0] = 256;
      res[1] = 256;
      res[2] = 256;
      res[3] = 256;
    } else {
      res[0] = res[1] = res[2] = res[3] = 1;
    }
  }
  return 0;
}
#endif
