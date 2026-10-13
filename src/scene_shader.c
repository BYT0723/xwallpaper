#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    "// Standard layer blending set used by the engine's imageblending combo\n"
    "// (docs.wallpaperengine.io/en/scene/shader/headers.html#common-blending-h).\n"
    "// The engine does not publish the numeric enum, so the ordering follows\n"
    "// the classic Photoshop-style list; anything unknown falls back to an\n"
    "// additive apply, which is how effect passes (sparkles, glow) compose.\n"
    "vec3 ApplyBlending(int mode, vec3 a, vec3 b, float blend) {\n"
    "  vec3 r;\n"
    "  if (mode == 1) r = min(a, b);\n"
    "  else if (mode == 2) r = a * b;\n"
    "  else if (mode == 5) r = max(a, b);\n"
    "  else if (mode == 6) r = 1.0 - (1.0 - a) * (1.0 - b);\n"
    "  else if (mode == 7) r = min(vec3(1.0), a / max(1.0 - b, vec3(1e-4)));\n"
    "  else if (mode == 8) r = min(a + b, vec3(1.0));\n"
    "  else if (mode == 9) r = min(a + b, vec3(1.0));\n"
    "  else if (mode == 11) r = mix(2.0 * a * b,\n"
    "                               1.0 - 2.0 * (1.0 - a) * (1.0 - b),\n"
    "                               step(0.5, a));\n"
    "  else if (mode == 13) r = mix(2.0 * a * b,\n"
    "                               1.0 - 2.0 * (1.0 - a) * (1.0 - b),\n"
    "                               step(0.5, b));\n"
    "  else if (mode == 18) r = abs(a - b);\n"
    "  else if (mode == 19) r = a + b - 2.0 * a * b;\n"
    "  else if (mode == 20) r = max(a - b, vec3(0.0));\n"
    "  else if (mode == 21) r = clamp(a / max(b + 0.01, vec3(1e-4)), 0.0, 1.0);\n"
    "  else if (mode == 25) r = clamp(a + b - 0.5, 0.0, 1.0);\n"
    "  else if (mode == 26) r = clamp(a - b + 0.5, 0.0, 1.0);\n"
    "  else r = min(a + b, vec3(1.0)); /* addition (mode 0/unknown) */\n"
    "  return mix(a, r, clamp(blend, 0.0, 1.0));\n"
    "}\n"
    "#endif\n";

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
      if (!content)
        content = xw_shader_builtin_header(name);
      if (content && depth < XW_INCLUDE_DEPTH_MAX) {
        if (append_cstr(tmp, len, cap, "// begin include ") != 0 ||
            append_cstr(tmp, len, cap, name) != 0 ||
            append_cstr(tmp, len, cap, "\n") != 0 ||
            emit_source(tmp, len, cap, content, is_vertex, inc, user,
                        depth + 1) != 0 ||
            append_cstr(tmp, len, cap, "// end include\n") != 0)
          return -1;
      } else if (append_cstr(tmp, len, cap, "// include not found: ") != 0 ||
                 append_cstr(tmp, len, cap, name) != 0 ||
                 append_cstr(tmp, len, cap, "\n") != 0) {
        return -1;
      }
    } else if (line_len >= 8 && strncmp(p, "#version", 8) == 0) {
      /* the generated header owns #version; drop any in the source */
    } else {
      const char *loc = NULL;
      if (is_vertex && line_contains(p, line_len, "attribute")) {
        if (line_contains(p, line_len, "a_Position"))
          loc = "layout(location=0) ";
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

int xw_shader_fix_linked_varyings(const char *frag, char *vert, size_t n) {
  char names[16][64];
  int nnames = 0, i;
  char *buf = NULL;
  size_t len = 0, cap = 0;
  const char *p;
  if (!frag || !vert || n == 0)
    return -1;

  /* collect `varying vec4 NAME;` declarations from the fragment unit */
  p = frag;
  while ((p = strstr(p, "varying")) != NULL) {
    const char *q = p + 7;
    char type[16], name[64];
    size_t tl = 0, nl = 0;
    while (*q == ' ' || *q == '\t') q++;
    while (q[tl] && wordch(q[tl]) && tl < sizeof(type) - 1) tl++;
    if (tl == 0) {
      p = q + 1;
      continue;
    }
    memcpy(type, q, tl);
    type[tl] = '\0';
    q += tl;
    while (*q == ' ' || *q == '\t') q++;
    while (q[nl] && wordch(q[nl]) && nl < sizeof(name) - 1) nl++;
    if (nl == 0) {
      p = q + 1;
      continue;
    }
    memcpy(name, q, nl);
    name[nl] = '\0';
    if (strcmp(type, "vec4") == 0 && nnames < 16)
      snprintf(names[nnames++], sizeof(names[0]), "%s", name);
    p = q + nl;
  }
  if (nnames == 0)
    return 0;

  /* 1) widen the matching `varying vec2 NAME` declarations in place */
  for (i = 0; i < nnames; i++) {
    char pat[128];
    char *hit;
    snprintf(pat, sizeof(pat), "varying vec2 %.64s", names[i]);
    hit = strstr(vert, pat);
    if (hit)
      memcpy(hit + 8, "vec4", 4); /* "varying " is 8 chars, same length */
  }

  /* 2) pad bare `NAME = expr;` assignments to vec4 */
  {
    const char *s = vert;
    while (*s) {
      int matched = -1;
      size_t nl = 0;
      for (i = 0; i < nnames; i++) {
        size_t L = strlen(names[i]);
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
    else
      goto next; /* sampler/int/array: no scalar default to apply */
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
