#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XW_HAVE_JSONC
#include <json-c/json.h>
#endif

#include "scene_parse.h"

static void copy_str(char *dst, size_t n, const char *src) {
  if (!dst || n == 0) return;
  if (!src) src = "";
  snprintf(dst, n, "%s", src);
}

#ifdef XW_HAVE_JSONC
static int get_vec2(struct json_object *o, const char *key, float *x, float *y,
                    float dx, float dy) {
  struct json_object *a = NULL;
  *x = dx;
  *y = dy;
  if (!json_object_object_get_ex(o, key, &a) || !a) return 0;
  if (json_object_get_type(a) != json_type_array) return -1;
  if (json_object_array_length(a) < 2) return -1;
  struct json_object *e0 = json_object_array_get_idx(a, 0);
  struct json_object *e1 = json_object_array_get_idx(a, 1);
  if (!e0 || !e1) return -1;
  *x = (float)json_object_get_double(e0);
  *y = (float)json_object_get_double(e1);
  return 0;
}

static int parse_pass(struct json_object *jo, XWScenePass *p, XWError *err) {
  struct json_object *v = NULL;
  memset(p, 0, sizeof(*p));
  if (json_object_object_get_ex(jo, "shader", &v) && v)
    copy_str(p->shader, sizeof(p->shader), json_object_get_string(v));
  if (json_object_object_get_ex(jo, "combos", &v) && v) {
    if (json_object_get_type(v) != json_type_object) {
      XW_ERROR_SET(err, 1, "pass combos must be an object");
      return -1;
    }
    json_object_object_foreach(v, key, val) {
      if (p->n_combos >= XW_SCENE_COMBOS_MAX) break;
      copy_str(p->combos[p->n_combos].key,
               sizeof(p->combos[p->n_combos].key), key);
      copy_str(p->combos[p->n_combos].value,
               sizeof(p->combos[p->n_combos].value),
               json_object_get_string(val));
      p->n_combos++;
    }
  }
  if (json_object_object_get_ex(jo, "binds", &v) && v) {
    if (json_object_get_type(v) != json_type_object) {
      XW_ERROR_SET(err, 1, "pass binds must be an object");
      return -1;
    }
    json_object_object_foreach(v, key, val) {
      if (p->n_binds >= XW_SCENE_BINDS_MAX) break;
      copy_str(p->binds[p->n_binds].name,
               sizeof(p->binds[p->n_binds].name), key);
      p->binds[p->n_binds].slot = json_object_get_int(val);
      p->n_binds++;
    }
  }
  return 0;
}

static void free_effects(XWSceneEffect *efx, int n) {
  if (!efx) return;
  for (int i = 0; i < n; i++) free(efx[i].passes);
  free(efx);
}

static int parse_effect(struct json_object *jo, XWSceneEffect *e, XWError *err) {
  struct json_object *v = NULL;
  memset(e, 0, sizeof(*e));
  if (json_object_object_get_ex(jo, "name", &v) && v)
    copy_str(e->name, sizeof(e->name), json_object_get_string(v));
  if (json_object_object_get_ex(jo, "passes", &v) && v) {
    if (json_object_get_type(v) != json_type_array) {
      XW_ERROR_SET(err, 1, "effect passes must be an array");
      return -1;
    }
    size_t n = json_object_array_length(v);
    e->passes = calloc(n ? n : 1, sizeof(XWScenePass));
    if (!e->passes) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    for (size_t i = 0; i < n; i++) {
      struct json_object *pj = json_object_array_get_idx(v, i);
      if (!pj || json_object_get_type(pj) != json_type_object) {
        XW_ERROR_SET(err, 1, "pass must be an object");
        free(e->passes);
        e->passes = NULL;
        return -1;
      }
      if (parse_pass(pj, &e->passes[i], err) != 0) {
        free(e->passes);
        e->passes = NULL;
        return -1;
      }
      e->n_passes++;
    }
  }
  return 0;
}

static int parse_object(struct json_object *jo, XWSceneObject *o, XWError *err) {
  struct json_object *v = NULL;
  memset(o, 0, sizeof(*o));
  o->visible = 1;
  o->scale[0] = 1.0f;
  o->scale[1] = 1.0f;
  if (json_object_object_get_ex(jo, "id", &v) && v)
    copy_str(o->id, sizeof(o->id), json_object_get_string(v));
  if (json_object_object_get_ex(jo, "model", &v) && v)
    copy_str(o->model, sizeof(o->model), json_object_get_string(v));
  if (json_object_object_get_ex(jo, "material", &v) && v)
    copy_str(o->material, sizeof(o->material), json_object_get_string(v));
  if (get_vec2(jo, "origin", &o->origin[0], &o->origin[1], 0, 0) != 0 ||
      get_vec2(jo, "scale", &o->scale[0], &o->scale[1], 1, 1) != 0 ||
      get_vec2(jo, "size", &o->size[0], &o->size[1], 0, 0) != 0) {
    XW_ERROR_SET(err, 1, "object origin/scale/size must be [x, y]");
    return -1;
  }
  if (json_object_object_get_ex(jo, "visible", &v) && v)
    o->visible = json_object_get_boolean(v) ? 1 : 0;
  if (json_object_object_get_ex(jo, "effects", &v) && v) {
    if (json_object_get_type(v) != json_type_array) {
      XW_ERROR_SET(err, 1, "object effects must be an array");
      return -1;
    }
    size_t n = json_object_array_length(v);
    o->effects = calloc(n ? n : 1, sizeof(XWSceneEffect));
    if (!o->effects) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    for (size_t i = 0; i < n; i++) {
      struct json_object *ej = json_object_array_get_idx(v, i);
      if (!ej || json_object_get_type(ej) != json_type_object) {
        XW_ERROR_SET(err, 1, "effect must be an object");
        free_effects(o->effects, o->n_effects);
        o->effects = NULL;
        return -1;
      }
      if (parse_effect(ej, &o->effects[i], err) != 0) {
        free_effects(o->effects, o->n_effects);
        o->effects = NULL;
        return -1;
      }
      o->n_effects++;
    }
  }
  return 0;
}
#endif

int xw_scene_parse(const char *dir, XWScene *out, XWError *err) {
  if (!dir || !out) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  memset(out, 0, sizeof(*out));
#ifndef XW_HAVE_JSONC
  XW_ERROR_SET(err, 1, "scene parsing needs json-c");
  return -1;
#else
  char path[1024];
  snprintf(path, sizeof(path), "%s/scene.json", dir);
  FILE *f = fopen(path, "rb");
  if (!f) {
    XW_ERROR_SET(err, 1, "cannot open scene.json");
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 8 * 1024 * 1024) {
    fclose(f);
    XW_ERROR_SET(err, 1, "bad scene.json size");
    return -1;
  }
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    XW_ERROR_SET(err, 1, "cannot read scene.json");
    return -1;
  }
  fclose(f);
  buf[sz] = '\0';
  struct json_object *root = json_tokener_parse(buf);
  free(buf);
  if (!root || json_object_get_type(root) != json_type_object) {
    if (root) json_object_put(root);
    XW_ERROR_SET(err, 1, "scene.json is not a JSON object");
    return -1;
  }
  struct json_object *cam = NULL;
  if (json_object_object_get_ex(root, "camera", &cam) && cam &&
      json_object_get_type(cam) == json_type_object) {
    struct json_object *v = NULL;
    float w = 0, h = 0;
    if (get_vec2(cam, "orthogonalprojection", &w, &h, 0, 0) != 0) {
      json_object_put(root);
      XW_ERROR_SET(err, 1, "camera orthogonalprojection must be [w, h]");
      return -1;
    }
    out->camera.ortho_w = w;
    out->camera.ortho_h = h;
    if (get_vec2(cam, "clearcolor", &out->camera.clearcolor[0],
                 &out->camera.clearcolor[1], 0, 0) != 0) {
      json_object_put(root);
      XW_ERROR_SET(err, 1, "camera clearcolor must be [r, g(, b)]");
      return -1;
    }
    if (json_object_object_get_ex(cam, "bloom", &v) && v)
      out->camera.bloom = json_object_get_boolean(v) ? 1 : 0;
    if (json_object_object_get_ex(cam, "parallax", &v) && v)
      out->camera.parallax = json_object_get_boolean(v) ? 1 : 0;
  }
  struct json_object *objs = NULL;
  if (json_object_object_get_ex(root, "objects", &objs) && objs) {
    if (json_object_get_type(objs) != json_type_array) {
      json_object_put(root);
      XW_ERROR_SET(err, 1, "objects must be an array");
      return -1;
    }
    size_t n = json_object_array_length(objs);
    out->objects = calloc(n ? n : 1, sizeof(XWSceneObject));
    if (!out->objects) {
      json_object_put(root);
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    for (size_t i = 0; i < n; i++) {
      struct json_object *oj = json_object_array_get_idx(objs, i);
      if (!oj || json_object_get_type(oj) != json_type_object) {
        XW_ERROR_SET(err, 1, "object must be an object");
        goto fail;
      }
      if (parse_object(oj, &out->objects[i], err) != 0) goto fail;
      out->n_objects++;
    }
  }
  json_object_put(root);
  return 0;
fail:
  json_object_put(root);
  xw_scene_free(out);
  return -1;
#endif
}

void xw_scene_free(XWScene *s) {
  if (!s || !s->objects) {
    if (s) {
      s->objects = NULL;
      s->n_objects = 0;
    }
    return;
  }
  for (int i = 0; i < s->n_objects; i++) {
    XWSceneObject *o = &s->objects[i];
    if (!o->effects) continue;
    for (int e = 0; e < o->n_effects; e++) free(o->effects[e].passes);
    free(o->effects);
  }
  free(s->objects);
  s->objects = NULL;
  s->n_objects = 0;
}
