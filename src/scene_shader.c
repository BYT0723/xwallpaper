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

static int word_char(char c) {
  return (c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z'));
}

int xw_shader_translate(const char *src, char *dst, size_t n) {
  if (!src || !dst || n == 0) return -1;
  char *tmp = NULL;
  size_t len = 0, cap = 0;
  const char *p = src;
  while (*p) {
    if (strncmp(p, "#include", 8) == 0 &&
        (p == src || p[-1] == '\n' || p[-1] == '\r')) {
      const char *eol = strchr(p, '\n');
      size_t adv = eol ? (size_t)(eol - p) + 1 : strlen(p);
      if (append(&tmp, &len, &cap, "// include expanded\n", 20) != 0) {
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
      if (append(&tmp, &len, &cap, "in", 2) != 0) {
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
  if (strstr(tmp ? tmp : "", "xw_FragColor") &&
      !strstr(tmp ? tmp : "", "out vec4 xw_FragColor"))
    pre = "#version 330 core\nout vec4 xw_FragColor;\n";
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
