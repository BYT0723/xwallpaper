#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "we_audio.h"
#include "we_pulse.h"

#ifdef XW_HAVE_PULSE
#include <pulse/pulseaudio.h>

#define WE_CHUNK 2048

typedef struct {
  pthread_t th;
  pthread_mutex_t mu;
  float latest[128];
  int refcount;
  int running;
  int stop;
  pa_mainloop *ml;
  pa_context *ctx;
  pa_stream *st;
  char monitor[256];
  int reconnect;
  float bl[WE_CHUNK], br[WE_CHUNK];
  int fill;
} PulseState;

static PulseState g = {.mu = PTHREAD_MUTEX_INITIALIZER};

static void server_info_cb(pa_context *c, const pa_server_info *i, void *ud) {
  PulseState *s = (PulseState *)ud;
  (void)c;
  if (i && i->default_sink_name)
    snprintf(s->monitor, sizeof(s->monitor), "%s", i->default_sink_name);
}

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *ud) {
  PulseState *s = (PulseState *)ud;
  (void)c;
  if (eol || !i)
    return;
  if (i->monitor_source_name)
    snprintf(s->monitor, sizeof(s->monitor), "%s", i->monitor_source_name);
}

static void stream_read_cb(pa_stream *st, size_t nbytes, void *ud) {
  PulseState *s = (PulseState *)ud;
  const void *data;
  size_t n = nbytes;
  if (pa_stream_peek(st, &data, &n) < 0 || !data || n == 0)
    return;
  size_t frames = n / (sizeof(float) * 2);
  const float *f = (const float *)data;
  for (size_t i = 0; i < frames; i++) {
    if (s->fill < WE_CHUNK) {
      s->bl[s->fill] = f[2 * i];
      s->br[s->fill] = f[2 * i + 1];
      s->fill++;
    }
    if (s->fill == WE_CHUNK) {
      float out[128];
      if (xw_we_audio_map(s->bl, s->br, WE_CHUNK, out) == 0) {
        pthread_mutex_lock(&s->mu);
        memcpy(s->latest, out, sizeof(out));
        pthread_mutex_unlock(&s->mu);
      }
      s->fill = 0;
    }
  }
  pa_stream_drop(st);
}

static void sub_cb(pa_context *c, pa_subscription_event_type_t t,
                   uint32_t idx, void *ud) {
  PulseState *s = (PulseState *)ud;
  (void)c;
  (void)idx;
  unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  if (facility == PA_SUBSCRIPTION_EVENT_SINK ||
      facility == PA_SUBSCRIPTION_EVENT_SOURCE ||
      facility == PA_SUBSCRIPTION_EVENT_SERVER)
    s->reconnect = 1;
}

static int wait_ctx_ready(PulseState *s) {
  for (int i = 0; i < 100; i++) {
    pa_mainloop_iterate(s->ml, 0, NULL);
    pa_context_state_t st = pa_context_get_state(s->ctx);
    if (st == PA_CONTEXT_READY)
      return 0;
    if (!PA_CONTEXT_IS_GOOD(st))
      return -1;
    usleep(50000);
  }
  return -1;
}

static int wait_op(pa_operation *op, PulseState *s) {
  if (!op)
    return -1;
  for (int i = 0; i < 100; i++) {
    pa_mainloop_iterate(s->ml, 0, NULL);
    if (pa_operation_get_state(op) == PA_OPERATION_DONE) {
      pa_operation_unref(op);
      return 0;
    }
    usleep(20000);
  }
  pa_operation_cancel(op);
  pa_operation_unref(op);
  return -1;
}

static int wait_stream_ready(pa_stream *st, PulseState *s) {
  for (int i = 0; i < 100; i++) {
    pa_mainloop_iterate(s->ml, 0, NULL);
    pa_stream_state_t t = pa_stream_get_state(st);
    if (t == PA_STREAM_READY)
      return 0;
    if (!PA_STREAM_IS_GOOD(t))
      return -1;
    usleep(50000);
  }
  return -1;
}

static void *pulse_thread(void *ud) {
  PulseState *s = (PulseState *)ud;
  while (!s->stop) {
    s->ml = pa_mainloop_new();
    if (!s->ml)
      break;
    s->ctx = pa_context_new(pa_mainloop_get_api(s->ml), "xwallpaper");
    if (!s->ctx) {
      pa_mainloop_free(s->ml);
      s->ml = NULL;
      break;
    }
    if (pa_context_connect(s->ctx, NULL, 0, NULL) < 0)
      goto retry;
    if (wait_ctx_ready(s) != 0)
      goto retry;
    pa_context_set_subscribe_callback(s->ctx, sub_cb, s);
    pa_operation *sop =
        pa_context_subscribe(s->ctx, PA_SUBSCRIPTION_MASK_SINK |
                                         PA_SUBSCRIPTION_MASK_SOURCE |
                                         PA_SUBSCRIPTION_MASK_SERVER,
                             NULL, NULL);
    if (sop)
      pa_operation_unref(sop);

    s->monitor[0] = '\0';
    if (wait_op(pa_context_get_server_info(s->ctx, server_info_cb, s), s) != 0)
      goto retry;
    if (!s->monitor[0])
      goto retry;
    /* monitor[] currently holds the default sink name; resolve its monitor source */
    char sink[256];
    snprintf(sink, sizeof(sink), "%s", s->monitor);
    s->monitor[0] = '\0';
    if (wait_op(pa_context_get_sink_info_by_name(s->ctx, sink, sink_info_cb, s), s) != 0)
      goto retry;
    if (!s->monitor[0])
      goto retry;

    pa_sample_spec ss = {.format = PA_SAMPLE_FLOAT32LE,
                         .rate = XW_WE_AUDIO_RATE,
                         .channels = 2};
    pa_buffer_attr attr = {.maxlength = (uint32_t)-1,
                           .fragsize = WE_CHUNK * sizeof(float) * 2};
    s->st = pa_stream_new(s->ctx, "we-audio", &ss, NULL);
    if (!s->st)
      goto retry;
    pa_stream_set_read_callback(s->st, stream_read_cb, s);
    if (pa_stream_connect_record(s->st, s->monitor, &attr,
                                 PA_STREAM_ADJUST_LATENCY) < 0) {
      pa_stream_unref(s->st);
      s->st = NULL;
      goto retry;
    }
    if (wait_stream_ready(s->st, s) != 0) {
      pa_stream_disconnect(s->st);
      pa_stream_unref(s->st);
      s->st = NULL;
      goto retry;
    }
    s->reconnect = 0;
    while (!s->stop && !s->reconnect) {
      pa_mainloop_iterate(s->ml, 1, NULL);
      pa_stream_state_t t = pa_stream_get_state(s->st);
      pa_context_state_t c = pa_context_get_state(s->ctx);
      if (!PA_STREAM_IS_GOOD(t) || !PA_CONTEXT_IS_GOOD(c))
        break;
    }
    if (s->st) {
      pa_stream_disconnect(s->st);
      pa_stream_unref(s->st);
      s->st = NULL;
    }
  retry:
    if (s->ctx) {
      pa_context_disconnect(s->ctx);
      pa_context_unref(s->ctx);
      s->ctx = NULL;
    }
    if (s->ml) {
      pa_mainloop_free(s->ml);
      s->ml = NULL;
    }
    for (int i = 0; i < 10 && !s->stop; i++)
      usleep(100000);
  }
  return NULL;
}

int xw_we_pulse_start(void) {
  pthread_mutex_lock(&g.mu);
  g.refcount++;
  int first = (g.refcount == 1);
  pthread_mutex_unlock(&g.mu);
  if (!first)
    return 0;
  memset(g.latest, 0, sizeof(g.latest));
  g.stop = 0;
  g.fill = 0;
  g.ml = NULL;
  g.ctx = NULL;
  g.st = NULL;
  if (pthread_create(&g.th, NULL, pulse_thread, &g) != 0) {
    pthread_mutex_lock(&g.mu);
    g.refcount = 0;
    pthread_mutex_unlock(&g.mu);
    return -1;
  }
  pthread_mutex_lock(&g.mu);
  g.running = 1;
  pthread_mutex_unlock(&g.mu);
  return 0;
}

void xw_we_pulse_stop(void) {
  pthread_mutex_lock(&g.mu);
  if (g.refcount > 0)
    g.refcount--;
  int last = (g.refcount == 0 && g.running);
  pthread_mutex_unlock(&g.mu);
  if (!last)
    return;
  g.stop = 1;
  pthread_join(g.th, NULL);
  pthread_mutex_lock(&g.mu);
  g.running = 0;
  memset(g.latest, 0, sizeof(g.latest));
  pthread_mutex_unlock(&g.mu);
}

int xw_we_pulse_get(float out[128]) {
  if (!out)
    return -1;
  pthread_mutex_lock(&g.mu);
  memcpy(out, g.latest, sizeof(g.latest));
  pthread_mutex_unlock(&g.mu);
  return 0;
}

int xw_we_pulse_running(void) {
  pthread_mutex_lock(&g.mu);
  int r = g.running && g.refcount > 0;
  pthread_mutex_unlock(&g.mu);
  return r;
}

#else /* !XW_HAVE_PULSE: build-safe stub, silence only */

#include <string.h>

int xw_we_pulse_start(void) { return -1; }
void xw_we_pulse_stop(void) {}
int xw_we_pulse_get(float out[128]) {
  if (!out)
    return -1;
  memset(out, 0, sizeof(float) * 128);
  return 0;
}
int xw_we_pulse_running(void) { return 0; }

#endif
