#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpv/client.h>

#include "backend.h"
#include "x11.h"

typedef struct {
  XWWindow *win;
  mpv_handle *mpv;
  char path[1024];
  int mute;
  double volume;
  int scale_mode;
} VideoCtx;

/* Start playback asynchronously; if it fails, mpv reports via events. */
static void load_file(VideoCtx *ctx) {
  const char *cmd[] = {"loadfile", ctx->path, "replace", NULL};
  mpv_command(ctx->mpv, cmd);
}

static int video_init(XWBackend *b, XWWindow *win, const XWBackendOpt *opt, XWError *err) {
  VideoCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->win = win;
  ctx->mute = opt->mute;
  ctx->volume = opt->volume;
  ctx->scale_mode = opt->scale_mode;

  ctx->mpv = mpv_create();
  if (!ctx->mpv) {
    XW_ERROR_SET(err, 1, "mpv_create failed");
    free(ctx);
    return -1;
  }

  /* mpv renders directly into the X window (own GL thread) */
  int64_t wid = (int64_t)win->win;
  if (mpv_set_property(ctx->mpv, "wid", MPV_FORMAT_INT64, &wid) < 0) {
    XW_ERROR_SET(err, 1, "mpv: cannot set wid");
    mpv_terminate_destroy(ctx->mpv);
    free(ctx);
    return -1;
  }
  mpv_set_option_string(ctx->mpv, "loop-file", "inf");
  mpv_set_option_string(ctx->mpv, "vo", "gpu");
  mpv_set_option_string(ctx->mpv, "hwdec", "auto-safe");
  mpv_set_option_string(ctx->mpv, "input-default-bindings", "no");
  mpv_set_option_string(ctx->mpv, "input-vo-keyboard", "no");
  mpv_set_option_string(ctx->mpv, "osc", "no");
  mpv_set_option_string(ctx->mpv, "osd-level", "0");
  /* identify the audio stream (PulseAudio/PipeWire/ALSA) with a friendly name */
  if (opt->audio_client_name && opt->audio_client_name[0])
    mpv_set_option_string(ctx->mpv, "audio-client-name", opt->audio_client_name);
  /* load custom keybinds from an input.conf-style file (empty = none) */
  if (opt->keybinds && opt->keybinds[0]) {
    mpv_set_option_string(ctx->mpv, "input-conf", opt->keybinds);
    mpv_set_option_string(ctx->mpv, "input-vo-keyboard", "yes");
  }
  /* target frame rate: the fps filter drops frames to a fixed rate
   * without touching the timeline, so playback speed stays correct. */
  if (opt->fps > 0.0) {
    char vf[32];
    snprintf(vf, sizeof(vf), "fps=%g", opt->fps);
    mpv_set_option_string(ctx->mpv, "vf", vf);
  }

  /* scale mode: FILL crops to fill (panscan 1.0), STRETCH disables aspect,
   * CONTAIN keeps aspect with bars (mpv default) */
  if (ctx->scale_mode == XW_SCALE_FILL) {
    mpv_set_option_string(ctx->mpv, "panscan", "1.0");
  } else if (ctx->scale_mode == XW_SCALE_STRETCH) {
    mpv_set_option_string(ctx->mpv, "keepaspect", "no");
  }

  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: mpv_initialize (win 0x%lx)...\n",
            (unsigned long)win->win);
  if (mpv_initialize(ctx->mpv) < 0) {
    XW_ERROR_SET(err, 1, "mpv_initialize failed");
    mpv_terminate_destroy(ctx->mpv);
    free(ctx);
    return -1;
  }
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: mpv_initialize done\n");

  if (ctx->mute)
    mpv_set_property_string(ctx->mpv, "mute", "yes");
  else {
    char vol[16];
    snprintf(vol, sizeof(vol), "%.1f", ctx->volume);
    mpv_set_property_string(ctx->mpv, "volume", vol);
  }

  /* static rotation: initial angle for the video, e.g. portrait clips */
  if (opt->rotate != 0) {
    int64_t rot = opt->rotate;
    mpv_set_property(ctx->mpv, "video-rotate", MPV_FORMAT_INT64, &rot);
  }

  snprintf(ctx->path, sizeof(ctx->path), "%s", opt->path);
  b->priv = ctx;
  return 0;
}

static int video_start(XWBackend *b) {
  VideoCtx *ctx = (VideoCtx *)b->priv;
  load_file(ctx);
  return 0;
}

static void video_stop(XWBackend *b) {
  VideoCtx *ctx = (VideoCtx *)b->priv;
  if (ctx)
    mpv_command_string(ctx->mpv, "stop");
}

/* Pause/resume playback: mpv stops rendering (no frame presents) while
 * paused, which is what lets DPMS stay off on the display side. */
static void video_set_paused(XWBackend *b, int paused) {
  VideoCtx *ctx = (VideoCtx *)b->priv;
  if (ctx)
    mpv_set_property_string(ctx->mpv, "pause", paused ? "yes" : "no");
}

static void video_destroy(XWBackend *b) {
  if (!b || !b->priv)
    return;
  VideoCtx *ctx = (VideoCtx *)b->priv;
  mpv_terminate_destroy(ctx->mpv);
  free(ctx);
  b->priv = NULL;
}

XWBackend *xw_video_backend_new(void) {
  static XWBackend backend = {
    .name = "video",
    .init = video_init,
    .start = video_start,
    .stop = video_stop,
    .destroy = video_destroy,
    .on_event = NULL,
    .set_paused = video_set_paused,
  };
  return &backend;
}
