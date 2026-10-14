#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* vendored-header noise stays out of -Wall -Wextra */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#include "quickjs.h"
#pragma GCC diagnostic pop

#include "scene_script.h"

typedef struct XWTimer {
  int id;
  int repeat;
  int64_t period_ms;
  uint64_t due;
  JSValue func;
} XWTimer;

/* one runner per scripted property per object, built lazily */
typedef struct XWObjPropRunners {
  XWPropScript *p[XW_PROP_COUNT];
} XWObjPropRunners;

struct XWScript {
  JSRuntime *rt;
  JSContext *ctx;
  XWTimer *timers;
  int ntimers;
  int cap;
  int next_id;
  uint64_t t0_ms;
  int64_t adjust_ms;
  char last_error[256];
  XWScene *scene;
  const XWUserProp *props;
  int n_props;
  struct {
    char name[64];
    char value[256];
  } *overrides;
  int noverrides;
  int override_cap;
  float mouse_x, mouse_y;
  XWObjPropRunners *objr;
  int objr_n;
  int *order;
  int n_order;
};

static uint64_t mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static XWScript *ctx_script(JSContext *ctx) {
  return (XWScript *)JS_GetContextOpaque(ctx);
}

static int push_timer(XWScript *s, JSValue func, int64_t delay_ms,
                      int repeat) {
  XWTimer *nt;
  if (s->ntimers >= s->cap) {
    int ncap = s->cap ? s->cap * 2 : 8;
    nt = realloc(s->timers, (size_t)ncap * sizeof(XWTimer));
    if (!nt)
      return -1;
    s->timers = nt;
    s->cap = ncap;
  }
  if (delay_ms < 0)
    delay_ms = 0;
  s->timers[s->ntimers].id = s->next_id++;
  s->timers[s->ntimers].repeat = repeat;
  s->timers[s->ntimers].period_ms = delay_ms;
  s->timers[s->ntimers].due = mono_ms() + (uint64_t)delay_ms;
  s->timers[s->ntimers].func = func;
  s->ntimers++;
  return s->timers[s->ntimers - 1].id;
}

static JSValue js_set_timer(JSContext *ctx, JSValueConst this_val, int argc,
                            JSValueConst *argv, int repeat) {
  XWScript *s = ctx_script(ctx);
  double delay = 0.0;
  int64_t ms;
  int id;
  (void)this_val;
  if (!s || argc < 1 || !JS_IsFunction(ctx, argv[0]))
    return JS_ThrowTypeError(ctx, "first argument must be a function");
  if (argc >= 2)
    JS_ToFloat64(ctx, &delay, argv[1]);
  if (isnan(delay) || delay < 0.0)
    delay = 0.0;
  ms = (int64_t)delay;
  id = push_timer(s, JS_DupValue(ctx, argv[0]), ms, repeat);
  if (id < 0)
    return JS_ThrowOutOfMemory(ctx);
  return JS_NewInt32(ctx, id);
}

static JSValue js_set_timeout(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  return js_set_timer(ctx, this_val, argc, argv, 0);
}

static JSValue js_set_interval(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  return js_set_timer(ctx, this_val, argc, argv, 1);
}

static JSValue js_clear_timer(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  int32_t id = 0;
  (void)this_val;
  if (s && argc >= 1 && JS_ToInt32(ctx, &id, argv[0]) == 0) {
    for (int i = 0; i < s->ntimers; i++) {
      if (s->timers[i].id == id) {
        JS_FreeValue(ctx, s->timers[i].func);
        s->ntimers--;
        if (i != s->ntimers)
          s->timers[i] = s->timers[s->ntimers];
        break;
      }
    }
  }
  return JS_UNDEFINED;
}

static JSValue js_get_time(JSContext *ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  (void)this_val;
  (void)argc;
  (void)argv;
  if (!s)
    return JS_NewInt32(ctx, 0);
  return JS_NewInt64(ctx, (int64_t)(mono_ms() - s->t0_ms) + s->adjust_ms);
}

static JSValue js_set_time(JSContext *ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  double v = 0.0;
  (void)this_val;
  if (s && argc >= 1 && JS_ToFloat64(ctx, &v, argv[0]) == 0 && !isnan(v))
    s->adjust_ms = (int64_t)v - (int64_t)(mono_ms() - s->t0_ms);
  return JS_UNDEFINED;
}

static XWSceneObject *find_object(XWScript *s, JSValueConst idv) {
  const char *id;
  XWSceneObject *found = NULL;
  if (!s || !s->scene || !JS_IsString(idv))
    return NULL;
  id = JS_ToCString(s->ctx, idv);
  if (!id)
    return NULL;
  for (int i = 0; i < s->scene->n_objects; i++) {
    if (strcmp(s->scene->objects[i].id, id) == 0) {
      found = &s->scene->objects[i];
      break;
    }
  }
  JS_FreeCString(s->ctx, id);
  return found;
}

static int set_override(XWScript *s, const char *name, const char *value) {
  for (int i = 0; i < s->noverrides; i++) {
    if (strcmp(s->overrides[i].name, name) == 0) {
      snprintf(s->overrides[i].value, sizeof(s->overrides[i].value), "%s",
               value);
      return 0;
    }
  }
  if (s->noverrides >= s->override_cap) {
    int ncap = s->override_cap ? s->override_cap * 2 : 8;
    void *no = realloc(s->overrides, (size_t)ncap * sizeof(*s->overrides));
    if (!no)
      return -1;
    s->overrides = no;
    s->override_cap = ncap;
  }
  snprintf(s->overrides[s->noverrides].name,
           sizeof(s->overrides[s->noverrides].name), "%s", name);
  snprintf(s->overrides[s->noverrides].value,
           sizeof(s->overrides[s->noverrides].value), "%s", value);
  s->noverrides++;
  return 0;
}

static const char *lookup_prop(XWScript *s, const char *name) {
  for (int i = 0; i < s->noverrides; i++)
    if (strcmp(s->overrides[i].name, name) == 0)
      return s->overrides[i].value;
  for (int i = 0; i < s->n_props; i++)
    if (strcmp(s->props[i].name, name) == 0)
      return s->props[i].value;
  return NULL;
}

static JSValue prop_to_js(JSContext *ctx, const char *value) {
  char *end;
  double d;
  if (strcmp(value, "true") == 0)
    return JS_NewBool(ctx, 1);
  if (strcmp(value, "false") == 0)
    return JS_NewBool(ctx, 0);
  d = strtod(value, &end);
  if (end != value && *end == '\0' && !isnan(d))
    return JS_NewFloat64(ctx, d);
  return JS_NewString(ctx, value);
}

static JSValue require_object(JSContext *ctx, XWScript *s, JSValueConst idv,
                              XWSceneObject **out) {
  const char *id;
  *out = find_object(s, idv);
  if (*out)
    return JS_UNDEFINED;
  if (JS_IsString(idv)) {
    id = JS_ToCString(ctx, idv);
    JSValue ex = JS_ThrowReferenceError(ctx, "unknown scene object '%s'",
                                        id ? id : "?");
    if (id)
      JS_FreeCString(ctx, id);
    return ex;
  }
  return JS_ThrowTypeError(ctx, "object id must be a string");
}

static JSValue js_scene_get_count(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  (void)this_val;
  (void)argc;
  (void)argv;
  return JS_NewInt32(ctx, s && s->scene ? s->scene->n_objects : 0);
}

static JSValue js_scene_get_alpha(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "object id required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  return JS_NewFloat64(ctx, xw_script_resolve_alpha(s, o));
}

static JSValue js_scene_set_alpha(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  double v = 1.0;
  char num[64];
  (void)this_val;
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "object id and alpha required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  if (JS_ToFloat64(ctx, &v, argv[1]) != 0 || isnan(v))
    return JS_ThrowTypeError(ctx, "alpha must be a number");
  if (o->alpha.kind == XW_DYN_USER && o->alpha.ref[0]) {
    snprintf(num, sizeof(num), "%g", v);
    if (set_override(s, o->alpha.ref, num) != 0)
      return JS_ThrowOutOfMemory(ctx);
  } else {
    o->alpha.kind = XW_DYN_CONSTANT;
    o->alpha.constant = (float)v;
  }
  return JS_UNDEFINED;
}

static JSValue js_vec2(JSContext *ctx, float x, float y) {
  JSValue a = JS_NewArray(ctx);
  JS_SetPropertyUint32(ctx, a, 0, JS_NewFloat64(ctx, x));
  JS_SetPropertyUint32(ctx, a, 1, JS_NewFloat64(ctx, y));
  return a;
}

static JSValue js_scene_get_origin(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "object id required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  return js_vec2(ctx, o->origin[0], o->origin[1]);
}

static JSValue js_scene_set_origin(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  double x, y;
  (void)this_val;
  if (argc < 3)
    return JS_ThrowTypeError(ctx, "object id, x and y required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  if (JS_ToFloat64(ctx, &x, argv[1]) != 0 ||
      JS_ToFloat64(ctx, &y, argv[2]) != 0 || isnan(x) || isnan(y))
    return JS_ThrowTypeError(ctx, "x and y must be numbers");
  o->origin[0] = (float)x;
  o->origin[1] = (float)y;
  return JS_UNDEFINED;
}

static JSValue js_scene_get_scale(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "object id required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  return js_vec2(ctx, o->scale[0], o->scale[1]);
}

static JSValue js_scene_set_scale(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  double x, y;
  (void)this_val;
  if (argc < 3)
    return JS_ThrowTypeError(ctx, "object id, sx and sy required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  if (JS_ToFloat64(ctx, &x, argv[1]) != 0 ||
      JS_ToFloat64(ctx, &y, argv[2]) != 0 || isnan(x) || isnan(y))
    return JS_ThrowTypeError(ctx, "sx and sy must be numbers");
  o->scale[0] = (float)x;
  o->scale[1] = (float)y;
  return JS_UNDEFINED;
}

static JSValue js_scene_set_visible(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  (void)this_val;
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "object id and flag required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  o->visible = JS_ToBool(ctx, argv[1]) ? 1 : 0;
  return JS_UNDEFINED;
}

static JSValue js_scene_is_visible(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  XWSceneObject *o;
  JSValue need;
  (void)this_val;
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "object id required");
  need = require_object(ctx, s, argv[0], &o);
  if (!o)
    return need;
  JS_FreeValue(ctx, need);
  return JS_NewBool(ctx, o->visible);
}

static JSValue js_scene_get_property(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  const char *name, *value;
  JSValue out;
  (void)this_val;
  if (!s || argc < 1 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "property name required");
  name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_ThrowOutOfMemory(ctx);
  value = lookup_prop(s, name);
  JS_FreeCString(ctx, name);
  if (!value)
    return JS_UNDEFINED;
  out = prop_to_js(ctx, value);
  return out;
}

static JSValue js_scene_set_property(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  const char *name;
  char value[256];
  double d;
  int32_t b;
  (void)this_val;
  if (!s || argc < 2 || !JS_IsString(argv[0]))
    return JS_ThrowTypeError(ctx, "property name and value required");
  name = JS_ToCString(ctx, argv[0]);
  if (!name)
    return JS_ThrowOutOfMemory(ctx);
  if (JS_IsBool(argv[1])) {
    b = JS_ToBool(ctx, argv[1]);
    snprintf(value, sizeof(value), "%s", b ? "true" : "false");
  } else if (!JS_IsString(argv[1]) &&
             JS_ToFloat64(ctx, &d, argv[1]) == 0 && !isnan(d)) {
    snprintf(value, sizeof(value), "%g", d);
  } else {
    const char *raw = JS_ToCString(ctx, argv[1]);
    snprintf(value, sizeof(value), "%s", raw ? raw : "");
    if (raw)
      JS_FreeCString(ctx, raw);
  }
  {
    /* copy name: ToCString buffer dies with FreeCString */
    char key[64];
    snprintf(key, sizeof(key), "%s", name);
    JS_FreeCString(ctx, name);
    if (set_override(s, key, value) != 0)
      return JS_ThrowOutOfMemory(ctx);
  }
  return JS_UNDEFINED;
}

static JSValue js_input_get_x(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  (void)this_val;
  (void)argc;
  (void)argv;
  return JS_NewFloat64(ctx, s ? s->mouse_x : 0.0);
}

static JSValue js_input_get_y(JSContext *ctx, JSValueConst this_val, int argc,
                              JSValueConst *argv) {
  XWScript *s = ctx_script(ctx);
  (void)this_val;
  (void)argc;
  (void)argv;
  return JS_NewFloat64(ctx, s ? s->mouse_y : 0.0);
}

static void add_fn(JSContext *ctx, JSValue obj, const char *name,
                   JSCFunction *func, int len) {
  JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, func, name, len));
}

int xw_script_create(XWScript **out, XWError *err) {
  XWScript *s;
  JSValue global;
  if (!out) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  *out = NULL;
  s = calloc(1, sizeof(*s));
  if (!s) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  s->rt = JS_NewRuntime();
  if (!s->rt) {
    free(s);
    XW_ERROR_SET(err, 1, "cannot create JS runtime");
    return -1;
  }
  s->ctx = JS_NewContext(s->rt);
  if (!s->ctx) {
    JS_FreeRuntime(s->rt);
    free(s);
    XW_ERROR_SET(err, 1, "cannot create JS context");
    return -1;
  }
  JS_SetContextOpaque(s->ctx, s);
  s->t0_ms = mono_ms();
  s->next_id = 1;
  global = JS_GetGlobalObject(s->ctx);
  JS_SetPropertyStr(s->ctx, global, "setTimeout",
                    JS_NewCFunction(s->ctx, js_set_timeout, "setTimeout", 2));
  JS_SetPropertyStr(s->ctx, global, "setInterval",
                    JS_NewCFunction(s->ctx, js_set_interval, "setInterval", 2));
  JS_SetPropertyStr(s->ctx, global, "clearTimeout",
                    JS_NewCFunction(s->ctx, js_clear_timer, "clearTimeout", 1));
  JS_SetPropertyStr(s->ctx, global, "clearInterval",
                    JS_NewCFunction(s->ctx, js_clear_timer, "clearInterval", 1));
  JS_SetPropertyStr(s->ctx, global, "getTime",
                    JS_NewCFunction(s->ctx, js_get_time, "getTime", 0));
  JS_SetPropertyStr(s->ctx, global, "setTime",
                    JS_NewCFunction(s->ctx, js_set_time, "setTime", 1));
  {
    JSValue scene = JS_NewObject(s->ctx);
    add_fn(s->ctx, scene, "getObjectCount", js_scene_get_count, 0);
    add_fn(s->ctx, scene, "getAlpha", js_scene_get_alpha, 1);
    add_fn(s->ctx, scene, "setAlpha", js_scene_set_alpha, 2);
    add_fn(s->ctx, scene, "getOrigin", js_scene_get_origin, 1);
    add_fn(s->ctx, scene, "setOrigin", js_scene_set_origin, 3);
    add_fn(s->ctx, scene, "getScale", js_scene_get_scale, 1);
    add_fn(s->ctx, scene, "setScale", js_scene_set_scale, 3);
    add_fn(s->ctx, scene, "setVisible", js_scene_set_visible, 2);
    add_fn(s->ctx, scene, "isVisible", js_scene_is_visible, 1);
    add_fn(s->ctx, scene, "getProperty", js_scene_get_property, 1);
    add_fn(s->ctx, scene, "setProperty", js_scene_set_property, 2);
    JS_SetPropertyStr(s->ctx, global, "scene", scene);
  }
  {
    JSValue input = JS_NewObject(s->ctx);
    add_fn(s->ctx, input, "getMouseX", js_input_get_x, 0);
    add_fn(s->ctx, input, "getMouseY", js_input_get_y, 0);
    JS_SetPropertyStr(s->ctx, global, "input", input);
  }
  JS_FreeValue(s->ctx, global);
  *out = s;
  return 0;
}

void xw_script_destroy(XWScript *s) {
  if (!s)
    return;
  for (int i = 0; i < s->ntimers; i++) JS_FreeValue(s->ctx, s->timers[i].func);
  free(s->timers);
  if (s->objr) {
    for (int i = 0; i < s->objr_n; i++)
      for (int k = 0; k < XW_PROP_COUNT; k++)
        xw_script_prop_free(s->objr[i].p[k]);
    free(s->objr);
  }
  free(s->overrides);
  JS_FreeContext(s->ctx);
  JS_FreeRuntime(s->rt);
  free(s);
}

static int take_exception(XWScript *s, XWError *err) {
  JSValue ex = JS_GetException(s->ctx);
  const char *msg = JS_ToCString(s->ctx, ex);
  XW_ERROR_SET(err, 1, "js: %.200s", msg ? msg : "?");
  if (msg)
    JS_FreeCString(s->ctx, msg);
  JS_FreeValue(s->ctx, ex);
  return -1;
}

int xw_script_eval(XWScript *s, const char *src, const char *filename,
                   XWError *err) {
  JSValue v;
  if (!s || !src) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  v = JS_Eval(s->ctx, src, strlen(src), filename ? filename : "<eval>",
              JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(v))
    return take_exception(s, err);
  JS_FreeValue(s->ctx, v);
  return 0;
}

int xw_script_get_number(XWScript *s, const char *name, double *out) {
  JSValue global, v;
  double d;
  if (!s || !name || !out)
    return -1;
  global = JS_GetGlobalObject(s->ctx);
  v = JS_GetPropertyStr(s->ctx, global, name);
  JS_FreeValue(s->ctx, global);
  if (JS_IsException(v)) {
    JS_FreeValue(s->ctx, v);
    return -1;
  }
  if (JS_ToFloat64(s->ctx, &d, v) != 0 || isnan(d)) {
    JS_FreeValue(s->ctx, v);
    return -1;
  }
  JS_FreeValue(s->ctx, v);
  *out = d;
  return 0;
}

void xw_script_tick(XWScript *s) {
  JSContext *pctx;
  uint64_t now;
  if (!s)
    return;
  now = mono_ms();
  for (int i = 0; i < s->ntimers;) {
    if (now < s->timers[i].due) {
      i++;
      continue;
    }
    {
      JSValue global = JS_GetGlobalObject(s->ctx);
      JSValue r = JS_Call(s->ctx, s->timers[i].func, global, 0, NULL);
      JS_FreeValue(s->ctx, global);
      if (JS_IsException(r)) {
        JSValue ex = JS_GetException(s->ctx);
        const char *msg = JS_ToCString(s->ctx, ex);
        snprintf(s->last_error, sizeof(s->last_error), "%s",
                 msg ? msg : "?");
        if (msg)
          JS_FreeCString(s->ctx, msg);
        JS_FreeValue(s->ctx, ex);
      }
      JS_FreeValue(s->ctx, r);
    }
    if (s->timers[i].repeat) {
      s->timers[i].due = now + (uint64_t)s->timers[i].period_ms;
      i++;
    } else {
      JS_FreeValue(s->ctx, s->timers[i].func);
      s->ntimers--;
      if (i != s->ntimers)
        s->timers[i] = s->timers[s->ntimers];
    }
  }
  while (JS_ExecutePendingJob(s->rt, &pctx) > 0)
    ;
}

uint64_t xw_script_now_ms(const XWScript *s) {
  int64_t t;
  if (!s)
    return 0;
  t = (int64_t)(mono_ms() - s->t0_ms) + s->adjust_ms;
  return t < 0 ? 0 : (uint64_t)t;
}

const char *xw_script_last_error(const XWScript *s) {
  return s ? s->last_error : "";
}

int xw_script_set_scene(XWScript *s, XWScene *scene) {
  if (!s)
    return -1;
  if (s->scene != scene && s->objr) {
    for (int i = 0; i < s->objr_n; i++)
      for (int k = 0; k < XW_PROP_COUNT; k++)
        xw_script_prop_free(s->objr[i].p[k]);
    free(s->objr);
    s->objr = NULL;
    s->objr_n = 0;
  }
  s->scene = scene;
  return 0;
}

int xw_script_set_props(XWScript *s, const XWUserProp *props, int n) {
  if (!s || (n > 0 && !props))
    return -1;
  s->props = n > 0 ? props : NULL;
  s->n_props = n > 0 ? n : 0;
  return 0;
}

void xw_script_set_mouse(XWScript *s, float x, float y) {
  if (!s)
    return;
  s->mouse_x = x;
  s->mouse_y = y;
}

int xw_script_set_user_prop(XWScript *s, const char *name, const char *value) {
  if (!s || !name || !name[0] || !value)
    return -1;
  return set_override(s, name, value);
}

int xw_script_get_user_prop(XWScript *s, const char *name, char *value,
                            size_t n) {
  const char *v;
  if (!s || !name || !value || n == 0)
    return -1;
  v = lookup_prop(s, name);
  if (!v)
    return -1;
  snprintf(value, n, "%s", v);
  return 0;
}

float xw_script_resolve_alpha(XWScript *s, const XWSceneObject *o) {
  const char *v;
  char *end;
  double d;
  if (!o || o->alpha.kind == XW_DYN_UNSET)
    return 1.0f;
  if (!s || o->alpha.kind == XW_DYN_CONSTANT)
    return o->alpha.constant;
  if (o->alpha.kind == XW_DYN_EXPRESSION)
    return o->alpha.constant;
  if (!o->alpha.ref[0])
    return o->alpha.constant;
  v = lookup_prop(s, o->alpha.ref);
  if (!v)
    return o->alpha.constant;
  d = strtod(v, &end);
  if (end == v || *end != '\0' || isnan(d))
    return o->alpha.constant;
  return (float)d;
}

/* ------------------------------------------------------------------ */
/* Text layers                                                         */
/* ------------------------------------------------------------------ */
/* Wallpaper Engine text objects may carry a script whose `update(value)`
 * returns the layer's text every frame (clocks, counters). The scripts are
 * ES modules that only ever export declarations, so the `export ` keyword is
 * dropped before evaluation. createScriptProperties() is the builder those
 * scripts use to declare their own properties; its defaults become plain
 * properties on the returned object. */

int xw_script_get_string(XWScript *s, const char *name, char *out, size_t n) {
  JSValue global, v;
  const char *str;
  if (!s || !name || !out || n == 0)
    return -1;
  global = JS_GetGlobalObject(s->ctx);
  v = JS_GetPropertyStr(s->ctx, global, name);
  JS_FreeValue(s->ctx, global);
  if (JS_IsException(v)) {
    JS_FreeValue(s->ctx, v);
    return -1;
  }
  str = JS_ToCString(s->ctx, v);
  JS_FreeValue(s->ctx, v);
  if (!str)
    return -1;
  snprintf(out, n, "%s", str);
  JS_FreeCString(s->ctx, str);
  return 0;
}

struct XWTextLayer {
  XWScript *js;
  char text[XW_TEXT_LAYER_MAX];
};

static const char *text_layer_prelude =
    "var __layerProps = {};\n"
    "function createScriptProperties() {\n"
    "  var api = {};\n"
    "  function set(o) {\n"
    "    if (o && o.name) __layerProps[o.name] = o.value;\n"
    "    return api;\n"
    "  }\n"
    "  api.addCheckbox = set; api.addText = set; api.addCombo = set;\n"
    "  api.addSlider = set; api.addNumber = set; api.addColor = set;\n"
    "  api.finish = function () { return __layerProps; };\n"
    "  return api;\n"
    "}\n"
    "var shared = {};\n"
    "var console = { log: function () {}, warn: function () {},"
    " error: function () {} };\n";

/* Drop the `export ` keyword (token-wise: a declaration modifier, never part
 * of an identifier). */
static void strip_export(const char *src, char *out, size_t n) {
  size_t o = 0;
  const char *p = src;
  while (*p && o + 1 < n) {
    if (strncmp(p, "export", 6) == 0 &&
        (p[6] == ' ' || p[6] == '\t' || p[6] == '\n' || p[6] == '\r') &&
        (p == src || !(p[-1] == '_' ||
                       (p[-1] >= 'a' && p[-1] <= 'z') ||
                       (p[-1] >= 'A' && p[-1] <= 'Z') ||
                       (p[-1] >= '0' && p[-1] <= '9')))) {
      p += 6;
      while (*p == ' ' || *p == '\t')
        p++;
      continue;
    }
    out[o++] = *p++;
  }
  out[o] = '\0';
}

int xw_script_text_layer_create(XWTextLayer **out, const char *src,
                                const char *initial, XWError *err) {
  XWTextLayer *l;
  char *clean;
  size_t n;
  if (!out || !src) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  *out = NULL;
  l = calloc(1, sizeof(*l));
  if (!l) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  snprintf(l->text, sizeof(l->text), "%s", initial ? initial : "");
  if (xw_script_create(&l->js, err) != 0) {
    free(l);
    return -1;
  }
  if (xw_script_eval(l->js, text_layer_prelude, "layer-prelude.js", err) != 0) {
    xw_script_text_layer_free(l);
    return -1;
  }
  n = strlen(src) + 1;
  clean = malloc(n);
  if (!clean) {
    XW_ERROR_SET(err, 1, "out of memory");
    xw_script_text_layer_free(l);
    return -1;
  }
  strip_export(src, clean, n);
  if (xw_script_eval(l->js, clean, "textlayer.js", err) != 0) {
    free(clean);
    xw_script_text_layer_free(l);
    return -1;
  }
  free(clean);
  *out = l;
  return 0;
}

const char *xw_script_text_layer_update(XWTextLayer *l) {
  JSValue global, cur;
  const char *str;
  if (!l || !l->js)
    return "";
  global = JS_GetGlobalObject(l->js->ctx);
  JS_SetPropertyStr(l->js->ctx, global, "__layerText",
                    JS_NewString(l->js->ctx, l->text));
  JS_FreeValue(l->js->ctx, global);
  /* a script without update() (or one that throws) keeps the last text */
  if (xw_script_eval(l->js,
                     "if (typeof update === 'function') {\n"
                     "  var __o = update(__layerText);\n"
                     "  if (typeof __o === 'string') __layerText = __o;\n"
                     "}\n",
                     "textlayer-update.js", NULL) != 0)
    return l->text;
  global = JS_GetGlobalObject(l->js->ctx);
  cur = JS_GetPropertyStr(l->js->ctx, global, "__layerText");
  JS_FreeValue(l->js->ctx, global);
  str = JS_ToCString(l->js->ctx, cur);
  JS_FreeValue(l->js->ctx, cur);
  if (str) {
    snprintf(l->text, sizeof(l->text), "%s", str);
    JS_FreeCString(l->js->ctx, str);
  }
  return l->text;
}

void xw_script_text_layer_free(XWTextLayer *l) {
  if (!l)
    return;
  xw_script_destroy(l->js);
  free(l);
}

/* ------------------------------------------------------------------ */
/* Scripted object properties                                          */
/* ------------------------------------------------------------------ */

struct XWPropScript {
  XWScript *js;
  XWScene *scene; /* borrowed */
  int *order;     /* borrowed render order the script may reorder */
  int n_order;
  int owner; /* index of the object the script belongs to, -1 when unknown */
};

/* thisScene.getLayer accepts a layer name or its index. The handle is a plain
 * object carrying the layer's index, so unknown property writes are harmless
 * (the engine's layer objects expose far more than we model). */
static int layer_index_from(JSContext *ctx, JSValueConst v, int n_objects) {
  JSValue idx;
  int32_t i = -1;
  if (JS_IsObject(v)) {
    idx = JS_GetPropertyStr(ctx, v, "__idx");
    if (!JS_IsUndefined(idx) && JS_ToInt32(ctx, &i, idx) != 0)
      i = -1;
    JS_FreeValue(ctx, idx);
  } else if (JS_IsNumber(v)) {
    if (JS_ToInt32(ctx, &i, v) != 0)
      i = -1;
  }
  if (i < 0 || i >= n_objects)
    return -1;
  return (int)i;
}

static JSValue layer_handle(JSContext *ctx, XWScene *scene, int i) {
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "__idx", JS_NewInt32(ctx, i));
  if (scene && i >= 0 && i < scene->n_objects)
    JS_SetPropertyStr(ctx, o, "name",
                      JS_NewString(ctx, scene->objects[i].id));
  return o;
}

static JSValue scene_get_layer(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  (void)this_val;
  XWPropScript *p = JS_GetContextOpaque(ctx);
  XWScene *sc;
  int i;
  if (!p || !p->scene || argc != 1)
    return JS_UNDEFINED;
  sc = p->scene;
  if (JS_IsString(argv[0])) {
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name)
      return JS_UNDEFINED;
    for (i = 0; i < sc->n_objects; i++)
      if (strcmp(sc->objects[i].id, name) == 0) {
        JS_FreeCString(ctx, name);
        return layer_handle(ctx, sc, i);
      }
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
  }
  i = layer_index_from(ctx, argv[0], sc->n_objects);
  if (i < 0)
    return JS_UNDEFINED;
  return layer_handle(ctx, sc, i);
}

static JSValue scene_get_layer_index(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  (void)this_val;
  XWPropScript *p = JS_GetContextOpaque(ctx);
  int i, k, *order, n;
  if (!p || !p->scene || argc != 1)
    return JS_NewInt32(ctx, -1);
  order = p->scene->order;
  n = p->scene->n_order;
  if (!order || n <= 0)
    return JS_NewInt32(ctx, -1);
  i = layer_index_from(ctx, argv[0], p->scene->n_objects);
  if (i < 0)
    return JS_NewInt32(ctx, -1);
  for (k = 0; k < n; k++)
    if (order[k] == i)
      return JS_NewInt32(ctx, k);
  return JS_NewInt32(ctx, -1);
}

static JSValue scene_sort_layer(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
  (void)this_val;
  XWPropScript *p = JS_GetContextOpaque(ctx);
  int i, at, from, k, *order, n;
  if (!p || !p->scene || argc != 2)
    return JS_UNDEFINED;
  order = p->scene->order;
  n = p->scene->n_order;
  if (!order || n <= 0)
    return JS_UNDEFINED;
  i = layer_index_from(ctx, argv[0], p->scene->n_objects);
  if (JS_ToInt32(ctx, &at, argv[1]) != 0)
    return JS_UNDEFINED;
  if (i < 0)
    return JS_UNDEFINED;
  if (at < 0)
    at = 0;
  if (at >= n)
    at = n - 1;
  from = -1;
  for (k = 0; k < n; k++)
    if (order[k] == i)
      from = k;
  if (from < 0 || from == at)
    return JS_UNDEFINED;
  if (from < at)
    for (k = from; k < at; k++)
      order[k] = order[k + 1];
  else
    for (k = from; k > at; k--)
      order[k] = order[k - 1];
  order[at] = i;
  return JS_UNDEFINED;
}

static JSValue scene_layer_count(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  XWPropScript *p = JS_GetContextOpaque(ctx);
  (void)this_val;
  (void)argc;
  (void)argv;
  return JS_NewInt32(ctx, p && p->scene ? p->scene->n_objects : 0);
}

/* installs thisScene / thisLayer and runs init() once */
void xw_script_prop_bind(XWPropScript *p, XWScene *scene, int *order,
                         int n_order, int owner) {
  JSValue global, obj, handle;
  JSContext *ctx;
  if (!p || !p->js)
    return;
  ctx = p->js->ctx;
  JS_SetContextOpaque(ctx, p);
  p->scene = scene;
  p->order = order;
  p->n_order = n_order > 0 ? n_order : 0;
  p->owner = owner;
  (void)order;
  (void)n_order;
  global = JS_GetGlobalObject(ctx);
  obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "getLayer",
                    JS_NewCFunction(ctx, scene_get_layer, "getLayer", 1));
  JS_SetPropertyStr(
      ctx, obj, "getLayerIndex",
      JS_NewCFunction(ctx, scene_get_layer_index, "getLayerIndex", 1));
  JS_SetPropertyStr(ctx, obj, "sortLayer",
                    JS_NewCFunction(ctx, scene_sort_layer, "sortLayer", 2));
  JS_SetPropertyStr(
      ctx, obj, "getLayerCount",
      JS_NewCFunction(ctx, scene_layer_count, "getLayerCount", 0));
  JS_SetPropertyStr(ctx, global, "thisScene", obj);
  handle = layer_handle(ctx, scene, owner);
  JS_SetPropertyStr(ctx, global, "thisLayer", handle);
  JS_FreeValue(ctx, global);
  /* the engine runs init() once when the layer is created; a script whose
   * init throws is logged and skipped, which is what the engine does too */
  /* the engine runs init() once when the layer is created; a script whose
   * init throws is logged and skipped there too */
  xw_script_eval(p->js,
                 "if (typeof init === 'function') {\n"
                 "  init(typeof thisLayer !== 'undefined' ? thisLayer : null);\n"
                 "}\n",
                 "prop-init.js", NULL);
}

static const char *prop_script_prelude =
    "function Vec3(x, y, z) { this.x = x; this.y = y; this.z = z; }\n"
    "var WEMath = {\n"
    "  mix: function (a, b, t) { return a + (b - a) * t; },\n"
    "  clamp: function (v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }\n"
    "};\n";

int xw_script_prop_create(XWPropScript **out, const char *src, XWError *err) {
  XWPropScript *p;
  char *clean;
  size_t n;
  if (!out || !src) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  *out = NULL;
  p = calloc(1, sizeof(*p));
  if (!p) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  if (xw_script_create(&p->js, err) != 0) {
    free(p);
    return -1;
  }
  if (xw_script_eval(p->js, text_layer_prelude, "layer-prelude.js", err) != 0 ||
      xw_script_eval(p->js, prop_script_prelude, "prop-prelude.js", err) != 0) {
    xw_script_prop_free(p);
    return -1;
  }
  n = strlen(src) + 1;
  clean = malloc(n);
  if (!clean) {
    XW_ERROR_SET(err, 1, "out of memory");
    xw_script_prop_free(p);
    return -1;
  }
  strip_export(src, clean, n);
  if (xw_script_eval(p->js, clean, "propscript.js", err) != 0) {
    free(clean);
    xw_script_prop_free(p);
    return -1;
  }
  free(clean);
  *out = p;
  return 0;
}

int xw_script_prop_update(XWPropScript *p, const float in[3], float out3[3]) {
  JSValue global, v, res;
  double d;
  int rc = -1;
  if (!p || !p->js || !out3)
    return -1;
  global = JS_GetGlobalObject(p->js->ctx);
  v = JS_NewObject(p->js->ctx);
  JS_SetPropertyStr(p->js->ctx, v, "x", JS_NewFloat64(p->js->ctx, in ? in[0] : 0.0f));
  JS_SetPropertyStr(p->js->ctx, v, "y", JS_NewFloat64(p->js->ctx, in ? in[1] : 0.0f));
  JS_SetPropertyStr(p->js->ctx, v, "z", JS_NewFloat64(p->js->ctx, in ? in[2] : 0.0f));
  JS_SetPropertyStr(p->js->ctx, global, "__propValue", v);
  JS_FreeValue(p->js->ctx, global);
  /* a script that throws (or has no update) leaves the value alone */
  if (xw_script_eval(p->js,
                     "if (typeof update === 'function') {\n"
                     "  var __r = update(__propValue);\n"
                     "  if (__r !== undefined && __r !== null) __propValue = __r;\n"
                     "} else { throw new Error('no update'); }\n",
                     "propscript-update.js", NULL) != 0)
    return -1;
  global = JS_GetGlobalObject(p->js->ctx);
  res = JS_GetPropertyStr(p->js->ctx, global, "__propValue");
  JS_FreeValue(p->js->ctx, global);
  if (JS_IsObject(res)) {
    JSValue c;
    c = JS_GetPropertyStr(p->js->ctx, res, "x");
    if (JS_ToFloat64(p->js->ctx, &d, c) == 0 && !isnan(d)) {
      out3[0] = (float)d;
      rc = 0;
    }
    JS_FreeValue(p->js->ctx, c);
    c = JS_GetPropertyStr(p->js->ctx, res, "y");
    if (JS_ToFloat64(p->js->ctx, &d, c) == 0 && !isnan(d))
      out3[1] = (float)d;
    JS_FreeValue(p->js->ctx, c);
    c = JS_GetPropertyStr(p->js->ctx, res, "z");
    if (JS_ToFloat64(p->js->ctx, &d, c) == 0 && !isnan(d))
      out3[2] = (float)d;
    JS_FreeValue(p->js->ctx, c);
  }
  JS_FreeValue(p->js->ctx, res);
  return rc;
}

/* Boolean-valued properties (visible) hand update() a plain boolean. */
int xw_script_prop_update_bool(XWPropScript *p, int in, int *out) {
  JSValue global, res;
  int rc = -1;
  if (!p || !p->js || !out)
    return -1;
  global = JS_GetGlobalObject(p->js->ctx);
  JS_SetPropertyStr(p->js->ctx, global, "__propValue",
                    JS_NewBool(p->js->ctx, in ? 1 : 0));
  JS_FreeValue(p->js->ctx, global);
  if (xw_script_eval(p->js,
                     "if (typeof update === 'function') {\n"
                     "  var __r = update(__propValue);\n"
                     "  if (__r !== undefined && __r !== null) __propValue = __r;\n"
                     "} else { throw new Error('no update'); }\n",
                     "prop-bool.js", NULL) != 0)
    return -1;
  global = JS_GetGlobalObject(p->js->ctx);
  res = JS_GetPropertyStr(p->js->ctx, global, "__propValue");
  if (JS_IsBool(res)) {
    *out = JS_ToBool(p->js->ctx, res) ? 1 : 0;
    rc = 0;
  }
  JS_FreeValue(p->js->ctx, res);
  JS_FreeValue(p->js->ctx, global);
  return rc;
}

/* Run an object's scripted transform properties for this frame. Scripts that
 * throw, or that declare no update(), leave the parsed value alone. */
void xw_script_apply_object_props(XWScript *s, XWSceneObject *o) {
  XWObjPropRunners *r;
  float in3[3], out3[3];
  int i, k;
  if (!s || !o || !s->scene || !s->scene->objects)
    return;
  i = (int)(o - s->scene->objects);
  if (i < 0 || i >= s->scene->n_objects)
    return;
  if (!s->objr || s->objr_n != s->scene->n_objects) {
    XWObjPropRunners *nr;
    if (s->objr) {
      for (int j = 0; j < s->objr_n; j++)
        for (int q = 0; q < XW_PROP_COUNT; q++)
          xw_script_prop_free(s->objr[j].p[q]);
      free(s->objr);
    }
    nr = calloc((size_t)s->scene->n_objects, sizeof(*nr));
    if (!nr) {
      s->objr = NULL;
      s->objr_n = 0;
      return;
    }
    s->objr = nr;
    s->objr_n = s->scene->n_objects;
  }
  r = &s->objr[i];
  for (k = 0; k < XW_PROP_COUNT; k++) {
    if (!o->prop_script[k] || !o->prop_script[k][0])
      continue;
    if (!r->p[k]) {
      XWError err = {0};
      if (xw_script_prop_create(&r->p[k], o->prop_script[k], &err) != 0) {
        /* a script we cannot even compile never runs again */
        r->p[k] = NULL;
        o->prop_script[k][0] = '\0';
        continue;
      }
      xw_script_prop_bind(r->p[k], s->scene, s->order, s->n_order, i);
    }
    if (k == XW_PROP_VISIBLE) {
      int b = o->visible;
      if (xw_script_prop_update_bool(r->p[k], o->visible, &b) == 0)
        o->visible = b;
      continue;
    }
    in3[0] = (k == XW_PROP_ORIGIN)   ? o->origin[0]
             : (k == XW_PROP_SCALE)  ? o->scale[0]
                                     : o->size[0];
    in3[1] = (k == XW_PROP_ORIGIN)   ? o->origin[1]
             : (k == XW_PROP_SCALE)  ? o->scale[1]
                                     : o->size[1];
    in3[2] = 0.0f;
    out3[0] = in3[0];
    out3[1] = in3[1];
    out3[2] = 0.0f;
    if (xw_script_prop_update(r->p[k], in3, out3) != 0)
      continue;
    if (k == XW_PROP_ORIGIN) {
      o->origin[0] = out3[0];
      o->origin[1] = out3[1];
    } else if (k == XW_PROP_SCALE) {
      o->scale[0] = out3[0];
      o->scale[1] = out3[1];
    } else {
      o->size[0] = out3[0];
      o->size[1] = out3[1];
    }
  }
}

void xw_script_prop_free(XWPropScript *p) {
  if (!p)
    return;
  xw_script_destroy(p->js);
  free(p);
}
