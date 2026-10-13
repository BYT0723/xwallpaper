#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "scene_shader.h"

/* ------------------------------------------------------------------ */
/* Built-in engine headers                                             */
/*                                                                     */
/* Linux-wallpaperengine ships its own copies of these under a GPL and */
/* needs the user's Steam install for the originals; we reimplement    */
/* them from the public header documentation instead.                  */
/*   https://docs.wallpaperengine.io/en/scene/shader/headers.html      */
/* Constants are documented exactly; function bodies are the standard  */
/* formulations (the docs only publish signatures) and are flagged     */
/* where a reference frame would be needed to verify them bit-exactly. */
/* ------------------------------------------------------------------ */

static const char *hdr_common =
    "// engine built-in common.h\n"
    "#ifndef XW_WE_COMMON_H\n"
    "#define XW_WE_COMMON_H\n"
    "#ifndef M_PI\n"
    "#define M_PI 3.14159265359\n"
    "#endif\n"
    "#ifndef M_PI_HALF\n"
    "#define M_PI_HALF 1.57079632679\n"
    "#endif\n"
    "#ifndef M_PI_2\n"
    "#define M_PI_2 6.28318530718\n"
    "#endif\n"
    "#ifndef SQRT_2\n"
    "#define SQRT_2 1.41421356237\n"
    "#endif\n"
    "#ifndef SQRT_3\n"
    "#define SQRT_3 1.73205080756\n"
    "#endif\n"
    "vec3 hsv2rgb(vec3 c) {\n"
    "  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
    "  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
    "  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
    "}\n"
    "vec3 rgb2hsv(vec3 c) {\n"
    "  vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
    "  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));\n"
    "  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));\n"
    "  float d = q.x - min(q.w, q.y);\n"
    "  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1.0e-10)), d / (q.x + 1.0e-10), q.x);\n"
    "}\n"
    "vec2 rotateVec2(vec2 v, float r) {\n"
    "  float c = cos(r), s = sin(r);\n"
    "  return vec2(c * v.x - s * v.y, s * v.x + c * v.y);\n"
    "}\n"
    "float greyscale(vec3 color) {\n"
    "  return dot(color, vec3(0.299, 0.587, 0.114));\n"
    "}\n"
    "#endif\n";

static const char *hdr_common_fragment =
    "// engine built-in common_fragment.h\n"
    "#ifndef XW_WE_COMMON_FRAGMENT_H\n"
    "#define XW_WE_COMMON_FRAGMENT_H\n"
    "vec3 DecompressNormal(vec4 n) {\n"
    "  vec3 t = n.xyz * 2.0 - 1.0;\n"
    "  t.z = sqrt(max(0.0, 1.0 - dot(t.xy, t.xy)));\n"
    "  return normalize(t);\n"
    "}\n"
    "vec4 DecompressNormalWithMask(vec4 n) {\n"
    "  vec3 t = n.xyz * 2.0 - 1.0;\n"
    "  t.z = sqrt(max(0.0, 1.0 - dot(t.xy, t.xy)));\n"
    "  return vec4(normalize(t), n.w);\n"
    "}\n"
    "float ConvertSampleR8(vec4 sample) {\n"
    "  return sample.r;\n"
    "}\n"
    "vec4 ConvertTexture0Format(vec4 color) {\n"
    "  return color;\n"
    "}\n"
    "#endif\n";

static const char *hdr_common_vertex =
    "// engine built-in common_vertex.h\n"
    "#ifndef XW_WE_COMMON_VERTEX_H\n"
    "#define XW_WE_COMMON_VERTEX_H\n"
    "mat3 BuildTangentSpace(vec3 normal, vec4 signedTangent) {\n"
    "  vec3 n = normalize(normal);\n"
    "  vec3 t = normalize(signedTangent.xyz - n * dot(n, signedTangent.xyz));\n"
    "  return mat3(t, cross(n, t) * signedTangent.w, n);\n"
    "}\n"
    "mat3 BuildTangentSpace(mat3 modelTransform, vec3 normal, vec4 signedTangent) {\n"
    "  return modelTransform * BuildTangentSpace(normal, signedTangent);\n"
    "}\n"
    "#endif\n";

static const char *hdr_common_blending =
    "// engine built-in common_blending.h\n"
    "#ifndef XW_WE_COMMON_BLENDING_H\n"
    "#define XW_WE_COMMON_BLENDING_H\n"
    "// Fallback for the engine's common_blending.h (used only without\n"
    "// XW_ASSETS_DIR). Mode numbers match the engine's table; the real\n"
    "// header is a compile-time #if BLENDMODE == N chain.\n"
    "vec3 ApplyBlending(int mode, vec3 A, vec3 B, float opacity) {\n"
    "  vec3 r;\n"
    "  if (mode == 1) r = min(A, B);\n"
    "  else if (mode == 2) r = A * B;\n"
    "  else if (mode == 4 || mode == 20) r = max(A - B, vec3(0.0));\n"
    "  else if (mode == 5) r = min(A, B);\n"
    "  else if (mode == 6 || mode == 10) r = max(A, B);\n"
    "  else if (mode == 7) r = 1.0 - (1.0 - A) * (1.0 - B);\n"
    "  else if (mode == 8) r = min(vec3(1.0), A / max(1.0 - B, vec3(1e-4)));\n"
    "  else if (mode == 9) r = min(A + B, vec3(1.0));\n"
    "  else if (mode == 11) r = mix(2.0 * A * B,\n"
    "                               1.0 - 2.0 * (1.0 - A) * (1.0 - B),\n"
    "                               step(0.5, A));\n"
    "  else if (mode == 13) r = mix(2.0 * A * B,\n"
    "                               1.0 - 2.0 * (1.0 - A) * (1.0 - B),\n"
    "                               step(0.5, B));\n"
    "  else if (mode == 18) r = abs(A - B);\n"
    "  else if (mode == 19) r = A + B - 2.0 * A * B;\n"
    "  else if (mode == 31) return A + B * opacity;\n"
    "  else if (mode == 32) return mix(A, A + A * B, opacity);\n"
    "  else return mix(A, B, clamp(opacity, 0.0, 1.0));\n"
    "  return mix(A, r, clamp(opacity, 0.0, 1.0));\n"
    "}\n"
    "#endif\n";

static char g_assets_dir[1024];

void xw_shader_set_assets_dir(const char *dir) {
  snprintf(g_assets_dir, sizeof(g_assets_dir), "%s", dir ? dir : "");
}

const char *xw_shader_assets_dir(void) { return g_assets_dir; }

/* The engine's asset directory is accepted only when it really carries the
 * canonical header, so a stray match elsewhere on disk is ignored. */
static int is_engine_assets(const char *dir) {
  char path[1200];
  FILE *f;
  snprintf(path, sizeof(path), "%.900s/shaders/common.h", dir);
  f = fopen(path, "rb");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

static int probe_library(const char *lib, char *out, size_t n) {
  char path[1200];
  snprintf(path, sizeof(path),
           "%.900s/steamapps/common/wallpaper_engine/assets", lib);
  if (!is_engine_assets(path))
    return 0;
  snprintf(out, n, "%s", path);
  return 1;
}

/* Pull one "quoted" token out of a vdf line; returns 0 when absent. */
static int vdf_quoted(const char **pp, char *out, size_t n) {
  const char *p = strchr(*pp, '"');
  size_t i = 0;
  if (!p)
    return 0;
  for (p++; *p && *p != '"'; p++)
    if (i + 1 < n)
      out[i++] = *p;
  if (*p != '"')
    return 0;
  out[i] = '\0';
  *pp = p + 1;
  return 1;
}

static int vdf_all_digits(const char *s) {
  if (!s[0])
    return 0;
  for (; *s; s++)
    if (*s < '0' || *s > '9')
      return 0;
  return 1;
}

/* Append the library roots found in a libraryfolders.vdf: the current format
 * nests "path" "<dir>" entries, older ones use "N" "<dir>". */
static int vdf_libraries(const char *vdf_path, char (*cands)[1024], int max,
                         int n) {
  FILE *f = fopen(vdf_path, "rb");
  char line[1024];
  if (!f)
    return n;
  while (n < max && fgets(line, sizeof(line), f)) {
    const char *p = line;
    char key[128], val[1024];
    if (!vdf_quoted(&p, key, sizeof(key)) ||
        !vdf_quoted(&p, val, sizeof(val)))
      continue;
    if (strcmp(key, "path") != 0 && !vdf_all_digits(key))
      continue;
    if (val[0] != '/')
      continue;
    snprintf(cands[n], 1024, "%s", val);
    n++;
  }
  fclose(f);
  return n;
}

int xw_shader_resolve_assets_dir(const char *env, const char *home, char *out,
                                 size_t n) {
  /* Steam roots directly under the home directory */
  static const char *const roots[] = {
      ".local/share/Steam",
      ".steam/steam",
      ".steam/root",
      ".var/app/com.valvesoftware.Steam/.local/share/Steam",
      ".snap/steam/common/.local/share/Steam",
  };
  char cands[32][1024];
  char vdf[1200];
  int nroots = (int)(sizeof(roots) / sizeof(roots[0])), i, nc = 0;

  if (!out || n == 0)
    return -1;
  if (env && env[0]) {
    snprintf(out, n, "%s", env);
    return 0;
  }
  if (!home || !home[0])
    return -1;
  for (i = 0; i < nroots && nc < 32; i++) {
    snprintf(cands[nc], sizeof(cands[0]), "%.400s/%.300s", home, roots[i]);
    nc++;
  }
  /* libraries that live elsewhere get listed in the root's vdf */
  for (i = 0; i < nroots && nc < 32; i++) {
    snprintf(vdf, sizeof(vdf), "%.400s/%.300s/steamapps/libraryfolders.vdf",
             home, roots[i]);
    nc = vdf_libraries(vdf, cands, 32, nc);
  }
  for (i = 0; i < nc; i++)
    if (probe_library(cands[i], out, n))
      return 0;
  return -1;
}

/* Read a header from <assets>/shaders/<name>; caller frees. */
static char *assets_header(const char *name) {
  char path[1152];
  FILE *f;
  long sz;
  char *b;
  if (!g_assets_dir[0] || !name || !name[0] || strstr(name, ".."))
    return NULL;
  snprintf(path, sizeof(path), "%.900s/shaders/%.150s", g_assets_dir, name);
  f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 256 * 1024) {
    fclose(f);
    return NULL;
  }
  b = malloc((size_t)sz + 1);
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
  b[sz] = '\0';
  return b;
}

int xw_shader_assets_shader(const char *name, char *out, size_t n) {
  static const char *exts[] = {"", ".frag"};
  struct stat st;
  size_t i;
  if (!g_assets_dir[0] || !name || !name[0] || !out || n == 0)
    return -1;
  if (strstr(name, ".."))
    return -1;
  for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
    char path[1152];
    snprintf(path, sizeof(path), "%.900s/shaders/%.150s%s", g_assets_dir, name,
             exts[i]);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
      snprintf(out, n, "%.1000s", path);
      return 0;
    }
  }
  return -1;
}

const char *xw_shader_builtin_header(const char *name) {
  if (!name)
    return NULL;
  if (strcmp(name, "common.h") == 0)
    return hdr_common;
  if (strcmp(name, "common_fragment.h") == 0)
    return hdr_common_fragment;
  if (strcmp(name, "common_vertex.h") == 0)
    return hdr_common_vertex;
  if (strcmp(name, "common_blending.h") == 0)
    return hdr_common_blending;
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Buffer helpers                                                      */
/* ------------------------------------------------------------------ */

static int append(char **buf, size_t *len, size_t *cap, const char *s,
                  size_t n) {
  if (*len + n + 1 > *cap) {
    size_t ncap = (*cap ? *cap * 2 : 256);
    while (ncap < *len + n + 1) ncap *= 2;
    char *nb = realloc(*buf, ncap);
    if (!nb) return -1;
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
  return 0;
}

static int append_cstr(char **buf, size_t *len, size_t *cap, const char *s) {
  return append(buf, len, cap, s, strlen(s));
}

/* Dialect + stage prefix. Mirrors the engine's documented compatibility
 * defines: HLSL spellings used by workshop shaders, mapped to GLSL. */
static const char *dialect_defines =
    "#define mul(x, y) ((y) * (x))\n"
    "#define lerp mix\n"
    "#define frac fract\n"
    "#define CAST2(x) (vec2(x))\n"
    "#define CAST3(x) (vec3(x))\n"
    "#define CAST4(x) (vec4(x))\n"
    "#define CAST3X3(x) (mat3(x))\n"
    "#define float2 vec2\n"
    "#define float3 vec3\n"
    "#define float4 vec4\n"
    "#define int2 ivec2\n"
    "#define int3 ivec3\n"
    "#define int4 ivec4\n"
    "#define saturate(x) (clamp(x, 0.0, 1.0))\n"
    "#define texSample2D texture\n"
    "#define texSample2DLod textureLod\n"
    "#define texture2D texture\n"
    "#define texture2DLod textureLod\n"
    "#define log10(x) (log2(x) * 0.301029995663981)\n"
    "#define atan2 atan\n"
    "#define fmod(x, y) ((x) - (y) * trunc((x) / (y)))\n"
    "#define ddx dFdx\n"
    "#define ddy(x) dFdy(-(x))\n"
    "#define GLSL 1\n";

static const char *fragment_defines =
    "out vec4 out_FragColor;\n"
    "#define gl_FragColor out_FragColor\n"
    "#define varying in\n";

static const char *vertex_defines =
    "#define attribute in\n"
    "#define varying out\n";

/* Combo annotations: // [COMBO] {"combo":"NAME",...,"default":N}
 * An annotation default is only emitted when the material did not already
 * provide that combo (explicit material combos win). */
static int emit_combos(char **tmp, size_t *len, size_t *cap, const char *src,
                       const XWSceneCombo *combos, int n_combos) {
  const char *q = src;
  char seen[64][64];
  int n_seen = 0;
  while ((q = strstr(q, "// [COMBO")) != NULL) {
    const char *eol = strchr(q, '\n');
    size_t llen = eol ? (size_t)(eol - q) : strlen(q);
    const char *ck = strstr(q, "\"combo\"");
    if (ck && ck < q + llen) {
      const char *colon = strchr(ck, ':');
      const char *q1, *q2;
      char name[64];
      size_t nl;
      if (colon && colon < q + llen && (q1 = strchr(colon, '"')) != NULL &&
          q1 < q + llen && (q2 = strchr(q1 + 1, '"')) != NULL &&
          q2 < q + llen && (nl = (size_t)(q2 - q1 - 1)) < sizeof(name)) {
        const char *dk;
        int i, skip = 0;
        memcpy(name, q1 + 1, nl);
        name[nl] = '\0';
        for (i = 0; i < n_combos; i++)
          if (strcmp(combos[i].key, name) == 0)
            skip = 1;
        for (i = 0; i < n_seen; i++)
          if (strcmp(seen[i], name) == 0)
            skip = 1;
        dk = strstr(q, "\"default\"");
        if (!skip && dk && dk < q + llen) {
          const char *dc = strchr(dk, ':');
          if (dc && dc < q + llen) {
            const char *ds = dc + 1;
            char *end = NULL;
            long dv;
            while (ds < q + llen && (*ds == ' ' || *ds == '\t')) ds++;
            /* quoted defaults are paths/colours, not combo values */
            if (ds < q + llen && *ds != '"') {
              dv = strtol(ds, &end, 10);
              if (end != ds) {
                char def[128];
                int dl = snprintf(def, sizeof(def), "#define %s %ld\n", name,
                                  dv);
                if (dl > 0 && append(tmp, len, cap, def, (size_t)dl) != 0)
                  return -1;
                if (n_seen < 64)
                  snprintf(seen[n_seen++], sizeof(seen[0]), "%s", name);
              }
            }
          }
        }
      }
    }
    q += llen ? llen : 1;
  }
  return 0;
}

/* Sampler annotations carry a `combo` (e.g. the opacity mask uses MASK).
 * The engine defines that combo as 1 when the texture is actually bound, so
 * shaders that guard the mask with #if MASK turn it on. */
static int emit_sampler_combos(char **tmp, size_t *len, size_t *cap,
                               const char *src, unsigned int texture_slots,
                               const XWSceneCombo *combos, int n_combos) {
  const char *p = src;
  char seen[32][32];
  int n_seen = 0;
  while ((p = strstr(p, "uniform")) != NULL) {
    const char *eol = strchr(p, '\n');
    const char *end = eol ? eol : p + strlen(p);
    const char *name = strstr(p, "sampler");
    const char *cmt = strstr(p, "//");
    char combo[32];
    int slot = -1, i, skip = 0;
    if (!name || name >= end || !cmt || cmt >= end) {
      p = end;
      continue;
    }
    /* g_Texture<N> gives the slot */
    {
      const char *g = strstr(p, "g_Texture");
      if (!g || g >= end) {
        p = end;
        continue;
      }
      g += 9;
      if (*g < '0' || *g > '9') {
        p = end;
        continue;
      }
      slot = *g - '0';
    }
    {
      const char *ck = strstr(cmt, "\"combo\"");
      const char *colon, *q1, *q2;
      size_t nl;
      if (!ck || ck >= end) {
        p = end;
        continue;
      }
      colon = strchr(ck, ':');
      if (!colon || colon >= end || (q1 = strchr(colon, '"')) == NULL ||
          q1 >= end || (q2 = strchr(q1 + 1, '"')) == NULL || q2 >= end) {
        p = end;
        continue;
      }
      nl = (size_t)(q2 - q1 - 1);
      if (nl == 0 || nl >= sizeof(combo)) {
        p = end;
        continue;
      }
      memcpy(combo, q1 + 1, nl);
      combo[nl] = '\0';
    }
    /* only when the slot is bound and the combo is not already defined */
    if (!((texture_slots >> slot) & 1u)) {
      p = end;
      continue;
    }
    for (i = 0; i < n_combos; i++)
      if (strcmp(combos[i].key, combo) == 0)
        skip = 1;
    for (i = 0; i < n_seen; i++)
      if (strcmp(seen[i], combo) == 0)
        skip = 1;
    if (!skip) {
      char def[96];
      int dl = snprintf(def, sizeof(def), "#define %s 1\n", combo);
      if (dl > 0 && append(tmp, len, cap, def, (size_t)dl) != 0)
        return -1;
      if (n_seen < 32)
        snprintf(seen[n_seen++], sizeof(seen[0]), "%s", combo);
    }
    p = end;
  }
  return 0;
}

/* Try to match `#include "name"` on a line; the resolver wins over the
 * built-in headers. Returns 1 and fills name when matched. */
static int match_include(const char *line, char *name, size_t n) {
  const char *p = line;
  const char *q1, *q2;
  size_t nl;
  while (*p == ' ' || *p == '\t') p++;
  if (strncmp(p, "#include", 8) != 0)
    return 0;
  p += 8;
  q1 = strchr(p, '"');
  if (!q1)
    return 0;
  q2 = strchr(q1 + 1, '"');
  if (!q2)
    return 0;
  nl = (size_t)(q2 - q1 - 1);
  if (nl == 0 || nl >= n)
    return 0;
  memcpy(name, q1 + 1, nl);
  name[nl] = '\0';
  return 1;
}

#define XW_INCLUDE_DEPTH_MAX 8

static int line_contains(const char *p, size_t len, const char *needle) {
  size_t nl = strlen(needle);
  size_t i;
  if (nl == 0 || len < nl)
    return 0;
  for (i = 0; i + nl <= len; i++)
    if (memcmp(p + i, needle, nl) == 0)
      return 1;
  return 0;
}

/* Copy `text` into the buffer, expanding #include lines. A missing header
 * becomes a comment (the engine does the same) rather than an error.
 * Vertex attributes are pinned to the renderer's VAO slots (0 = position,
 * 1 = texcoord) because compiling for SPIR-V requires explicit locations
 * and glslang's auto-mapping would not match our layout. */
static int emit_source(char **tmp, size_t *len, size_t *cap, const char *text,
                       int is_vertex, XWShaderInclude inc, void *user,
                       int depth) {
  const char *p = text;
  while (*p) {
    const char *eol = strchr(p, '\n');
    size_t line_len = eol ? (size_t)(eol - p) + 1 : strlen(p);
    char name[256];
    if (match_include(p, name, sizeof(name))) {
      const char *content = inc ? inc(user, name) : NULL;
      char *owned = NULL;
      if (!content) {
        /* the engine's own assets (when pointed at) win over our copies */
        owned = assets_header(name);
        content = owned ? owned : xw_shader_builtin_header(name);
      }
      if (content && depth < XW_INCLUDE_DEPTH_MAX) {
        int bad = append_cstr(tmp, len, cap, "// begin include ") != 0 ||
                  append_cstr(tmp, len, cap, name) != 0 ||
                  append_cstr(tmp, len, cap, "\n") != 0 ||
                  emit_source(tmp, len, cap, content, is_vertex, inc, user,
                              depth + 1) != 0 ||
                  append_cstr(tmp, len, cap, "// end include\n") != 0;
        free(owned);
        if (bad)
          return -1;
      } else {
        free(owned);
        if (append_cstr(tmp, len, cap, "// include not found: ") != 0 ||
            append_cstr(tmp, len, cap, name) != 0 ||
            append_cstr(tmp, len, cap, "\n") != 0)
          return -1;
      }
    } else if (line_len >= 8 && strncmp(p, "#version", 8) == 0) {
      /* the generated header owns #version; drop any in the source */
    } else {
      const char *loc = NULL;
      if (is_vertex && line_contains(p, line_len, "attribute")) {
        /* particle shaders declare a wider attribute set; match the
         * longest names first so a_TexCoordVec4 does not hit a_TexCoord */
        if (line_contains(p, line_len, "a_Position"))
          loc = "layout(location=0) ";
        else if (line_contains(p, line_len, "a_TexCoordVec4C1"))
          loc = "layout(location=3) ";
        else if (line_contains(p, line_len, "a_TexCoordVec4"))
          loc = "layout(location=1) ";
        else if (line_contains(p, line_len, "a_TexCoordC2"))
          loc = "layout(location=4) ";
        else if (line_contains(p, line_len, "a_Color"))
          loc = "layout(location=2) ";
        else if (line_contains(p, line_len, "a_TexCoord"))
          loc = "layout(location=1) ";
      }
      if (loc && append_cstr(tmp, len, cap, loc) != 0)
        return -1;
      if (append(tmp, len, cap, p, line_len) != 0)
        return -1;
    }
    p += line_len;
  }
  return 0;
}

int xw_shader_preprocess(const char *src, char *dst, size_t n, int is_vertex,
                         const XWSceneCombo *combos, int n_combos,
                         unsigned int texture_slots, XWShaderInclude inc,
                         void *user) {
  char *tmp = NULL;
  size_t len = 0, cap = 0;
  int i;
  if (!src || !dst || n == 0)
    return -1;
  if (append_cstr(&tmp, &len, &cap, "#version 330\nprecision highp float;\n") != 0 ||
      append_cstr(&tmp, &len, &cap, dialect_defines) != 0 ||
      append_cstr(&tmp, &len, &cap,
                  is_vertex ? vertex_defines : fragment_defines) != 0)
    goto fail;
  for (i = 0; i < n_combos; i++) {
    char def[128];
    int dl;
    if (!combos[i].key[0])
      continue;
    dl = snprintf(def, sizeof(def), "#define %s %s\n", combos[i].key,
                  combos[i].value[0] ? combos[i].value : "1");
    if (dl <= 0 || dl >= (int)sizeof(def))
      continue;
    if (append(&tmp, &len, &cap, def, (size_t)dl) != 0)
      goto fail;
  }
  if (emit_combos(&tmp, &len, &cap, src, combos, n_combos) != 0)
    goto fail;
  if (emit_sampler_combos(&tmp, &len, &cap, src, texture_slots, combos,
                          n_combos) != 0)
    goto fail;
  if (emit_source(&tmp, &len, &cap, src, is_vertex, inc, user, 0) != 0)
    goto fail;
  if (len + 1 > n)
    goto fail;
  memcpy(dst, tmp, len + 1);
  free(tmp);
  return 0;
fail:
  free(tmp);
  return -1;
}

int xw_shader_translate_stage(const char *src, char *dst, size_t n,
                              int is_vertex) {
  return xw_shader_preprocess(src, dst, n, is_vertex, NULL, 0, 0, NULL, NULL);
}

int xw_shader_translate(const char *src, char *dst, size_t n) {
  return xw_shader_preprocess(src, dst, n, 0, NULL, 0, 0, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Linked varying compatibility                                        */
/* ------------------------------------------------------------------ */

static int wordch(char c) {
  return c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

/* The engine guards a varying with #if <COMBO> and declares a different
 * width per branch (genericparticle declares vec2/vec4 v_TexCoord under
 * #if SPRITESHEET). Collecting the raw text therefore picks a width from an
 * inactive branch and widens a pair that actually matches, so the collector
 * has to evaluate the conditions against the combo defines. The evaluator
 * covers what the engine's shaders use: identifiers, defined(), !, &&, ||,
 * parentheses and numeric literals. */
static const XWSceneCombo *ev_combos;
static int ev_ncombos;

static int ev_defined(const char *name, size_t len) {
  int i;
  for (i = 0; i < ev_ncombos; i++)
    if (strlen(ev_combos[i].key) == len &&
        strncmp(ev_combos[i].key, name, len) == 0)
      return 1;
  return 0;
}

static long ev_lookup(const char *name, size_t len) {
  int i;
  for (i = 0; i < ev_ncombos; i++)
    if (strlen(ev_combos[i].key) == len &&
        strncmp(ev_combos[i].key, name, len) == 0)
      return ev_combos[i].value[0] ? strtol(ev_combos[i].value, NULL, 0) : 1;
  return 0;
}

static const char *ev_ws(const char *p) {
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}

static long ev_or(const char **pp);

static long ev_primary(const char **pp) {
  const char *p = ev_ws(*pp);
  size_t nl = 0;
  if (*p == '(') {
    long v;
    p++;
    v = ev_or(&p);
    p = ev_ws(p);
    if (*p == ')')
      p++;
    *pp = p;
    return v;
  }
  if (strncmp(p, "defined", 7) == 0 && !wordch(p[7])) {
    long v;
    p = ev_ws(p + 7);
    if (*p == '(')
      p = ev_ws(p + 1);
    while (wordch(p[nl]))
      nl++;
    v = nl ? ev_defined(p, nl) : 0;
    p += nl;
    p = ev_ws(p);
    if (*p == ')')
      p++;
    *pp = p;
    return v;
  }
  if (*p >= '0' && *p <= '9') {
    char *e = NULL;
    long v = strtol(p, &e, 0);
    *pp = e;
    return v;
  }
  while (wordch(p[nl]))
    nl++;
  if (nl == 0) {
    *pp = p + 1;
    return 0;
  }
  *pp = p + nl;
  return ev_lookup(p, nl);
}

static long ev_unary(const char **pp) {
  const char *p = ev_ws(*pp);
  if (*p == '!') {
    p++;
    return !ev_unary(&p);
  }
  return ev_primary(pp);
}

static long ev_and(const char **pp) {
  long v = ev_unary(pp);
  const char *p = ev_ws(*pp);
  while (p[0] == '&' && p[1] == '&') {
    long r;
    p = ev_ws(p + 2);
    r = ev_unary(&p);
    v = v && r;
    p = ev_ws(p);
  }
  *pp = p;
  return v;
}

static long ev_or(const char **pp) {
  long v = ev_and(pp);
  const char *p = ev_ws(*pp);
  while (p[0] == '|' && p[1] == '|') {
    long r;
    p = ev_ws(p + 2);
    r = ev_and(&p);
    v = v || r;
    p = ev_ws(p);
  }
  *pp = p;
  return v;
}

/* Collect the names the fragment unit declares `varying vec4`, skipping the
 * #if branches the combo defines turn off. */
static void collect_frag_varyings(const char *frag, const XWSceneCombo *combos,
                                  int n_combos, char names[][64], int max,
                                  int *out_n) {
  struct {
    int active, taken;
  } stack[32];
  int sp = 0, k = *out_n;
  const char *p = frag;
  ev_combos = combos;
  ev_ncombos = n_combos;
  while (*p) {
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    size_t s = 0;
    while (s < len && (p[s] == ' ' || p[s] == '\t'))
      s++;
    if (s < len && p[s] == '#') {
      char kw[16], cond[256];
      size_t ks = 0, cs;
      const char *d = p + s + 1;
      while (d < p + len && (*d == ' ' || *d == '\t'))
        d++;
      while (d < p + len && *d != ' ' && *d != '\t' && ks < sizeof(kw) - 1)
        kw[ks++] = *d++;
      kw[ks] = '\0';
      while (d < p + len && (*d == ' ' || *d == '\t'))
        d++;
      cs = (size_t)(p + len - d);
      if (cs > sizeof(cond) - 1)
        cs = sizeof(cond) - 1;
      memcpy(cond, d, cs);
      cond[cs] = '\0';
      if (strcmp(kw, "if") == 0 || strcmp(kw, "elif") == 0) {
        const char *ep = cond;
        int eval = (ev_or(&ep) != 0);
        if (strcmp(kw, "if") == 0) {
          int parent = sp ? stack[sp - 1].active : 1;
          if (sp < (int)(sizeof(stack) / sizeof(stack[0]))) {
            stack[sp].active = parent && eval;
            stack[sp].taken = parent && eval;
            sp++;
          }
        } else if (sp) {
          int parent = sp >= 2 ? stack[sp - 2].active : 1;
          int take = parent && !stack[sp - 1].taken && eval;
          stack[sp - 1].active = take;
          stack[sp - 1].taken = stack[sp - 1].taken || take;
        }
      } else if (strcmp(kw, "ifdef") == 0 || strcmp(kw, "ifndef") == 0) {
        size_t nl = 0;
        int parent = sp ? stack[sp - 1].active : 1;
        int def;
        while (cond[nl] && wordch(cond[nl]))
          nl++;
        def = nl ? ev_defined(cond, nl) : 0;
        if (strcmp(kw, "ifndef") == 0)
          def = !def;
        if (sp < (int)(sizeof(stack) / sizeof(stack[0]))) {
          stack[sp].active = parent && def;
          stack[sp].taken = parent && def;
          sp++;
        }
      } else if (strcmp(kw, "else") == 0) {
        if (sp) {
          int parent = sp >= 2 ? stack[sp - 2].active : 1;
          int take = parent && !stack[sp - 1].taken;
          stack[sp - 1].active = take;
          stack[sp - 1].taken = 1;
        }
      } else if (strcmp(kw, "endif") == 0) {
        if (sp)
          sp--;
      }
    } else if (sp == 0 || stack[sp - 1].active) {
      const char *q = p + s;
      while (q < p + len) {
        const char *r;
        char type[16];
        size_t tl = 0, nl = 0;
        q = strstr(q, "varying");
        if (!q || q >= p + len)
          break;
        r = q + 7;
        while (r < p + len && (*r == ' ' || *r == '\t'))
          r++;
        while (r + tl < p + len && wordch(r[tl]) && tl < sizeof(type) - 1)
          tl++;
        memcpy(type, r, tl);
        type[tl] = '\0';
        r += tl;
        while (r < p + len && (*r == ' ' || *r == '\t'))
          r++;
        while (r + nl < p + len && wordch(r[nl]) && nl < 63)
          nl++;
        if (tl && nl && strcmp(type, "vec4") == 0 && k < max) {
          memcpy(names[k], r, nl);
          names[k][nl] = '\0';
          k++;
        }
        q = r + (nl ? nl : 1);
      }
    }
    p = eol ? eol + 1 : p + len;
  }
  *out_n = k;
}

int xw_shader_fix_linked_varyings(const char *frag, char *vert, size_t n,
                                  const XWSceneCombo *combos, int n_combos) {
  char names[16][64];
  int widened[16];
  int nnames = 0, i;
  char *buf = NULL;
  size_t len = 0, cap = 0;
  if (!frag || !vert || n == 0)
    return -1;

  memset(widened, 0, sizeof(widened));

  /* collect `varying vec4 NAME;` declarations active in the fragment unit */
  collect_frag_varyings(frag, combos, n_combos, names, 16, &nnames);
  if (nnames == 0)
    return 0;

  /* 1) widen the matching `varying vec2 NAME` declarations in place */
  for (i = 0; i < nnames; i++) {
    char pat[128];
    char *hit;
    snprintf(pat, sizeof(pat), "varying vec2 %.64s", names[i]);
    hit = strstr(vert, pat);
    if (hit) {
      memcpy(hit + 8, "vec4", 4); /* "varying " is 8 chars, same length */
      widened[i] = 1;
    }
  }

  /* 2) pad bare `NAME = expr;` assignments only for the declarations we
   * widened above: a vertex unit that already declares the name vec4 (and
   * assigns a vec4, e.g. `v_Color = a_Color;`) must be left alone or the
   * constructor would end up with too many components. */
  {
    const char *s = vert;
    while (*s) {
      int matched = -1;
      size_t nl = 0;
      for (i = 0; i < nnames; i++) {
        size_t L = strlen(names[i]);
        if (!widened[i])
          continue;
        if ((s == vert || !wordch(s[-1])) && strncmp(s, names[i], L) == 0 &&
            !wordch(s[L])) {
          matched = i;
          nl = L;
          break;
        }
      }
      if (matched >= 0) {
        const char *after = s + nl;
        const char *eq = after;
        while (*eq == ' ' || *eq == '\t') eq++;
        if (*eq == '=' && eq[1] != '=') {
          const char *eol = strchr(eq, '\n');
          const char *semi = strchr(eq, ';');
          if (semi && (!eol || semi < eol)) {
            const char *rhs = eq + 1;
            const char *rend = semi;
            while (rhs < rend && (*rhs == ' ' || *rhs == '\t')) rhs++;
            while (rend > rhs && (rend[-1] == ' ' || rend[-1] == '\t')) rend--;
            if (append(&buf, &len, &cap, names[matched], nl) != 0 ||
                append_cstr(&buf, &len, &cap, " = vec4(") != 0 ||
                append(&buf, &len, &cap, rhs, (size_t)(rend - rhs)) != 0 ||
                append_cstr(&buf, &len, &cap, ", 0.0, 1.0);") != 0) {
              free(buf);
              return -1;
            }
            s = semi + 1;
            continue;
          }
        }
      }
      if (append(&buf, &len, &cap, s, 1) != 0) {
        free(buf);
        return -1;
      }
      s++;
    }
  }
  if (len + 1 > n) {
    free(buf);
    return -1;
  }
  memcpy(vert, buf, len + 1);
  free(buf);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Annotated uniform defaults                                          */
/* ------------------------------------------------------------------ */

static const char *skip_spaces(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  return p;
}

static int parse_floats(const char *p, const char *end, float *v, int cap) {
  int n = 0;
  while (p < end && n < cap) {
    char *e = NULL;
    p = skip_spaces(p, end);
    if (p >= end || *p == '"')
      break;
    v[n] = strtof(p, &e);
    if (!e || e == p)
      break;
    n++;
    p = e;
  }
  return n;
}

int xw_shader_collect_defaults(const char *src, XWShaderDef *defs, int *ndefs,
                               int cap) {
  const char *p;
  int n;
  if (!src || !defs || !ndefs || cap <= 0)
    return -1;
  n = *ndefs;
  p = src;
  while ((p = strstr(p, "uniform")) != NULL) {
    const char *eol = strchr(p, '\n');
    const char *end = eol ? eol : p + strlen(p);
    const char *q = p + 7;
    char type[16], name[64], material[64];
    size_t tl, nl;
    const char *cmt, *mk, *dk;
    int kind, got;
    float v[4] = {0, 0, 0, 0};
    if (n >= cap)
      break;
    q = skip_spaces(q, end);
    tl = 0;
    while (q + tl < end && tl < sizeof(type) - 1 &&
           ((q[tl] >= 'a' && q[tl] <= 'z') ||
            (q[tl] >= 'A' && q[tl] <= 'Z') || (q[tl] >= '0' && q[tl] <= '9')))
      tl++;
    if (tl == 0 || tl >= sizeof(type))
      goto next;
    memcpy(type, q, tl);
    type[tl] = '\0';
    if (strcmp(type, "float") == 0)
      kind = 1;
    else if (strcmp(type, "vec2") == 0)
      kind = 2;
    else if (strcmp(type, "vec3") == 0)
      kind = 3;
    else if (strcmp(type, "vec4") == 0 || strcmp(type, "mat4") == 0)
      kind = 4;
    else if (strcmp(type, "sampler2D") == 0)
      kind = 0; /* sampler: `default` names a texture, not a number */
    else
      goto next; /* int/array: no default to apply */
    q = skip_spaces(q + tl, end);
    nl = 0;
    while (q + nl < end && nl < sizeof(name) - 1 &&
           (q[nl] == '_' || (q[nl] >= 'a' && q[nl] <= 'z') ||
            (q[nl] >= 'A' && q[nl] <= 'Z') || (q[nl] >= '0' && q[nl] <= '9')))
      nl++;
    if (nl == 0)
      goto next;
    memcpy(name, q, nl);
    name[nl] = '\0';
    cmt = strstr(q, "//");
    if (!cmt || cmt >= end)
      goto next;
    mk = strstr(cmt, "\"material\"");
    material[0] = '\0';
    if (mk && mk < end) {
      const char *colon = strchr(mk, ':');
      const char *a, *b;
      if (colon && colon < end && (a = strchr(colon, '"')) != NULL &&
          a < end && (b = strchr(a + 1, '"')) != NULL && b < end &&
          (size_t)(b - a - 1) < sizeof(material)) {
        memcpy(material, a + 1, (size_t)(b - a - 1));
        material[b - a - 1] = '\0';
      }
    }
    dk = strstr(cmt, "\"default\"");
    if (!dk || dk >= end)
      goto next;
    if (kind == 0) {
      /* samplers default to a texture the engine loads itself (util/white,
       * util/perlin_256, gradient/..., _rt_* render targets) */
      const char *colon = strchr(dk, ':');
      const char *a, *b;
      char tref[64];
      if (!colon || colon >= end || (a = strchr(colon, '"')) == NULL ||
          a >= end || (b = strchr(a + 1, '"')) == NULL || b >= end ||
          (size_t)(b - a - 1) >= sizeof(tref))
        goto next;
      memcpy(tref, a + 1, (size_t)(b - a - 1));
      tref[b - a - 1] = '\0';
      for (int i = 0; i < n; i++) {
        if (strcmp(defs[i].name, name) == 0)
          goto next;
      }
      snprintf(defs[n].name, sizeof(defs[n].name), "%s", name);
      snprintf(defs[n].material, sizeof(defs[n].material), "%s", material);
      snprintf(defs[n].tex, sizeof(defs[n].tex), "%s", tref);
      defs[n].kind = 0;
      memset(defs[n].v, 0, sizeof(defs[n].v));
      n++;
      goto next;
    }
    {
      const char *colon = strchr(dk, ':');
      const char *ds;
      if (!colon || colon >= end)
        goto next;
      ds = skip_spaces(colon + 1, end);
      if (ds < end && *ds == '"') {
        const char *qe = strchr(ds + 1, '"');
        if (!qe || qe > end)
          goto next;
        got = parse_floats(ds + 1, qe, v, 4);
      } else {
        got = parse_floats(ds, end, v, 4);
      }
      if (got <= 0)
        goto next;
    }
    if (strcmp(type, "mat4") == 0)
      goto next;
    for (int i = 0; i < n; i++) {
      if (strcmp(defs[i].name, name) == 0)
        goto next;
    }
    snprintf(defs[n].name, sizeof(defs[n].name), "%s", name);
    snprintf(defs[n].material, sizeof(defs[n].material), "%s", material);
    /* scalar defaults splat across vector uniforms (HLSL convention) */
    if (got == 1) {
      for (int k = 1; k < kind; k++) v[k] = v[0];
    }
    defs[n].kind = kind;
    memcpy(defs[n].v, v, sizeof(v));
    n++;
next:
    p = end;
  }
  *ndefs = n;
  return 0;
}
