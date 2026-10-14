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
static void resolve_user_props(XWScene *s);
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

/* Emitter distances: the engine writes a vector ("1200 700 0") for
 * boxrandom and a single radius number for sphererandom. */
static void get_dist3(struct json_object *o, const char *key, float *v) {
  struct json_object *a = NULL;
  enum json_type t;
  if (!v || !json_object_object_get_ex(o, key, &a) || !a)
    return;
  t = json_object_get_type(a);
  if (t == json_type_string) {
    parse_vec_str(json_object_get_string(a), v, 3);
  } else if (t == json_type_double || t == json_type_int) {
    v[0] = v[1] = v[2] = (float)json_object_get_double(a);
  }
}

/* A "scripted property" wraps its base value as {"script": .., "script
 * properties": .., "value": <the vector>}; the engine evaluates the script
 * per frame, we apply the base value so the object still lands somewhere
 * sensible. */
static struct json_object *unwrap_scripted(struct json_object *a) {
  struct json_object *iv = NULL;
  if (a && json_object_get_type(a) == json_type_object &&
      json_object_object_get_ex(a, "value", &iv) && iv)
    return iv;
  return a;
}

static int get_vec2(struct json_object *o, const char *key, float *x, float *y,
                    float dx, float dy) {
  struct json_object *a = NULL;
  float v[2];
  *x = dx;
  *y = dy;
  if (!json_object_object_get_ex(o, key, &a) || !a) return 0;
  a = unwrap_scripted(a);
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
  a = unwrap_scripted(a);
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
  a = unwrap_scripted(a);
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
  if (json_object_object_get_ex(v, "user", &u) && u &&
      json_object_get_type(u) == json_type_string) {
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
  /* a "scripted" or "animated" property wraps its base value in `value`
   * (with `script` and/or `animation` alongside); the engine drives it per
   * frame, we apply the base so the object still loads. 2887099508 puts one
   * on almost every object. */
  {
    struct json_object *w = NULL;
    if (json_object_object_get_ex(v, "value", &w) && w &&
        (json_object_get_type(w) == json_type_double ||
         json_object_get_type(w) == json_type_int)) {
      d->constant = (float)json_object_get_double(w);
      return 0;
    }
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
  parse_mult(jo, "size", &o->particle.size_mult);
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
    parse_mult(w, "size", &o->particle.size_mult);
  }
  cfg = slurp_json(dir, json_object_get_string(v));
  if (!cfg)
    return;
  o->particle.dirs[0] = o->particle.dirs[1] = o->particle.dirs[2] = 1.0f;
  o->particle.drag = 0.0f;
  if (json_object_object_get_ex(cfg, "maxcount", &w) && w)
    o->particle.max_count = json_object_get_int(w);
  if (json_object_object_get_ex(cfg, "flags", &w) && w)
    o->particle.flags = json_object_get_int(w);
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
      if (json_object_object_get_ex(r0, "length", &rm) && rm)
        o->particle.trail_length = (float)json_object_get_double(rm);
      if (json_object_object_get_ex(r0, "minlength", &rm) && rm)
        o->particle.trail_min = (float)json_object_get_double(rm);
    }
  }
  if (o->particle.trail_max <= 0.0f)
    o->particle.trail_max = 30.0f;
  /* engine defaults for the trail renderer (CParticle's member defaults) */
  if (o->particle.trail_length <= 0.0f)
    o->particle.trail_length = 0.05f;
  o->particle.has_trail = strcmp(o->particle.renderer, "spritetrail") == 0;
  if (json_object_object_get_ex(cfg, "animationmode", &w) && w &&
      json_object_get_type(w) == json_type_string)
    copy_str(o->particle.animation_mode, sizeof(o->particle.animation_mode),
             json_object_get_string(w));
  o->particle.seq_mult = 1.0f;
  if (json_object_object_get_ex(cfg, "sequencemultiplier", &w) && w &&
      (json_object_get_type(w) == json_type_double ||
       json_object_get_type(w) == json_type_int))
    o->particle.seq_mult = (float)json_object_get_double(w);
  if (json_object_object_get_ex(cfg, "emitter", &w) && w &&
      json_object_get_type(w) == json_type_array &&
      json_object_array_length(w) >= 1) {
    struct json_object *e0 = json_object_array_get_idx(w, 0);
    struct json_object *r = NULL, *s3 = NULL;
    if (e0 && json_object_get_type(e0) == json_type_object) {
      struct json_object *nm = NULL;
      if (json_object_object_get_ex(e0, "rate", &r) && r)
        o->particle.rate = (float)json_object_get_double(r);
      if (json_object_object_get_ex(e0, "name", &nm) && nm &&
          json_object_get_type(nm) == json_type_string)
        o->particle.emitter_sphere =
            strcmp(json_object_get_string(nm), "sphererandom") == 0;
      if (json_object_object_get_ex(e0, "origin", &s3) && s3 &&
          json_object_get_type(s3) == json_type_string)
        parse_vec_str(json_object_get_string(s3), o->particle.origin, 3);
      get_dist3(e0, "distancemin", o->particle.dist_min);
      get_dist3(e0, "distancemax", o->particle.dist_max);
      if (json_object_object_get_ex(e0, "directions", &s3) && s3 &&
          json_object_get_type(s3) == json_type_string)
        parse_vec_str(json_object_get_string(s3), o->particle.dirs, 3);
      get_vec3(e0, "sign", o->particle.sign);
    }
  }
  /* the config's control point table: the attract operator adds its origin
   * to one of these offsets */
  if (json_object_object_get_ex(cfg, "controlpoint", &w) && w &&
      json_object_get_type(w) == json_type_array) {
    int ci;
    size_t cn = json_object_array_length(w);
    for (ci = 0; ci < (int)cn && o->particle.n_control_points < XW_PARTICLE_CP_MAX;
         ci++) {
      struct json_object *cp = json_object_array_get_idx(w, ci);
      struct json_object *id = NULL, *off = NULL;
      int slot = o->particle.n_control_points;
      if (!cp || json_object_get_type(cp) != json_type_object)
        continue;
      if (json_object_object_get_ex(cp, "id", &id) && id &&
          json_object_get_type(id) == json_type_int)
        slot = json_object_get_int(id);
      if (slot < 0 || slot >= XW_PARTICLE_CP_MAX)
        continue;
      if (json_object_object_get_ex(cp, "offset", &off) && off &&
          json_object_get_type(off) == json_type_string)
        parse_vec_str(json_object_get_string(off),
                      o->particle.cp_offsets[slot], 3);
      if (slot + 1 > o->particle.n_control_points)
        o->particle.n_control_points = slot + 1;
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
        struct json_object *lo = NULL, *hi = NULL, *ex = NULL;
        if (json_object_object_get_ex(it, "min", &lo) && lo)
          o->particle.size_min = (float)json_object_get_double(lo);
        if (json_object_object_get_ex(it, "max", &hi) && hi)
          o->particle.size_max = (float)json_object_get_double(hi);
        /* the engine shapes the random draw with this exponent */
        if (json_object_object_get_ex(it, "exponent", &ex) && ex)
          o->particle.size_exponent = (float)json_object_get_double(ex);
      } else if (strcmp(name, "alpharandom") == 0) {
        struct json_object *lo = NULL, *hi = NULL;
        if (json_object_object_get_ex(it, "min", &lo) && lo)
          o->particle.alpha_min = (float)json_object_get_double(lo);
        if (json_object_object_get_ex(it, "max", &hi) && hi)
          o->particle.alpha_max = (float)json_object_get_double(hi);
      } else if (strcmp(name, "colorrandom") == 0) {
        struct json_object *mn = NULL, *mx = NULL;
        float lo[3] = {1, 1, 1}, hi[3] = {1, 1, 1};
        if (json_object_object_get_ex(it, "min", &mn) && mn &&
            json_object_get_type(mn) == json_type_string)
          parse_vec_str(json_object_get_string(mn), lo, 3);
        if (json_object_object_get_ex(it, "max", &mx) && mx &&
            json_object_get_type(mx) == json_type_string)
          parse_vec_str(json_object_get_string(mx), hi, 3);
        memcpy(o->particle.color_min, lo, sizeof(lo));
        memcpy(o->particle.color_max, hi, sizeof(hi));
        o->particle.has_color_random = 1;
      } else if (strcmp(name, "rotationrandom") == 0) {
        struct json_object *mn = NULL, *mx = NULL;
        float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        if (json_object_object_get_ex(it, "min", &mn) && mn &&
            json_object_get_type(mn) == json_type_string)
          parse_vec_str(json_object_get_string(mn), lo, 3);
        if (json_object_object_get_ex(it, "max", &mx) && mx &&
            json_object_get_type(mx) == json_type_string)
          parse_vec_str(json_object_get_string(mx), hi, 3);
        /* 2D sprites roll in the Z axis */
        o->particle.rot_min = lo[2];
        o->particle.rot_max = hi[2];
      } else if (strcmp(name, "angularvelocityrandom") == 0) {
        struct json_object *ex = NULL;
        float lo[3] = {0.0f, 0.0f, -5.0f}, hi[3] = {0.0f, 0.0f, 5.0f};
        o->particle.has_av_random = 1;
        o->particle.av_exponent = 1.0f;
        get_vec3(it, "min", lo);
        get_vec3(it, "max", hi);
        if (json_object_object_get_ex(it, "exponent", &ex) && ex)
          o->particle.av_exponent = (float)json_object_get_double(ex);
        /* the engine spins a vec3; our quad only uses the in-plane Z axis */
        o->particle.av_min = lo[2];
        o->particle.av_max = hi[2];
      } else if (strcmp(name, "lifetimerandom") == 0) {
        struct json_object *lo = NULL, *hi = NULL;
        float lmin = 1.0f, lmax = 1.0f;
        if (json_object_object_get_ex(it, "min", &lo) && lo)
          lmin = (float)json_object_get_double(lo);
        if (json_object_object_get_ex(it, "max", &hi) && hi)
          lmax = (float)json_object_get_double(hi);
        o->particle.life = (lmin + lmax) / 2.0f;
      } else if (strcmp(name, "turbulentvelocityrandom") == 0) {
        struct json_object *sv = NULL;
        float vec[3];
        o->particle.has_turb = 1;
        o->particle.turb_speed_min = 0.0f;
        o->particle.turb_speed_max = 1.0f;
        o->particle.turb_scale = 2.0f;
        if (json_object_object_get_ex(it, "speedmin", &sv) && sv)
          o->particle.turb_speed_min = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "speedmax", &sv) && sv)
          o->particle.turb_speed_max = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "offset", &sv) && sv)
          o->particle.turb_offset = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "scale", &sv) && sv)
          o->particle.turb_scale = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "timescale", &sv) && sv)
          o->particle.turb_time_scale = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "phasemin", &sv) && sv)
          o->particle.turb_phase_min = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "phasemax", &sv) && sv)
          o->particle.turb_phase_max = (float)json_object_get_double(sv);
        if (json_object_object_get_ex(it, "forward", &sv) && sv &&
            json_object_get_type(sv) == json_type_string &&
            parse_vec_str(json_object_get_string(sv), vec, 3) == 0)
          memcpy(o->particle.turb_forward, vec, sizeof(vec));
        if (json_object_object_get_ex(it, "right", &sv) && sv &&
            json_object_get_type(sv) == json_type_string &&
            parse_vec_str(json_object_get_string(sv), vec, 3) == 0)
          memcpy(o->particle.turb_right, vec, sizeof(vec));
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
      if (strcmp(json_object_get_string(nm), "alphafade") == 0) {
        struct json_object *fi = NULL, *fo = NULL;
        o->particle.has_alpha_fade = 1;
        /* the engine defaults both fades to 0.5 (ObjectParser) */
        o->particle.fade_in = 0.5f;
        o->particle.fade_out = 0.5f;
        if (json_object_object_get_ex(it, "fadeintime", &fi) && fi)
          o->particle.fade_in = (float)json_object_get_double(fi);
        if (json_object_object_get_ex(it, "fadeouttime", &fo) && fo)
          o->particle.fade_out = (float)json_object_get_double(fo);
        continue;
      }
      if (strcmp(json_object_get_string(nm), "movement") != 0) {
        /* oscillator operators: defaults from ObjectParser::parseOperator */
        const char *opname = json_object_get_string(nm);
        XWOscillate *osc = NULL;
        if (strcmp(opname, "oscillateposition") == 0) {
          osc = &o->particle.osc_pos;
          osc->freq_max = 5.0f;
          osc->scale_max = 10.0f;
          osc->mask[0] = 1.0f;
          osc->mask[1] = 1.0f;
        } else if (strcmp(opname, "oscillatealpha") == 0) {
          osc = &o->particle.osc_alpha;
          osc->freq_max = 10.0f;
          osc->scale_max = 1.0f;
        } else if (strcmp(opname, "oscillatesize") == 0) {
          osc = &o->particle.osc_size;
          osc->freq_max = 10.0f;
          osc->scale_min = 0.8f;
          osc->scale_max = 1.2f;
        } else if (strcmp(opname, "controlpointattract") == 0) {
          struct json_object *v = NULL;
          float ov[3] = {0.0f, 0.0f, 0.0f};
          o->particle.has_cp_attract = 1;
          /* ObjectParser defaults: controlpoint 0, origin (0,0,0),
           * scale 100, threshold 1000 */
          o->particle.cp_scale = 100.0f;
          o->particle.cp_threshold = 1000.0f;
          if (json_object_object_get_ex(it, "controlpoint", &v) && v &&
              json_object_get_type(v) == json_type_int)
            o->particle.cp_index = json_object_get_int(v);
          if (json_object_object_get_ex(it, "scale", &v) && v)
            o->particle.cp_scale = (float)json_object_get_double(v);
          if (json_object_object_get_ex(it, "threshold", &v) && v)
            o->particle.cp_threshold = (float)json_object_get_double(v);
          get_vec3(it, "origin", ov);
          memcpy(o->particle.cp_origin, ov, sizeof(ov));
          continue;
        } else if (strcmp(opname, "angularmovement") == 0) {
          struct json_object *v = NULL;
          float fv[3] = {0.0f, 0.0f, 0.0f};
          o->particle.has_angular_movement = 1;
          if (json_object_object_get_ex(it, "drag", &v) && v)
            o->particle.am_drag = (float)json_object_get_double(v);
          get_vec3(it, "force", fv);
          o->particle.am_force_z = fv[2];
          continue;
        }
        if (osc) {
          struct json_object *v = NULL;
          osc->present = 1;
          osc->phase_max = 6.28318530718f;
          if (json_object_object_get_ex(it, "frequencymin", &v) && v)
            osc->freq_min = (float)json_object_get_double(v);
          if (json_object_object_get_ex(it, "frequencymax", &v) && v)
            osc->freq_max = (float)json_object_get_double(v);
          if (json_object_object_get_ex(it, "scalemin", &v) && v)
            osc->scale_min = (float)json_object_get_double(v);
          if (json_object_object_get_ex(it, "scalemax", &v) && v)
            osc->scale_max = (float)json_object_get_double(v);
          if (json_object_object_get_ex(it, "phasemin", &v) && v)
            osc->phase_min = (float)json_object_get_double(v);
          if (json_object_object_get_ex(it, "phasemax", &v) && v)
            osc->phase_max = (float)json_object_get_double(v);
          if (strcmp(opname, "oscillateposition") == 0)
            get_vec3(it, "mask", osc->mask);
        }
        continue;
      }
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
      if (m0 && json_object_get_type(m0) == json_type_object) {
        struct json_object *sv = NULL, *cv = NULL, *kv = NULL;
        size_t ti;
        if (json_object_object_get_ex(m0, "shader", &sv) && sv &&
            json_object_get_type(sv) == json_type_string)
          copy_str(o->particle.pshader, sizeof(o->particle.pshader),
                   json_object_get_string(sv));
        if (json_object_object_get_ex(m0, "blending", &sv) && sv &&
            json_object_get_type(sv) == json_type_string) {
          const char *bn = json_object_get_string(sv);
          if (strcmp(bn, "translucent") == 0)
            o->particle.pblend = XW_BLEND_TRANSLUCENT;
          else if (strcmp(bn, "additive") == 0)
            o->particle.pblend = XW_BLEND_ADDITIVE;
          else
            o->particle.pblend = XW_BLEND_NORMAL;
        }
        if (json_object_object_get_ex(m0, "textures", &tx) && tx &&
            json_object_get_type(tx) == json_type_array) {
          size_t tn = json_object_array_length(tx);
          for (ti = 0; ti < tn && ti < XW_PARTICLE_TEX_MAX; ti++) {
            struct json_object *t = json_object_array_get_idx(tx, ti);
            if (t && json_object_get_type(t) == json_type_string)
              copy_str(o->particle.ptextures[ti],
                       sizeof(o->particle.ptextures[0]),
                       json_object_get_string(t));
          }
          o->particle.n_ptextures = (int)(tn > XW_PARTICLE_TEX_MAX
                                              ? XW_PARTICLE_TEX_MAX
                                              : tn);
          if (o->particle.n_ptextures > 0 && o->particle.ptextures[0][0])
            copy_str(o->particle.sprite, sizeof(o->particle.sprite),
                     o->particle.ptextures[0]);
        }
        if (json_object_object_get_ex(m0, "combos", &cv) && cv &&
            json_object_get_type(cv) == json_type_object) {
          json_object_object_foreach(cv, ck, cval) {
            if (o->particle.n_pcombos >= XW_SCENE_COMBOS_MAX)
              break;
            copy_str(o->particle.pcombos[o->particle.n_pcombos].key,
                     sizeof(o->particle.pcombos[0].key), ck);
            copy_str(o->particle.pcombos[o->particle.n_pcombos].value,
                     sizeof(o->particle.pcombos[0].value),
                     json_object_get_string(cval));
            o->particle.n_pcombos++;
          }
        }
        if (json_object_object_get_ex(m0, "constantshadervalues", &kv) && kv &&
            json_object_get_type(kv) == json_type_object) {
          json_object_object_foreach(kv, kk, kval) {
            if (o->particle.n_pconsts >= XW_SCENE_CONSTS_MAX)
              break;
            copy_str(o->particle.pconsts[o->particle.n_pconsts].key,
                     sizeof(o->particle.pconsts[0].key), kk);
            copy_str(o->particle.pconsts[o->particle.n_pconsts].value,
                     sizeof(o->particle.pconsts[0].value),
                     json_object_get_string(kval));
            o->particle.n_pconsts++;
          }
        }
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
  /* `dependencies`: numeric ids the engine emits before this object; keep
   * them as strings so they compare directly against `id`. Parsed first
   * because the id/name lookup below returns early. */
  if (json_object_object_get_ex(jo, "dependencies", &v) && v &&
      json_object_get_type(v) == json_type_array) {
    int di;
    size_t dn = json_object_array_length(v);
    for (di = 0; di < (int)dn && o->n_deps < XW_SCENE_DEPS_MAX; di++) {
      struct json_object *e = json_object_array_get_idx(v, di);
      if (!e)
        continue;
      if (json_object_get_type(e) == json_type_int)
        snprintf(o->deps[o->n_deps], sizeof(o->deps[0]), "%d",
                 json_object_get_int(e));
      else if (json_object_get_type(e) == json_type_string)
        copy_str(o->deps[o->n_deps], sizeof(o->deps[0]),
                 json_object_get_string(e));
      else
        continue;
      o->n_deps++;
    }
  }
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
  /* engine defaults (ObjectParser::parseParticle / parseParticleEmitter):
   * an emitter with no rate still emits 10/s, a config with no maxcount
   * still pools 100 particles and an omitted animationmode is "sequence".
   * Without the first two the pool is empty and the system stays invisible. */
  o->particle.rate = 10.0f;
  o->particle.max_count = 100;
  copy_str(o->particle.animation_mode, sizeof(o->particle.animation_mode),
           "sequence");
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
    if (json_object_get_type(v) != json_type_object &&
        json_object_get_type(v) != json_type_string) {
      XW_ERROR_SET(err, 1, "text must be a string or a scripted object");
      return -1;
    }
    {
      struct json_object *e = NULL;
      /* the real format puts the content in `text` itself - either a plain
       * string or {"script": .., "value": <string>} driven per frame */
      if (json_object_get_type(v) == json_type_string) {
        copy_str(o->text.content, sizeof(o->text.content),
                 json_object_get_string(v));
      } else if (json_object_object_get_ex(v, "value", &e) && e &&
                 json_object_get_type(e) == json_type_string) {
        copy_str(o->text.content, sizeof(o->text.content),
                 json_object_get_string(e));
      }
      /* a scripted layer runs update(value) every frame to produce the text */
      if (json_object_object_get_ex(v, "script", &e) && e &&
          json_object_get_type(e) == json_type_string) {
        const char *src = json_object_get_string(e);
        if (src && src[0]) {
          o->text.script = strdup(src);
          if (!o->text.script) {
            XW_ERROR_SET(err, 1, "out of memory");
            return -1;
          }
        }
      }
      /* font, point size and colour are siblings of `text` */
      if (json_object_object_get_ex(jo, "font", &e) && e &&
          json_object_get_type(e) == json_type_string)
        copy_str(o->text.font, sizeof(o->text.font),
                 json_object_get_string(e));
      if (json_object_object_get_ex(jo, "pointsize", &e) && e &&
          (json_object_get_type(e) == json_type_double ||
           json_object_get_type(e) == json_type_int))
        o->text.size = (float)json_object_get_double(e);
      o->text.color[0] = 1.0f;
      o->text.color[1] = 1.0f;
      o->text.color[2] = 1.0f;
      o->text.color[3] = 1.0f;
      /* a colour may be a {user,value} dict; it is an RGB triple there */
      {
        float c3[3];
        if (get_vec3(jo, "color", c3) == 0) {
          o->text.color[0] = c3[0];
          o->text.color[1] = c3[1];
          o->text.color[2] = c3[2];
        }
      }
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
  /* perspective parameters for flags & 4 particles (engine defaults) */
  out->camera.fov = 50.0f;
  out->camera.nearz = 0.01f;
  out->camera.farz = 10000.0f;
  if (json_object_object_get_ex(root, "general", &gen) && gen &&
      json_object_get_type(gen) == json_type_object) {
    struct json_object *v = NULL, *op = NULL;
    float cc[3] = {0.7f, 0.7f, 0.7f};
    {
      struct json_object *fv = NULL;
      if (json_object_object_get_ex(gen, "fov", &fv) && fv)
        out->camera.fov = (float)json_object_get_double(fv);
      if (json_object_object_get_ex(gen, "nearz", &fv) && fv)
        out->camera.nearz = (float)json_object_get_double(fv);
      if (json_object_object_get_ex(gen, "farz", &fv) && fv)
        out->camera.farz = (float)json_object_get_double(fv);
    }
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
  resolve_user_props(out);
  return 0;
fail:
  json_object_put(root);
  xw_scene_free(out);
  return -1;
#endif
}

/* A wallpaper's `{user, value}` settings resolve against the properties it
 * declares in project.json: the property's value wins, and the inline value
 * is only the fallback for a property the wallpaper does not declare. */
static float prop_float(const XWScene *s, const char *name, float fallback) {
  int i;
  for (i = 0; i < s->n_props; i++)
    if (strcmp(s->props[i].name, name) == 0 && s->props[i].value[0])
      return (float)strtod(s->props[i].value, NULL);
  return fallback;
}

static void resolve_user(XWDynFloat *d, const XWScene *s) {
  if (d->kind != XW_DYN_USER || !d->ref[0])
    return;
  d->constant = prop_float(s, d->ref, d->constant);
  d->kind = XW_DYN_CONSTANT;
}

static void resolve_user_props(XWScene *s) {
  int i;
  for (i = 0; i < s->n_objects; i++) {
    XWSceneObject *o = &s->objects[i];
    /* alpha keeps its XW_DYN_USER form: the script layer resolves it against
     * the live property store */
    resolve_user(&o->particle.count_mult, s);
    resolve_user(&o->particle.life_mult, s);
    resolve_user(&o->particle.speed_mult, s);
    resolve_user(&o->particle.size_mult, s);
  }
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
    free(o->text.script);
    o->text.script = NULL;
    if (!o->effects) continue;
    for (int e = 0; e < o->n_effects; e++) free(o->effects[e].passes);
    free(o->effects);
  }
  free(s->objects);
  s->objects = NULL;
  s->n_objects = 0;
}
