#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

#include "web_props.h"

int xw_web_props_parse(const char *json, size_t len, XWWebProp **out) {
  struct json_object *root = NULL, *general = NULL, *props = NULL;
  XWWebProp *arr = NULL;
  int n = 0, cap = 0;
  char *text;

  if (out)
    *out = NULL;
  if (!json || !len || !out)
    return -1;
  text = malloc(len + 1);
  if (!text)
    return -1;
  memcpy(text, json, len);
  text[len] = '\0';
  root = json_tokener_parse(text);
  free(text);
  if (!root)
    return -1;

  if (!json_object_object_get_ex(root, "general", &general) || !general ||
      !json_object_object_get_ex(general, "properties", &props) || !props ||
      json_object_get_type(props) != json_type_object) {
    json_object_put(root);
    return -1;
  }

  json_object_object_foreach(props, key, val) {
    struct json_object *t = NULL, *v = NULL;
    const char *ts = "", *vs = "";
    if (json_object_get_type(val) == json_type_object) {
      if (json_object_object_get_ex(val, "type", &t) && t &&
          json_object_get_type(t) == json_type_string) {
        const char *s = json_object_get_string(t);
        if (s)
          ts = s;
      }
      /* numbers/bools land as their JSON text form ("100", "true") */
      if (json_object_object_get_ex(val, "value", &v) && v) {
        const char *s = json_object_get_string(v);
        if (s)
          vs = s;
      }
    }
    /* Group headings have text/order but no type; they are not settings. */
    if (!ts[0])
      continue;
    if (n == cap) {
      int next = cap ? cap * 2 : 16;
      XWWebProp *grown;
      if (next > XW_WEB_PROPS_MAX)
        next = XW_WEB_PROPS_MAX;
      if (next <= cap) {
        free(arr);
        json_object_put(root);
        return -1;
      }
      grown = realloc(arr, sizeof(*arr) * (size_t)next);
      if (!grown) {
        free(arr);
        json_object_put(root);
        return -1;
      }
      arr = grown;
      cap = next;
    }
    snprintf(arr[n].name, sizeof(arr[n].name), "%s", key);
    snprintf(arr[n].type, sizeof(arr[n].type), "%s", ts);
    snprintf(arr[n].value, sizeof(arr[n].value), "%s", vs);
    n++;
  }

  json_object_put(root);
  *out = arr;
  return n;
}

/* JSON-style double-quote escaping for a JS string literal. */
static int esc_append(char *out, size_t m, size_t *off, const char *s) {
  for (; *s; s++) {
    char esc[8];
    int w;
    switch (*s) {
    case '"':
      w = snprintf(esc, sizeof(esc), "\\\"");
      break;
    case '\\':
      w = snprintf(esc, sizeof(esc), "\\\\");
      break;
    case '\n':
      w = snprintf(esc, sizeof(esc), "\\n");
      break;
    case '\r':
      w = snprintf(esc, sizeof(esc), "\\r");
      break;
    case '\t':
      w = snprintf(esc, sizeof(esc), "\\t");
      break;
    default:
      if ((unsigned char)*s < 0x20) {
        w = snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*s);
      } else {
        if (*off + 1 >= m)
          return -1;
        out[(*off)++] = *s;
        continue;
      }
    }
    if (w < 0 || *off + (size_t)w >= m)
      return -1;
    memcpy(out + *off, esc, (size_t)w);
    *off += (size_t)w;
  }
  return 0;
}

int xw_web_props_normalize(const char *type, const char *value,
                            char *out, size_t m) {
  unsigned r = 0, g = 0, b = 0;
  int w;

  if (!type || !value || !out || m == 0)
    return -1;
  if (strcmp(type, "color") != 0) {
    if (snprintf(out, m, "%s", value) >= (int)m)
      return -1;
    return 0;
  }
  if (value[0] == '#') {
    size_t n = strlen(value + 1);
    char hex[7];
    if (n != 3 && n != 6)
      return -1;
    if (n == 3) {
      hex[0] = value[1]; hex[1] = value[1];
      hex[2] = value[2]; hex[3] = value[2];
      hex[4] = value[3]; hex[5] = value[3];
    } else {
      memcpy(hex, value + 1, 6);
    }
    hex[6] = '\0';
    for (int i = 0; i < 6; i++) {
      if (!((hex[i] >= '0' && hex[i] <= '9') ||
            (hex[i] >= 'a' && hex[i] <= 'f') ||
            (hex[i] >= 'A' && hex[i] <= 'F')))
        return -1;
    }
    unsigned v = (unsigned)strtoul(hex, NULL, 16);
    r = v >> 16 & 0xff;
    g = v >> 8 & 0xff;
    b = v & 0xff;
  } else {
    /* native WE triple: three floats, passed through verbatim */
    const char *s = value;
    if (!*s)
      return -1;
    while (*s == ' ' || *s == '\t')
      s++;
    for (int i = 0; i < 3; i++) {
      char *end;
      if (*s == '\0')
        return -1;
      (void)strtof(s, &end);
      if (end == s)
        return -1;
      s = end;
      while (*s == ' ' || *s == '\t')
        s++;
    }
    if (*s != '\0')
      return -1;
    if (snprintf(out, m, "%s", value) >= (int)m)
      return -1;
    return 0;
  }
  w = snprintf(out, m, "%f %f %f", r / 255.0, g / 255.0, b / 255.0);
  if (w < 0 || (size_t)w >= m)
    return -1;
  return 0;
}

/* Byte length of s after esc_append() escaping. */
static size_t esc_len(const char *s) {
  size_t n = 0;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t')
      n += 2;
    else if (c < 0x20)
      n += 6;
    else
      n++;
  }
  return n;
}

static int lit_append(char *out, size_t m, size_t *off, const char *s) {
  size_t n = strlen(s);
  if (*off + n >= m)
    return -1;
  memcpy(out + *off, s, n);
  *off += n;
  return 0;
}

int xw_web_props_object_js(const XWWebProp *props, int n,
                            char *out, size_t m) {
  size_t off = 0;
  int i;
  if (!props || n <= 0 || !out || m == 0)
    return -1;
  if (lit_append(out, m, &off, "{") != 0)
    return -1;
  for (i = 0; i < n; i++) {
    if (i > 0 && lit_append(out, m, &off, ",") != 0)
      return -1;
    if (lit_append(out, m, &off, "\"") != 0 ||
        esc_append(out, m, &off, props[i].name) != 0 ||
        lit_append(out, m, &off, "\":{\"value\":\"") != 0 ||
        esc_append(out, m, &off, props[i].value) != 0 ||
        lit_append(out, m, &off, "\"}") != 0)
      return -1;
  }
  if (lit_append(out, m, &off, "}") != 0)
    return -1;
  if (off >= m)
    return -1;
  out[off] = '\0';
  return 0;
}

size_t xw_web_props_object_js_len(const XWWebProp *props, int n) {
  size_t total = 2; /* { ... } */
  int i;
  if (!props || n <= 0)
    return 0;
  for (i = 0; i < n; i++) {
    /* opening quote + ":{\"value\":\"" + closing quote/brace */
    size_t add = 15 + esc_len(props[i].name) + esc_len(props[i].value);
    if (i > 0)
      add++;
    if (add > (size_t)-1 - total)
      return 0;
    total += add;
  }
  if (total > (size_t)-1 - 1)
    return 0;
  return total + 1; /* trailing NUL */
}

int xw_web_props_apply_js(const XWWebProp *props, int n,
                           char *out, size_t m) {
  size_t off = 0;
  if (!props || n <= 0 || !out || m == 0)
    return -1;
  if (lit_append(out, m, &off,
                 "window.wallpaperPropertyListener&&"
                 "window.wallpaperPropertyListener.applyUserProperties(") != 0)
    return -1;
  if (xw_web_props_object_js(props, n, out + off, m - off) != 0)
    return -1;
  off += strlen(out + off);
  if (lit_append(out, m, &off, ");") != 0)
    return -1;
  if (off >= m)
    return -1;
  out[off] = '\0';
  return 0;
}
