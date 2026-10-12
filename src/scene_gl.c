#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XW_HAVE_GL
#include <GL/glew.h>
#include <GL/glx.h>
#endif

#include <Imlib2.h>

#include "scene_gl.h"
#include "scene_shader.h"

#define XW_GL_PROG_MAX 16
#define XW_GL_TEXCACHE_MAX 8

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
  } progs[XW_GL_PROG_MAX];
  int nprogs;
  struct {
    char path[1024];
    GLuint tex;
  } texcache[XW_GL_TEXCACHE_MAX];
  int ntex;
};

static const char *vert_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 uv;\n"
    "out vec2 v_TexCoord;\n"
    "out vec2 vUv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  v_TexCoord = uv;\n"
    "  vUv = uv;\n"
    "  v_uv = uv;\n"
    "  gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *copy_frag_src =
    "#version 330 core\n"
    "in vec2 v_TexCoord;\n"
    "uniform sampler2D xw_tex0;\n"
    "out vec4 xw_out;\n"
    "void main() { xw_out = texture(xw_tex0, v_TexCoord); }\n";

static const char *points_vert_src =
    "#version 330 core\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 ps;\n"
    "out float v_alpha;\n"
    "void main() {\n"
    "  v_alpha = ps.y;\n"
    "  gl_PointSize = ps.x;\n"
    "  gl_Position = vec4(pos, 0.0, 1.0);\n"
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

int xw_gl_upload(XWGl *g, const char *path, unsigned int *tex, XWError *err) {
  Imlib_Context ictx;
  Imlib_Image img;
  int iw, ih, x, y;
  DATA32 *data;
  unsigned char *px;
  GLuint t;
  int i;
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
  ictx = imlib_context_new();
  if (!ictx)
    return gl_error("imlib_context_new failed", err);
  imlib_context_push(ictx);
  img = imlib_load_image_immediately_without_cache(path);
  if (!img) {
    imlib_context_pop();
    imlib_context_free(ictx);
    if (g->ntex < XW_GL_TEXCACHE_MAX) {
      snprintf(g->texcache[g->ntex].path, sizeof(g->texcache[g->ntex].path),
               "%s", path);
      g->texcache[g->ntex].tex = 0;
      g->ntex++;
    }
    XW_ERROR_SET(err, 1, "cannot load texture '%s'", path);
    return -1;
  }
  imlib_context_set_image(img);
  iw = imlib_image_get_width();
  ih = imlib_image_get_height();
  if (iw <= 0 || ih <= 0 || iw > 8192 || ih > 8192) {
    imlib_free_image();
    imlib_context_pop();
    imlib_context_free(ictx);
    XW_ERROR_SET(err, 1, "bad texture size '%s'", path);
    return -1;
  }
  data = imlib_image_get_data_for_reading_only();
  px = malloc((size_t)iw * ih * 4);
  if (!px) {
    imlib_free_image();
    imlib_context_pop();
    imlib_context_free(ictx);
    return gl_error("out of memory", err);
  }
  for (y = 0; y < ih; y++)
    for (x = 0; x < iw; x++) {
      DATA32 d = data[y * iw + x];
      unsigned char *o = px + ((size_t)y * iw + x) * 4;
      o[0] = (unsigned char)((d >> 16) & 0xff);
      o[1] = (unsigned char)((d >> 8) & 0xff);
      o[2] = (unsigned char)(d & 0xff);
      o[3] = (unsigned char)((d >> 24) & 0xff);
    }
  imlib_free_image();
  imlib_context_pop();
  imlib_context_free(ictx);
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, iw, ih, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, px);
  free(px);
  if (g->ntex < XW_GL_TEXCACHE_MAX) {
    snprintf(g->texcache[g->ntex].path, sizeof(g->texcache[g->ntex].path),
             "%s", path);
    g->texcache[g->ntex].tex = t;
    g->ntex++;
  } else {
    glDeleteTextures(1, &t);
    XW_ERROR_SET(err, 1, "texture cache full");
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

int xw_gl_program_we(XWGl *g, const char *frag_path, unsigned int *prog,
                     XWError *err) {
  char *src = NULL, *dst = NULL;
  size_t cap;
  int rc;
  if (!g || !frag_path || !prog)
    return gl_error("invalid args", err);
  for (int i = 0; i < g->nprogs; i++)
    if (strcmp(g->progs[i].key, frag_path) == 0) {
      *prog = g->progs[i].prog;
      return 0;
    }
  if (slurp(frag_path, &src, err) != 0)
    return -1;
  cap = strlen(src) + 4096;
  dst = malloc(cap);
  if (!dst) {
    free(src);
    return gl_error("out of memory", err);
  }
  if (xw_shader_translate(src, dst, cap) != 0) {
    free(src);
    free(dst);
    XW_ERROR_SET(err, 1, "cannot translate shader '%s'", frag_path);
    return -1;
  }
  free(src);
  rc = link_program(frag_path, vert_src, dst, g, prog, err);
  free(dst);
  return rc;
}

int xw_gl_program_copy(XWGl *g, unsigned int *prog, XWError *err) {
  if (!g || !prog)
    return gl_error("invalid args", err);
  return link_program("xw_copy", vert_src, copy_frag_src, g, prog, err);
}

int xw_gl_program_points(XWGl *g, unsigned int *prog, XWError *err) {
  if (!g || !prog)
    return gl_error("invalid args", err);
  return link_program("xw_points", points_vert_src, points_frag_src, g, prog,
                      err);
}

int xw_gl_bind_target(XWGl *g, int fbo) {
  if (!g)
    return -1;
  glXMakeCurrent(g->dpy, g->win, g->ctx);
  if (fbo < 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->w, g->h);
  } else if (fbo < 2) {
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbos[fbo]);
    glViewport(0, 0, g->w, g->h);
  } else {
    return -1;
  }
  return 0;
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

int xw_gl_draw(XWGl *g, unsigned int prog, unsigned int tex, float time) {
  GLint sloc, tloc;
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
  glBindVertexArray(g->quad_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  return 0;
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

int xw_gl_draw_rect(XWGl *g, unsigned int prog, unsigned int tex, float time,
                    int dx, int dy, int dw, int dh) {
  float x0, x1, y0, y1, verts[16];
  GLint sloc, tloc;
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
  glBindVertexArray(g->rect_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g->rect_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  return 0;
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

int xw_gl_program_we(XWGl *g, const char *frag_path, unsigned int *prog,
                     XWError *err) {
  (void)g;
  (void)frag_path;
  (void)prog;
  XW_ERROR_SET(err, 1, "scene GL support not compiled in (need gl/glew)");
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

int xw_gl_draw_points(XWGl *g, unsigned int prog, unsigned int tex,
                      const XWPoint *pts, int n) {
  (void)g;
  (void)prog;
  (void)tex;
  (void)pts;
  (void)n;
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
