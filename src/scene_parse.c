#include <math.h>
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

static int parse_project_props(const char *dir, XWScene *out, XWError *err);
static struct json_object *slurp_json(const char *dir, const char *rel);

#ifdef XW_HAVE_JSONC
static int parse_vec_str(const char *s, float *v, int n) {
  int i;
  if (!s)
    return -1;
  for (i = 0; i < n; i++) {
    char *end;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0')
      return -1;
    v[i] = strtof(s, &end);
    if (end == s)
      return -1;
    s = end;
  }
  return 0;
}

/* Real workshop files store vectors as "x y [z]" strings; our fixtures
 * use JSON arrays. Both accepted. */
static int get_vec2(struct json_object *o, const char *key, float *x, float *y,
                    float dx, float dy) {
  struct json_object *a = NULL;
  float v[2];
  *x = dx;
  *y = dy;
  if (!json_object_object_get_ex(o, key, &a) || !a) return 0;
  if (json_object_get_type(a) == json_type_string) {
    const char *s = json_object_get_string(a);
    if (!s || parse_vec_str(s, v, 2) != 0) return -1;
    *x = v[0];
    *y = v[1];
    return 0;
  }
  if (json_object_get_type(a) != json_type_array) return -1;
  if (json_object_array_length(a) < 2) return -1;
  struct json_object *e0 = json_object_array_get_idx(a, 0);
  struct json_object *e1 = json_object_array_get_idx(a, 1);
  if (!e0 || !e1) return -1;
  *x = (float)json_object_get_double(e0);
  *y = (float)json_object_get_double(e1);
  return 0;
}

static int get_vec3(struct json_object *o, const char *key, float *v) {
  struct json_object *a = NULL;
  if (!json_object_object_get_ex(o, key, &a) || !a) return 0;
  if (json_object_get_type(a) == json_type_string) {
    const char *s = json_object_get_string(a);
    if (!s || parse_vec_str(s, v, 3) != 0) return -1;
    return 0;
  }
  if (json_object_get_type(a) != json_type_array) return -1;
  if (json_object_array_length(a) < 3) return -1;
  for (int i = 0; i < 3; i++) {
    struct json_object *e = json_object_array_get_idx(a, i);
    if (!e) return -1;
    v[i] = (float)json_object_get_double(e);
  }
  return 0;
}

static int get_vec4(struct json_object *o, const char *key, float *v) {
  struct json_object *a = NULL;
  if (!json_object_object_get_ex(o, key, &a) || !a) return 0;
  if (json_object_get_type(a) == json_type_string) {
    const char *s = json_object_get_string(a);
    if (!s || parse_vec_str(s, v, 4) != 0) return -1;
    return 0;
  }
  if (json_object_get_type(a) != json_type_array) return -1;
  if (json_object_array_length(a) < 4) return -1;
  for (int i = 0; i < 4; i++) {
    struct json_object *e = json_object_array_get_idx(a, i);
    if (!e) return -1;
    v[i] = (float)json_object_get_double(e);
  }
  return 0;
}

static int parse_dynfloat(struct json_object *o, const char *key,
                          XWDynFloat *d, float def) {
  struct json_object *v = NULL;
  d->kind = XW_DYN_CONSTANT;
  d->constant = def;
  d->ref[0] = '\0';
  if (!json_object_object_get_ex(o, key, &v) || !v) return 0;
  enum json_type t = json_object_get_type(v);
  if (t == json_type_double || t == json_type_int) {
    d->constant = (float)json_object_get_double(v);
    return 0;
  }
  if (t != json_type_object) return -1;
  struct json_object *u = NULL;
  if (json_object_object_get_ex(v, "user", &u) && u) {
    struct json_object *w = NULL;
    d->kind = XW_DYN_USER;
    copy_str(d->ref, sizeof(d->ref), json_object_get_string(u));
    /* real walls carry the default in "value" */
    if (json_object_object_get_ex(v, "value", &w) && w &&
        (json_object_get_type(w) == json_type_double ||
         json_object_get_type(w) == json_type_int))
      d->constant = (float)json_object_get_double(w);
    return 0;
  }
  if (json_object_object_get_ex(v, "expression", &u) && u) {
    d->kind = XW_DYN_EXPRESSION;
    copy_str(d->ref, sizeof(d->ref), json_object_get_string(u));
    return 0;
  }
  return -1;
}

static XWObjType infer_type(struct json_object *jo) {
  struct json_object *v = NULL;
  if (json_object_object_get_ex(jo, "type", &v) && v &&
      json_object_get_type(v) == json_type_string) {
    const char *t = json_object_get_string(v);
    if (strcmp(t, "particle") == 0) return XW_OBJ_PARTICLE;
    if (strcmp(t, "text") == 0) return XW_OBJ_TEXT;
    if (strcmp(t, "sound") == 0) return XW_OBJ_SOUND;
    return XW_OBJ_IMAGE;
  }
  if (json_object_object_get_ex(jo, "text", &v) && v) return XW_OBJ_TEXT;
  if (json_object_object_get_ex(jo, "sound", &v) && v) return XW_OBJ_SOUND;
  if (json_object_object_get_ex(jo, "emitter", &v) && v) return XW_OBJ_PARTICLE;
  /* real workshop particle objects carry a config ref, not an emitter */
  if (json_object_object_get_ex(jo, "particle", &v) && v &&
      json_object_get_type(v) == json_type_string)
    return XW_OBJ_PARTICLE;
  return XW_OBJ_IMAGE;
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
  /* real workshop extras: constant uniforms + texture refs */
  if (json_object_object_get_ex(jo, "constantshadervalues", &v) && v) {
    if (json_object_get_type(v) != json_type_object) {
      XW_ERROR_SET(err, 1, "constantshadervalues must be an object");
      return -1;
    }
    json_object_object_foreach(v, key, val) {
      if (p->n_consts >= XW_SCENE_CONSTS_MAX) break;
      copy_str(p->consts[p->n_consts].key,
               sizeof(p->consts[p->n_consts].key), key);
      copy_str(p->consts[p->n_consts].value,
               sizeof(p->consts[p->n_consts].value),
               json_object_get_string(val));
      p->n_consts++;
    }
  }
  if (json_object_object_get_ex(jo, "textures", &v) && v) {
    size_t ti, tn;
    if (json_object_get_type(v) != json_type_array) {
      XW_ERROR_SET(err, 1, "pass textures must be an array");
      return -1;
    }
    tn = json_object_array_length(v);
    /* slot-aligned: null entries stay empty so unit indices match the
     * scene doc (reference TextureMap is index-keyed the same way) */
    if (tn > XW_SCENE_PASS_TEX_MAX)
      tn = XW_SCENE_PASS_TEX_MAX;
    for (ti = 0; ti < tn; ti++) {
      struct json_object *t = json_object_array_get_idx(v, ti);
      if (!t || json_object_get_type(t) != json_type_string)
        continue;
      copy_str(p->textures[ti], sizeof(p->textures[0]),
               json_object_get_string(t));
    }
    p->n_textures = (int)tn;
  }
  return 0;
}

/* Resolve scene effect "file" -> effect.json passes[i].material ->
 * material passes[0].shader into empty scene-pass shader slots
 * (index-aligned with clamp; missing links stay empty). */
static void resolve_effect_shaders(const char *dir, const char *eff_file,
                                   XWSceneEffect *e) {
  struct json_object *ej, *passes;
  size_t n, i;
  if (!dir || !eff_file || !eff_file[0] || !e || e->n_passes <= 0)
    return;
  ej = slurp_json(dir, eff_file);
  if (!ej)
    return;
  if (!json_object_object_get_ex(ej, "passes", &passes) || !passes ||
      json_object_get_type(passes) != json_type_array) {
    json_object_put(ej);
    return;
  }
  n = json_object_array_length(passes);
  if (n == 0) {
    json_object_put(ej);
    return;
  }
  /* effect-declared offscreen buffers (targets resolve to these) */
  {
    struct json_object *fbos = NULL, *fv = NULL;
    if (json_object_object_get_ex(ej, "fbos", &fbos) && fbos &&
        json_object_get_type(fbos) == json_type_array) {
      size_t fn = json_object_array_length(fbos), fi;
      for (fi = 0; fi < fn && e->n_fbos < XW_SCENE_FBOS_MAX; fi++) {
        struct json_object *fo = json_object_array_get_idx(fbos, fi);
        XWSceneFbo *dst;
        if (!fo || json_object_get_type(fo) != json_type_object)
          continue;
        dst = &e->fbos[e->n_fbos];
        memset(dst, 0, sizeof(*dst));
        if (json_object_object_get_ex(fo, "name", &fv) && fv)
          copy_str(dst->name, sizeof(dst->name), json_object_get_string(fv));
        if (!dst->name[0])
          continue;
        if (json_object_object_get_ex(fo, "width", &fv) && fv)
          dst->width = json_object_get_int(fv);
        if (json_object_object_get_ex(fo, "height", &fv) && fv)
          dst->height = json_object_get_int(fv);
        if (json_object_object_get_ex(fo, "format", &fv) && fv)
          copy_str(dst->format, sizeof(dst->format),
                   json_object_get_string(fv));
        if (json_object_object_get_ex(fo, "uvs", &fv) && fv)
          copy_str(dst->uvs, sizeof(dst->uvs), json_object_get_string(fv));
        e->n_fbos++;
      }
    }
  }
  for (i = 0; i < (size_t)e->n_passes; i++) {
    struct json_object *pj = json_object_array_get_idx(
        passes, i < n ? i : n - 1);
    struct json_object *mat = NULL, *mj = NULL, *mp = NULL, *sh = NULL;
    struct json_object *tg = NULL;
    if (!pj || json_object_get_type(pj) != json_type_object)
      continue;
    /* offscreen render target for this pass, if the effect uses one */
    if (json_object_object_get_ex(pj, "target", &tg) && tg &&
        json_object_get_type(tg) == json_type_string)
      copy_str(e->passes[i].target, sizeof(e->passes[i].target),
               json_object_get_string(tg));
    /* binds: [{"name": "_rt_X" | "previous", "index": N}] remaps the
     * sampler at index N to a named FBO (or the sequence input) */
    {
      struct json_object *binds = NULL;
      if (json_object_object_get_ex(pj, "bind", &binds) && binds &&
          json_object_get_type(binds) == json_type_array) {
        size_t bn = json_object_array_length(binds), bi;
        for (bi = 0; bi < bn && e->passes[i].n_binds < XW_SCENE_BINDS_MAX;
             bi++) {
          struct json_object *bo = json_object_array_get_idx(binds, bi);
          struct json_object *nv = NULL, *iv = NULL;
          XWSceneBind *bd;
          if (!bo || json_object_get_type(bo) != json_type_object)
            continue;
          if (!json_object_object_get_ex(bo, "name", &nv) || !nv)
            continue;
          bd = &e->passes[i].binds[e->passes[i].n_binds];
          memset(bd, 0, sizeof(*bd));
          copy_str(bd->name, sizeof(bd->name), json_object_get_string(nv));
          if (json_object_object_get_ex(bo, "index", &iv) && iv)
            bd->slot = json_object_get_int(iv);
          e->passes[i].n_binds++;
        }
      }
    }
    if (e->passes[i].shader[0])
      continue;
    if (!json_object_object_get_ex(pj, "material", &mat) || !mat ||
        json_object_get_type(mat) != json_type_string)
      continue;
    mj = slurp_json(dir, json_object_get_string(mat));
    if (!mj)
      continue;
    if (json_object_object_get_ex(mj, "passes", &mp) && mp &&
        json_object_get_type(mp) == json_type_array &&
        json_object_array_length(mp) >= 1) {
      struct json_object *m0 = json_object_array_get_idx(mp, 0);
      if (m0 && json_object_get_type(m0) == json_type_object &&
          json_object_object_get_ex(m0, "shader", &sh) && sh &&
          json_object_get_type(sh) == json_type_string)
        copy_str(e->passes[i].shader, sizeof(e->passes[i].shader),
                 json_object_get_string(sh));
    }
    json_object_put(mj);
  }
  json_object_put(ej);
}

static void free_effects(XWSceneEffect *efx, int n) {
  if (!efx) return;
  for (int i = 0; i < n; i++) free(efx[i].passes);
  free(efx);
}

static int parse_effect(const char *dir, struct json_object *jo,
                        XWSceneEffect *e, XWError *err) {
  struct json_object *v = NULL;
  memset(e, 0, sizeof(*e));
  e->visible = 1;
  if (json_object_object_get_ex(jo, "visible", &v) && v)
    e->visible = json_object_get_boolean(v) ? 1 : 0;
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
  if (json_object_object_get_ex(jo, "file", &v) && v &&
      json_object_get_type(v) == json_type_string)
    resolve_effect_shaders(dir, json_object_get_string(v), e);
  return 0;
}

/* Read a {user, value} multiplier (default constant 1). */
static void parse_mult(struct json_object *parent, const char *key,
                       XWDynFloat *d) {
  struct json_object *v = NULL;
  d->kind = XW_DYN_CONSTANT;
  d->constant = 1.0f;
  d->ref[0] = '\0';
  if (!json_object_object_get_ex(parent, key, &v) || !v)
    return;
  if (json_object_get_type(v) != json_type_object)
    return;
  {
    struct json_object *u = NULL, *w = NULL;
    if (json_object_object_get_ex(v, "user", &u) && u &&
        json_object_get_type(u) == json_type_string) {
      d->kind = XW_DYN_USER;
      copy_str(d->ref, sizeof(d->ref), json_object_get_string(u));
    }
    if (json_object_object_get_ex(v, "value", &w) && w &&
        (json_object_get_type(w) == json_type_double ||
         json_object_get_type(w) == json_type_int))
      d->constant = (float)json_object_get_double(w);
  }
}

static void parse_particle_cfg(const char *dir, struct json_object *jo,
                               XWSceneObject *o) {
  struct json_object *v = NULL, *cfg = NULL, *w = NULL;
  size_t i, n;
  if (!json_object_object_get_ex(jo, "particle", &v) || !v ||
      json_object_get_type(v) != json_type_string)
    return;
  parse_mult(jo, "count", &o->particle.count_mult);
  parse_mult(jo, "lifetime", &o->particle.life_mult);
  parse_mult(jo, "speed", &o->particle.speed_mult);
  /* NOTE: real walls nest these under "instanceoverride"; our fixtures
   * and a future normalize pass may hoist them. Accept both. */
  if (json_object_object_get_ex(jo, "instanceoverride", &w) && w &&
      json_object_get_type(w) == json_type_object) {
    struct json_object *a = NULL;
    if (json_object_object_get_ex(w, "alpha", &a) && a) {
      XWDynFloat dv;
      memset(&dv, 0, sizeof(dv));
      dv.kind = XW_DYN_CONSTANT;
      dv.constant = 1.0f;
      if (json_object_get_type(a) == json_type_object) {
        struct json_object *u = NULL, *val = NULL;
        if (json_object_object_get_ex(a, "user", &u) && u) {
          dv.kind = XW_DYN_USER;
          copy_str(dv.ref, sizeof(dv.ref), json_object_get_string(u));
        }
        if (json_object_object_get_ex(a, "value", &val) && val)
          dv.constant = (float)json_object_get_double(val);
      } else {
        dv.constant = (float)json_object_get_double(a);
      }
      o->alpha = dv;
    }
    parse_mult(w, "count", &o->particle.count_mult);
    parse_mult(w, "lifetime", &o->particle.life_mult);
    parse_mult(w, "speed", &o->particle.speed_mult);
  }
  cfg = slurp_json(dir, json_object_get_string(v));
  if (!cfg)
    return;
  o->particle.dirs[0] = o->particle.dirs[1] = o->particle.dirs[2] = 1.0f;
  o->particle.drag = 0.0f;
  if (json_object_object_get_ex(cfg, "maxcount", &w) && w)
    o->particle.max_count = json_object_get_int(w);
  if (json_object_object_get_ex(cfg, "renderer", &w) && w &&
      json_object_get_type(w) == json_type_array &&
      json_object_array_length(w) >= 1) {
    struct json_object *r0 = json_object_array_get_idx(w, 0);
    struct json_object *rn = NULL, *rm = NULL;
    if (r0 && json_object_get_type(r0) == json_type_object) {
      if (json_object_object_get_ex(r0, "name", &rn) && rn)
        copy_str(o->particle.renderer, sizeof(o->particle.renderer),
                 json_object_get_string(rn));
      if (json_object_object_get_ex(r0, "maxlength", &rm) && rm)
        o->particle.trail_max = (float)json_object_get_double(rm);
    }
  }
  if (o->particle.trail_max <= 0.0f)
    o->particle.trail_max = 30.0f;
  if (json_object_object_get_ex(cfg, "animationmode", &w) && w &&
      json_object_get_type(w) == json_type_string)
    copy_str(o->particle.animation_mode, sizeof(o->particle.animation_mode),
             json_object_get_string(w));
  if (json_object_object_get_ex(cfg, "emitter", &w) && w &&
      json_object_get_type(w) == json_type_array &&
      json_object_array_length(w) >= 1) {
    struct json_object *e0 = json_object_array_get_idx(w, 0);
    struct json_object *r = NULL, *s3 = NULL;
    if (e0 && json_object_get_type(e0) == json_type_object) {
      if (json_object_object_get_ex(e0, "rate", &r) && r)
        o->particle.rate = (float)json_object_get_double(r);
      if (json_object_object_get_ex(e0, "origin", &s3) && s3 &&
          json_object_get_type(s3) == json_type_string)
        parse_vec_str(json_object_get_string(s3), o->particle.origin, 3);
      if (json_object_object_get_ex(e0, "distancemin", &s3) && s3 &&
          json_object_get_type(s3) == json_type_string)
        parse_vec_str(json_object_get_string(s3), o->particle.dist_min, 3);
      if (json_object_object_get_ex(e0, "distancemax", &s3) && s3 &&
          json_object_get_type(s3) == json_type_string)
        parse_vec_str(json_object_get_string(s3), o->particle.dist_max, 3);
      if (json_object_object_get_ex(e0, "directions", &s3) && s3 &&
          json_object_get_type(s3) == json_type_string)
        parse_vec_str(json_object_get_string(s3), o->particle.dirs, 3);
    }
  }
  if (json_object_object_get_ex(cfg, "initializer", &w) && w &&
      json_object_get_type(w) == json_type_array) {
    n = json_object_array_length(w);
    for (i = 0; i < n; i++) {
      struct json_object *it = json_object_array_get_idx(w, i);
      struct json_object *nm = NULL;
      const char *name;
      float a[3], b[3], m[3], len;
      int k;
      if (!it || json_object_get_type(it) != json_type_object)
        continue;
      if (!json_object_object_get_ex(it, "name", &nm) || !nm)
        continue;
      name = json_object_get_string(nm);
      if (!name)
        continue;
      if (strcmp(name, "sizerandom") == 0) {
        struct json_object *lo = NULL, *hi = NULL;
        if (json_object_object_get_ex(it, "min", &lo) && lo)
          o->particle.size_min = (float)json_object_get_double(lo);
        if (json_object_object_get_ex(it, "max", &hi) && hi)
          o->particle.size_max = (float)json_object_get_double(hi);
      } else if (strcmp(name, "lifetimerandom") == 0) {
        struct json_object *lo = NULL, *hi = NULL;
        float lmin = 1.0f, lmax = 1.0f;
        if (json_object_object_get_ex(it, "min", &lo) && lo)
          lmin = (float)json_object_get_double(lo);
        if (json_object_object_get_ex(it, "max", &hi) && hi)
          lmax = (float)json_object_get_double(hi);
        o->particle.life = (lmin + lmax) / 2.0f;
      } else if (strcmp(name, "velocityrandom") == 0) {
        struct json_object *mn = NULL, *mx = NULL;
        a[0] = a[1] = a[2] = 0.0f;
        b[0] = b[1] = b[2] = 0.0f;
        if (json_object_object_get_ex(it, "min", &mn) && mn &&
            json_object_get_type(mn) == json_type_string)
          parse_vec_str(json_object_get_string(mn), a, 3);
        if (json_object_object_get_ex(it, "max", &mx) && mx &&
            json_object_get_type(mx) == json_type_string)
          parse_vec_str(json_object_get_string(mx), b, 3);
        memcpy(o->particle.vel_min, a, sizeof(a));
        memcpy(o->particle.vel_max, b, sizeof(b));
        len = 0.0f;
        for (k = 0; k < 3; k++) {
          m[k] = (a[k] + b[k]) / 2.0f;
          len += m[k] * m[k];
        }
        o->particle.speed = sqrtf(len);
      }
    }
  }
  if (json_object_object_get_ex(cfg, "operator", &w) && w &&
      json_object_get_type(w) == json_type_array) {
    n = json_object_array_length(w);
    for (i = 0; i < n; i++) {
      struct json_object *it = json_object_array_get_idx(w, i);
      struct json_object *nm = NULL, *g = NULL;
      float gv[3] = {0, 0, 0};
      if (!it || json_object_get_type(it) != json_type_object)
        continue;
      if (!json_object_object_get_ex(it, "name", &nm) || !nm)
        continue;
      if (strcmp(json_object_get_string(nm), "movement") != 0)
        continue;
      if (json_object_object_get_ex(it, "gravity", &g) && g &&
          json_object_get_type(g) == json_type_string &&
          parse_vec_str(json_object_get_string(g), gv, 3) == 0) {
        memcpy(o->particle.gravity_v, gv, sizeof(gv));
        o->particle.gravity = gv[1];
      }
      {
        struct json_object *dg = NULL;
        if (json_object_object_get_ex(it, "drag", &dg) && dg)
          o->particle.drag = (float)json_object_get_double(dg);
      }
    }
  }
  if (json_object_object_get_ex(cfg, "material", &w) && w &&
      json_object_get_type(w) == json_type_string) {
    struct json_object *mj = slurp_json(dir, json_object_get_string(w));
    struct json_object *mp = NULL;
    if (mj && json_object_object_get_ex(mj, "passes", &mp) && mp &&
        json_object_get_type(mp) == json_type_array &&
        json_object_array_length(mp) >= 1) {
      struct json_object *m0 = json_object_array_get_idx(mp, 0);
      struct json_object *tx = NULL;
      if (m0 && json_object_get_type(m0) == json_type_object &&
          json_object_object_get_ex(m0, "textures", &tx) && tx &&
          json_object_get_type(tx) == json_type_array &&
          json_object_array_length(tx) >= 1) {
        struct json_object *t0 = json_object_array_get_idx(tx, 0);
        if (t0 && json_object_get_type(t0) == json_type_string)
          copy_str(o->particle.sprite, sizeof(o->particle.sprite),
                   json_object_get_string(t0));
      }
    }
    if (mj)
      json_object_put(mj);
  }
  json_object_put(cfg);
}

/* Real workshop ids are numbers with a separate name; prefer the name. */
static void parse_id(struct json_object *jo, XWSceneObject *o) {
  struct json_object *v = NULL;
  if (json_object_object_get_ex(jo, "name", &v) && v &&
      json_object_get_type(v) == json_type_string) {
    copy_str(o->id, sizeof(o->id), json_object_get_string(v));
    return;
  }
  if (json_object_object_get_ex(jo, "id", &v) && v) {
    if (json_object_get_type(v) == json_type_string) {
      copy_str(o->id, sizeof(o->id), json_object_get_string(v));
    } else if (json_object_get_type(v) == json_type_int) {
      snprintf(o->id, sizeof(o->id), "%d", json_object_get_int(v));
    }
  }
}

/* Read a small JSON doc from dir/relpath; NULL when missing/unreadable.
 * Referenced chain files (models/materials) resolve lazily: absent files
 * leave refs stored but empty, they must not fail the whole parse. */
static struct json_object *slurp_json(const char *dir, const char *rel) {
  char path[1152], *buf;
  FILE *f;
  long sz;
  struct json_object *o;
  if (!dir || !rel || !rel[0] || strstr(rel, "..") != NULL)
    return NULL;
  snprintf(path, sizeof(path), "%s/%s", dir, rel);
  f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 1024 * 1024) {
    fclose(f);
    return NULL;
  }
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  buf[sz] = '\0';
  o = json_tokener_parse(buf);
  free(buf);
  if (!o || json_object_get_type(o) != json_type_object) {
    if (o)
      json_object_put(o);
    return NULL;
  }
  return o;
}

static XWBlend blend_from_name(const char *s) {
  if (!s)
    return XW_BLEND_NORMAL;
  if (strcmp(s, "translucent") == 0)
    return XW_BLEND_TRANSLUCENT;
  if (strcmp(s, "additive") == 0)
    return XW_BLEND_ADDITIVE;
  return XW_BLEND_NORMAL;
}

/* Follow models/X.json -> {"material": ...} -> material passes[0] for the
 * base blend mode and texture ref. Missing links stay empty (lazy). */
static void resolve_material(const char *dir, XWSceneObject *o) {
  struct json_object *model, *mat, *v, *p0;
  if (!o->model[0])
    return;
  model = slurp_json(dir, o->model);
  if (!model)
    return;
  if (json_object_object_get_ex(model, "material", &v) && v &&
      json_object_get_type(v) == json_type_string)
    copy_str(o->material, sizeof(o->material), json_object_get_string(v));
  json_object_put(model);
  if (!o->material[0])
    return;
  mat = slurp_json(dir, o->material);
  if (!mat)
    return;
  if (!json_object_object_get_ex(mat, "passes", &v) || !v ||
      json_object_get_type(v) != json_type_array ||
      json_object_array_length(v) < 1) {
    json_object_put(mat);
    return;
  }
  p0 = json_object_array_get_idx(v, 0);
  if (!p0 || json_object_get_type(p0) != json_type_object) {
    json_object_put(mat);
    return;
  }
  if (json_object_object_get_ex(p0, "blending", &v) && v &&
      json_object_get_type(v) == json_type_string)
    o->blend = blend_from_name(json_object_get_string(v));
  if (json_object_object_get_ex(p0, "textures", &v) && v &&
      json_object_get_type(v) == json_type_array &&
      json_object_array_length(v) >= 1) {
    struct json_object *t0 = json_object_array_get_idx(v, 0);
    if (t0 && json_object_get_type(t0) == json_type_string)
      copy_str(o->mat_tex, sizeof(o->mat_tex), json_object_get_string(t0));
  }
  json_object_put(mat);
}

static int parse_object_at(const char *dir, struct json_object *jo,
                           XWSceneObject *o, XWError *err) {
  struct json_object *v = NULL;
  memset(o, 0, sizeof(*o));
  o->visible = 1;
  o->scale[0] = 1.0f;
  o->scale[1] = 1.0f;
  /* particle colour defaults to opaque white for every object kind; the
   * "particle" config path never reaches the emitter block below */
  o->particle.color[0] = 1.0f;
  o->particle.color[1] = 1.0f;
  o->particle.color[2] = 1.0f;
  o->particle.color[3] = 1.0f;
  parse_id(jo, o);
  if (json_object_object_get_ex(jo, "model", &v) && v)
    copy_str(o->model, sizeof(o->model), json_object_get_string(v));
  /* real workshop image objects reference "image" instead of "model" */
  if (!o->model[0] && json_object_object_get_ex(jo, "image", &v) && v &&
      json_object_get_type(v) == json_type_string)
    copy_str(o->model, sizeof(o->model), json_object_get_string(v));
  if (json_object_object_get_ex(jo, "material", &v) && v)
    copy_str(o->material, sizeof(o->material), json_object_get_string(v));
  if (json_object_object_get_ex(jo, "texture", &v) && v &&
      json_object_get_type(v) == json_type_string)
    copy_str(o->texture, sizeof(o->texture), json_object_get_string(v));
  resolve_material(dir, o);
  if (get_vec2(jo, "origin", &o->origin[0], &o->origin[1], 0, 0) != 0 ||
      get_vec2(jo, "scale", &o->scale[0], &o->scale[1], 1, 1) != 0 ||
      get_vec2(jo, "size", &o->size[0], &o->size[1], 0, 0) != 0) {
    XW_ERROR_SET(err, 1, "object origin/scale/size must be [x, y]");
    return -1;
  }
  if (json_object_object_get_ex(jo, "visible", &v) && v) {
    if (json_object_get_type(v) == json_type_object) {
      /* real walls: {"user": ..., "value": bool} toggle */
      struct json_object *w = NULL;
      if (json_object_object_get_ex(v, "value", &w) && w)
        o->visible = json_object_get_boolean(w) ? 1 : 0;
    } else {
      o->visible = json_object_get_boolean(v) ? 1 : 0;
    }
  }
  o->type = infer_type(jo);
  if (parse_dynfloat(jo, "alpha", &o->alpha, 1.0f) != 0) {
    XW_ERROR_SET(err, 1, "object alpha must be a number or {user,expression}");
    return -1;
  }
  if (json_object_object_get_ex(jo, "emitter", &v) && v) {
    if (json_object_get_type(v) != json_type_object) {
      XW_ERROR_SET(err, 1, "emitter must be an object");
      return -1;
    }
    struct json_object *e = NULL;
    if (json_object_object_get_ex(v, "rate", &e) && e)
      o->particle.rate = (float)json_object_get_double(e);
    if (json_object_object_get_ex(v, "max_count", &e) && e)
      o->particle.max_count = json_object_get_int(e);
    if (json_object_object_get_ex(v, "sprite", &e) && e)
      copy_str(o->particle.sprite, sizeof(o->particle.sprite),
               json_object_get_string(e));
    if (json_object_object_get_ex(v, "life", &e) && e)
      o->particle.life = (float)json_object_get_double(e);
    if (json_object_object_get_ex(v, "speed", &e) && e)
      o->particle.speed = (float)json_object_get_double(e);
    if (json_object_object_get_ex(v, "spread", &e) && e)
      o->particle.spread = (float)json_object_get_double(e);
    if (json_object_object_get_ex(v, "size_min", &e) && e)
      o->particle.size_min = (float)json_object_get_double(e);
    if (json_object_object_get_ex(v, "size_max", &e) && e)
      o->particle.size_max = (float)json_object_get_double(e);
    if (json_object_object_get_ex(v, "gravity", &e) && e)
      o->particle.gravity = (float)json_object_get_double(e);
    o->particle.color[0] = 1.0f;
    o->particle.color[1] = 1.0f;
    o->particle.color[2] = 1.0f;
    o->particle.color[3] = 1.0f;
    if (get_vec4(v, "color", o->particle.color) != 0) {
      XW_ERROR_SET(err, 1, "emitter color must be [r, g, b, a]");
      return -1;
    }
  }
  parse_particle_cfg(dir, jo, o);
  if (json_object_object_get_ex(jo, "text", &v) && v) {
    if (json_object_get_type(v) != json_type_object) {
      XW_ERROR_SET(err, 1, "text must be an object");
      return -1;
    }
    struct json_object *e = NULL;
    if (json_object_object_get_ex(v, "content", &e) && e)
      copy_str(o->text.content, sizeof(o->text.content),
               json_object_get_string(e));
    if (json_object_object_get_ex(v, "font", &e) && e)
      copy_str(o->text.font, sizeof(o->text.font), json_object_get_string(e));
    if (json_object_object_get_ex(v, "size", &e) && e)
      o->text.size = (float)json_object_get_double(e);
    o->text.color[0] = 1.0f;
    o->text.color[1] = 1.0f;
    o->text.color[2] = 1.0f;
    o->text.color[3] = 1.0f;
    if (get_vec4(v, "color", o->text.color) != 0) {
      XW_ERROR_SET(err, 1, "text color must be [r, g, b, a]");
      return -1;
    }
  }
  if (json_object_object_get_ex(jo, "sound", &v) && v) {
    /* real walls: ["sounds/x.wav"]; ours: "path" */
    if (json_object_get_type(v) == json_type_array) {
      struct json_object *e0 = json_object_array_length(v) >= 1
                                   ? json_object_array_get_idx(v, 0)
                                   : NULL;
      if (e0 && json_object_get_type(e0) == json_type_string)
        copy_str(o->sound.file, sizeof(o->sound.file),
                 json_object_get_string(e0));
    } else {
      copy_str(o->sound.file, sizeof(o->sound.file),
               json_object_get_string(v));
    }
    o->sound.volume = 1.0f;
    o->sound.loop = 1;
    if (json_object_object_get_ex(jo, "volume", &v) && v) {
      if (json_object_get_type(v) == json_type_object) {
        /* real walls: {"user": ..., "value": ...} */
        XWDynFloat dv;
        memset(&dv, 0, sizeof(dv));
        if (parse_dynfloat(jo, "volume", &dv, 1.0f) == 0)
          o->sound.volume = dv.constant;
      } else {
        o->sound.volume = (float)json_object_get_double(v);
      }
    }
    if (json_object_object_get_ex(jo, "loop", &v) && v)
      o->sound.loop = json_object_get_boolean(v) ? 1 : 0;
    /* real walls: "playbackmode": "loop" */
    if (json_object_object_get_ex(jo, "playbackmode", &v) && v &&
        json_object_get_type(v) == json_type_string)
      o->sound.loop = strcmp(json_object_get_string(v), "loop") == 0 ? 1 : 0;
  }
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
      if (parse_effect(dir, ej, &o->effects[i], err) != 0) {
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
  struct json_object *gen = NULL;
  /* real workshop files carry projection under "general"; our fixtures
   * use a "camera" object. General wins when both exist. */
  if (json_object_object_get_ex(root, "general", &gen) && gen &&
      json_object_get_type(gen) == json_type_object) {
    struct json_object *v = NULL, *op = NULL;
    float cc[3] = {0.7f, 0.7f, 0.7f};
    if (get_vec3(gen, "clearcolor", cc) != 0) {
      json_object_put(root);
      XW_ERROR_SET(err, 1, "general clearcolor must be [r, g, b]");
      return -1;
    }
    memcpy(out->camera.clearcolor, cc, sizeof(cc));
    if (json_object_object_get_ex(gen, "orthogonalprojection", &op) && op &&
        json_object_get_type(op) == json_type_object) {
      struct json_object *w = NULL, *h = NULL;
      if (json_object_object_get_ex(op, "width", &w) && w)
        out->camera.ortho_w = (float)json_object_get_int(w);
      if (json_object_object_get_ex(op, "height", &h) && h)
        out->camera.ortho_h = (float)json_object_get_int(h);
    }
    if (json_object_object_get_ex(gen, "bloom", &v) && v)
      out->camera.bloom = json_object_get_boolean(v) ? 1 : 0;
    if (json_object_object_get_ex(gen, "cameraparallax", &v) && v)
      out->camera.parallax = json_object_get_boolean(v) ? 1 : 0;
  } else if (json_object_object_get_ex(root, "camera", &cam) && cam &&
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
    if (get_vec3(cam, "clearcolor", out->camera.clearcolor) != 0) {
      json_object_put(root);
      XW_ERROR_SET(err, 1, "camera clearcolor must be [r, g, b]");
      return -1;
    }
    if (json_object_object_get_ex(cam, "bloom", &v) && v)
      out->camera.bloom = json_object_get_boolean(v) ? 1 : 0;
    if (json_object_object_get_ex(cam, "parallax", &v) && v)
      out->camera.parallax = json_object_get_boolean(v) ? 1 : 0;
  }
  struct json_object *sv = NULL;
  if (json_object_object_get_ex(root, "script", &sv) && sv &&
      json_object_get_type(sv) == json_type_string)
    copy_str(out->script, sizeof(out->script), json_object_get_string(sv));
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
      if (parse_object_at(dir, oj, &out->objects[i], err) != 0) goto fail;
      out->n_objects++;
    }
  }
  json_object_put(root);
  if (parse_project_props(dir, out, err) != 0) {
    xw_scene_free(out);
    return -1;
  }
  return 0;
fail:
  json_object_put(root);
  xw_scene_free(out);
  return -1;
#endif
}

static int parse_project_props(const char *dir, XWScene *out, XWError *err) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/project.json", dir);
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 1024 * 1024) {
    fclose(f);
    XW_ERROR_SET(err, 1, "bad project.json size");
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
    XW_ERROR_SET(err, 1, "cannot read project.json");
    return -1;
  }
  fclose(f);
  buf[sz] = '\0';
  struct json_object *root = json_tokener_parse(buf);
  free(buf);
  if (!root || json_object_get_type(root) != json_type_object) {
    if (root) json_object_put(root);
    XW_ERROR_SET(err, 1, "project.json is not a JSON object");
    return -1;
  }
  int rc = 0;
  struct json_object *gen = NULL, *props = NULL;
  if (json_object_object_get_ex(root, "general", &gen) && gen &&
      json_object_get_type(gen) == json_type_object &&
      json_object_object_get_ex(gen, "properties", &props) && props) {
    if (json_object_get_type(props) != json_type_object) {
      XW_ERROR_SET(err, 1, "properties must be an object");
      rc = -1;
    } else {
      size_t n = 0;
      json_object_object_foreach(props, key, val) {
        (void)key;
        (void)val;
        n++;
      }
      out->props = calloc(n ? n : 1, sizeof(XWUserProp));
      if (!out->props) {
        XW_ERROR_SET(err, 1, "out of memory");
        rc = -1;
      } else {
        json_object_object_foreach(props, key, val) {
          XWUserProp *p = &out->props[out->n_props++];
          copy_str(p->name, sizeof(p->name), key);
          if (json_object_get_type(val) == json_type_object) {
            struct json_object *t = NULL, *vv = NULL;
            if (json_object_object_get_ex(val, "type", &t) && t)
              copy_str(p->type, sizeof(p->type), json_object_get_string(t));
            if (json_object_object_get_ex(val, "value", &vv) && vv)
              copy_str(p->value, sizeof(p->value),
                       json_object_get_string(vv));
          } else {
            copy_str(p->value, sizeof(p->value), json_object_get_string(val));
          }
        }
      }
    }
  }
  json_object_put(root);
  return rc;
}

void xw_scene_free(XWScene *s) {
  if (!s) return;
  free(s->props);
  s->props = NULL;
  s->n_props = 0;
  if (!s->objects) {
    s->n_objects = 0;
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
