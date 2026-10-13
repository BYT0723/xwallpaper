#include <stdlib.h>
#include <string.h>

#include "scene_glsl.h"

#ifdef XW_HAVE_GLSLANG

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_cross/spirv_cross_c.h>

int xw_glsl_available(void) { return 1; }

/* glslang keeps process-global state; initialise it once. */
static int ensure_glslang(void) {
  static int ready = 0;
  static int tried = 0;
  if (ready)
    return 1;
  if (tried)
    return 0;
  tried = 1;
  /* glslang_initialize_process returns non-zero on success (it forwards
   * ShInitialize's bool) */
  if (glslang_initialize_process() != 0)
    ready = 1;
  return ready;
}

static int to_spirv(glslang_stage_t stage, const char *src, unsigned **words,
                    size_t *nwords, XWError *err) {
  glslang_input_t in;
  glslang_shader_t *sh;
  glslang_program_t *pr;
  memset(&in, 0, sizeof(in));
  in.language = GLSLANG_SOURCE_GLSL;
  in.stage = stage;
  in.client = GLSLANG_CLIENT_OPENGL;
  in.client_version = GLSLANG_TARGET_OPENGL_450;
  in.target_language = GLSLANG_TARGET_SPV;
  in.target_language_version = GLSLANG_TARGET_SPV_1_5;
  in.code = src;
  in.default_version = 330;
  in.default_profile = GLSLANG_NO_PROFILE;
  in.messages = GLSLANG_MSG_DEFAULT_BIT;
  in.resource = glslang_default_resource();
  sh = glslang_shader_create(&in);
  if (!sh) {
    XW_ERROR_SET(err, 1, "glslang: cannot create shader");
    return -1;
  }
  /* SPIR-V needs explicit locations/bindings; auto-map them (the engine
   * does the same) after we pinned the vertex attributes ourselves */
  glslang_shader_set_options(sh, GLSLANG_SHADER_AUTO_MAP_BINDINGS |
                                     GLSLANG_SHADER_AUTO_MAP_LOCATIONS);
  if (!glslang_shader_preprocess(sh, &in) || !glslang_shader_parse(sh, &in)) {
    XW_ERROR_SET(err, 1, "glslang: %.200s", glslang_shader_get_info_log(sh));
    glslang_shader_delete(sh);
    return -1;
  }
  pr = glslang_program_create();
  if (!pr) {
    glslang_shader_delete(sh);
    XW_ERROR_SET(err, 1, "glslang: cannot create program");
    return -1;
  }
  glslang_program_add_shader(pr, sh);
  if (!glslang_program_link(pr, GLSLANG_MSG_DEFAULT_BIT)) {
    XW_ERROR_SET(err, 1, "glslang link: %.200s",
                 glslang_program_get_info_log(pr));
    glslang_program_delete(pr);
    glslang_shader_delete(sh);
    return -1;
  }
  glslang_program_map_io(pr);
  glslang_program_SPIRV_generate(pr, stage);
  *nwords = glslang_program_SPIRV_get_size(pr);
  *words = malloc(*nwords * sizeof(unsigned));
  if (!*words) {
    glslang_program_delete(pr);
    glslang_shader_delete(sh);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  memcpy(*words, glslang_program_SPIRV_get_ptr(pr), *nwords * sizeof(unsigned));
  glslang_program_delete(pr);
  glslang_shader_delete(sh);
  return 0;
}

static int to_glsl(const unsigned *words, size_t nwords, char **out,
                   XWError *err) {
  spvc_context ctx;
  spvc_parsed_ir ir;
  spvc_compiler comp;
  spvc_compiler_options opts;
  const char *src = NULL;
  size_t len;
  char *copy;
  if (spvc_context_create(&ctx) != SPVC_SUCCESS) {
    XW_ERROR_SET(err, 1, "spirv-cross: cannot create context");
    return -1;
  }
  if (spvc_context_parse_spirv(ctx, words, nwords, &ir) != SPVC_SUCCESS) {
    XW_ERROR_SET(err, 1, "spirv-cross: %.200s",
                 spvc_context_get_last_error_string(ctx));
    spvc_context_destroy(ctx);
    return -1;
  }
  if (spvc_context_create_compiler(ctx, SPVC_BACKEND_GLSL, ir,
                                   SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                                   &comp) != SPVC_SUCCESS) {
    XW_ERROR_SET(err, 1, "spirv-cross: cannot create compiler");
    spvc_context_destroy(ctx);
    return -1;
  }
  if (spvc_compiler_create_compiler_options(comp, &opts) == SPVC_SUCCESS) {
    spvc_compiler_options_set_uint(opts, SPVC_COMPILER_OPTION_GLSL_VERSION,
                                   330);
    spvc_compiler_install_compiler_options(comp, opts);
  }
  if (spvc_compiler_compile(comp, &src) != SPVC_SUCCESS) {
    XW_ERROR_SET(err, 1, "spirv-cross: %.200s",
                 spvc_context_get_last_error_string(ctx));
    spvc_context_destroy(ctx);
    return -1;
  }
  len = strlen(src);
  copy = malloc(len + 1);
  if (copy)
    memcpy(copy, src, len + 1);
  spvc_context_destroy(ctx);
  if (!copy) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  *out = copy;
  return 0;
}

int xw_glsl_roundtrip(const char *vert_src, const char *frag_src,
                      char **vert_out, char **frag_out, XWError *err) {
  unsigned *vw = NULL, *fw = NULL;
  size_t vn = 0, fn = 0;
  int rc = -1;
  if (!vert_src || !frag_src || !vert_out || !frag_out)
    return -1;
  *vert_out = NULL;
  *frag_out = NULL;
  if (!ensure_glslang()) {
    XW_ERROR_SET(err, 1, "glslang: cannot initialise");
    return -1;
  }
  if (to_spirv(GLSLANG_STAGE_VERTEX, vert_src, &vw, &vn, err) != 0)
    goto done;
  if (to_spirv(GLSLANG_STAGE_FRAGMENT, frag_src, &fw, &fn, err) != 0)
    goto done;
  if (to_glsl(vw, vn, vert_out, err) != 0)
    goto done;
  if (to_glsl(fw, fn, frag_out, err) != 0) {
    free(*vert_out);
    *vert_out = NULL;
    goto done;
  }
  rc = 0;
done:
  free(vw);
  free(fw);
  return rc;
}

#else /* !XW_HAVE_GLSLANG */

int xw_glsl_available(void) { return 0; }

int xw_glsl_roundtrip(const char *vert_src, const char *frag_src,
                      char **vert_out, char **frag_out, XWError *err) {
  (void)vert_src;
  (void)frag_src;
  (void)vert_out;
  (void)frag_out;
  (void)err;
  return -1;
}

#endif
