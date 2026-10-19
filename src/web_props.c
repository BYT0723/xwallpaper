#include <stdio.h>
#include <string.h>

#include <json-c/json.h>

#include "web_props.h"

int xw_web_props_parse(const char *json, size_t len,
                        XWWebProp *out, int cap) {
  struct json_object *root = NULL, *general = NULL, *props = NULL;
  int n = 0;
  char *text;

  if (!json || !len || !out || cap <= 0)
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
    if (n >= cap) {
      json_object_put(root);
      return -1;
    }
    if (json_object_get_type(val) == json_type_object) {
      if (json_object_object_get_ex(val, "type", &t) && t &&
          json_object_get_type(t) == json_type_string && json_object_get_string(t))
        ts = json_object_get_string(t);
      if (json_object_object_get_ex(val, "value", &v) && v) {
        if (json_object_get_type(v) == json_type_string) {
          if (json_object_get_string(v))
            vs = json_object_get_string(v);
        } else {
          /* numbers/bools: JSON text form ("100", "true") */
          if (json_object_get_string(v))
            vs = json_object_get_string(v);
        }
      }
    }
    snprintf(out[n].name, sizeof(out[n].name), "%s", key);
    snprintf(out[n].type, sizeof(out[n].type), "%s", ts);
    snprintf(out[n].value, sizeof(out[n].value), "%s", vs);
    n++;
  }

  json_object_put(root);
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

int xw_web_props_apply_js(const XWWebProp *props, int n,
                           char *out, size_t m) {
  char obj[8192];
  size_t off = 0;
  if (!props || n <= 0 || !out || m == 0)
    return -1;
  if (xw_web_props_object_js(props, n, obj, sizeof(obj)) != 0)
    return -1;
  if (lit_append(out, m, &off,
                 "window.wallpaperPropertyListener&&"
                 "window.wallpaperPropertyListener.applyUserProperties(") != 0 ||
      lit_append(out, m, &off, obj) != 0 ||
      lit_append(out, m, &off, ");") != 0)
    return -1;
  if (off >= m)
    return -1;
  out[off] = '\0';
  return 0;
}
