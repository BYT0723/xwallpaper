#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_shader.h"

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

static int word_char(char c) {
  return (c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z'));
}

/* Wallpaper Engine common built-ins observed in workshop shaders. Rotation
 * direction follows the standard math convention; revisit against a
 * reference frame if parallaxed layers shear the wrong way. */
static const char *we_prelude =
    "#ifndef XW_WE_COMMON_H\n"
    "#define XW_WE_COMMON_H\n"
    "#define texSample2D(s, uv) texture(s, uv)\n"
    "#define mul(v, m) ((m) * (v))\n"
    "#define saturate(x) clamp(x, 0.0, 1.0)\n"
    "#define frac(x) fract(x)\n"
    "#define lerp(a, b, t) mix(a, b, t)\n"
    "#define CAST2(x) vec2(x)\n"
    "#define CAST3(x) vec3(x)\n"
    "#define CAST4(x) vec4(x)\n"
    "#ifndef M_PI\n"
    "#define M_PI 3.141592653589793\n"
    "#endif\n"
    "#ifndef M_PI_2\n"
    "#define M_PI_2 1.5707963267948966\n"
    "#endif\n"
    "#ifndef M_PI_4\n"
    "#define M_PI_4 0.7853981633974483\n"
    "#endif\n"
    "vec2 rotateVec2(vec2 v, float a) {\n"
    "  float c = cos(a);\n"
    "  float s = sin(a);\n"
    "  return vec2(c * v.x - s * v.y, s * v.x + c * v.y);\n"
    "}\n"
    "vec3 ApplyBlending(int mode, vec3 a, vec3 b, float blend) {\n"
    "  return mix(a, b, clamp(blend, 0.0, 1.0));\n"
    "}\n"
    "#endif\n";

int xw_shader_translate(const char *src, char *dst, size_t n) {
  return xw_shader_translate_stage(src, dst, n, 0);
}

int xw_shader_translate_stage(const char *src, char *dst, size_t n,
                              int is_vertex) {
  if (!src || !dst || n == 0) return -1;
  char *tmp = NULL;
  size_t len = 0, cap = 0;
  const char *p = src;
  /* built-ins are needed with or without an #include line */
  if (append_cstr(&tmp, &len, &cap, we_prelude) != 0) {
    free(tmp);
    return -1;
  }
  /* combo defaults from // [COMBO] {"combo":"NAME",...,"default":N}
   * annotations (integer defaults only; materials may override later) */
  {
    const char *q = src;
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
          memcpy(name, q1 + 1, nl);
          name[nl] = '\0';
          dk = strstr(q, "\"default\"");
          if (dk && dk < q + llen) {
            const char *dc = strchr(dk, ':');
            char *end = NULL;
            long dv = 0;
            if (dc && dc < q + llen) {
              const char *ds = dc + 1;
              while (ds < q + llen && (*ds == ' ' || *ds == '\t')) ds++;
              /* quoted defaults are paths/colors, not combo values */
              if (ds < q + llen && *ds != '"') {
                dv = strtol(ds, &end, 10);
                if (end != ds) {
                  char def[128];
                  int dl = snprintf(def, sizeof(def), "#define %s %ld\n",
                                    name, dv);
                  if (dl > 0 &&
                      append(&tmp, &len, &cap, def, (size_t)dl) != 0) {
                    free(tmp);
                    return -1;
                  }
                }
              }
            }
          }
        }
      }
      q += llen ? llen : 1;
    }
  }
  while (*p) {
    if (strncmp(p, "#include", 8) == 0 &&
        (p == src || p[-1] == '\n' || p[-1] == '\r')) {
      const char *eol = strchr(p, '\n');
      size_t adv = eol ? (size_t)(eol - p) + 1 : strlen(p);
      if (append_cstr(&tmp, &len, &cap, "// include skipped\n") != 0) {
        free(tmp);
        return -1;
      }
      p += adv;
      continue;
    }
    if (strncmp(p, "texture2D", 9) == 0 && !word_char(p[9])) {
      if (append(&tmp, &len, &cap, "texture", 7) != 0) {
        free(tmp);
        return -1;
      }
      p += 9;
      continue;
    }
    if (strncmp(p, "gl_FragColor", 12) == 0 && !word_char(p[12])) {
      if (append(&tmp, &len, &cap, "xw_FragColor", 12) != 0) {
        free(tmp);
        return -1;
      }
      p += 12;
      continue;
    }
    if (strncmp(p, "attribute", 9) == 0 && !word_char(p[9])) {
      if (append(&tmp, &len, &cap, "in", 2) != 0) {
        free(tmp);
        return -1;
      }
      p += 9;
      continue;
    }
    if (strncmp(p, "varying", 7) == 0 && !word_char(p[7])) {
      const char *kw = is_vertex ? "out" : "in";
      if (append(&tmp, &len, &cap, kw, strlen(kw)) != 0) {
        free(tmp);
        return -1;
      }
      p += 7;
      continue;
    }
    if (append(&tmp, &len, &cap, p, 1) != 0) {
      free(tmp);
      return -1;
    }
    p++;
  }
  const char *pre = "";
  if (!is_vertex && strstr(tmp ? tmp : "", "xw_FragColor") &&
      !strstr(tmp ? tmp : "", "out vec4 xw_FragColor"))
    pre = "#version 330 core\nout vec4 xw_FragColor;\n";
  if (is_vertex && !strstr(tmp ? tmp : "", "#version"))
    pre = "#version 330 core\n";
  size_t need = strlen(pre) + len + 1;
  if (need > n) {
    free(tmp);
    return -1;
  }
  memcpy(dst, pre, strlen(pre));
  memcpy(dst + strlen(pre), tmp ? tmp : "", len + 1);
  free(tmp);
  return 0;
}

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
    /* type */
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
    /* only annotated uniforms carry usable defaults */
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
    /* mat4 has no scalar default; skip (identity comes from draw) */
    if (strcmp(type, "mat4") == 0)
      goto next;
    /* dedupe by name (vertex + fragment declare the same uniforms) */
    for (int i = 0; i < n; i++) {
      if (strcmp(defs[i].name, name) == 0)
        goto next;
    }
    snprintf(defs[n].name, sizeof(defs[n].name), "%s", name);
    snprintf(defs[n].material, sizeof(defs[n].material), "%s", material);
    /* scalar defaults splat across vector uniforms (HLSL convention) */
    if (got == 1) {
      for (int k = 1; k < kind; k++) v[k] = v[0];
      got = kind;
    }
    (void)got;
    defs[n].kind = kind;
    memcpy(defs[n].v, v, sizeof(v));
    n++;
next:
    p = end;
  }
  *ndefs = n;
  return 0;
}
