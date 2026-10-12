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
  JS_FreeValue(s->ctx, global);
  *out = s;
  return 0;
}

void xw_script_destroy(XWScript *s) {
  if (!s)
    return;
  for (int i = 0; i < s->ntimers; i++) JS_FreeValue(s->ctx, s->timers[i].func);
  free(s->timers);
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
