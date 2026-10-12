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
    "#define CAST2(x) vec2(x)\n"
    "vec2 rotateVec2(vec2 v, float a) {\n"
    "  float c = cos(a);\n"
    "  float s = sin(a);\n"
    "  return vec2(c * v.x - s * v.y, s * v.x + c * v.y);\n"
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
  int included = 0;
  while (*p) {
    if (strncmp(p, "#include", 8) == 0 &&
        (p == src || p[-1] == '\n' || p[-1] == '\r')) {
      const char *eol = strchr(p, '\n');
      size_t adv = eol ? (size_t)(eol - p) + 1 : strlen(p);
      if (!included) {
        if (append_cstr(&tmp, &len, &cap, we_prelude) != 0) {
          free(tmp);
          return -1;
        }
        included = 1;
      } else {
        if (append_cstr(&tmp, &len, &cap, "// include skipped\n") != 0) {
          free(tmp);
          return -1;
        }
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
