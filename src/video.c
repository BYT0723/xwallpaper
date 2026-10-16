#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mpv/client.h>

#include "backend.h"
#include "keybinds.h"
#include "x11.h"

typedef struct {
  mpv_handle *mpv;
  char path[1024];
  int mute;
  double volume;
  int scale_mode;
  int64_t last_load_ms; /* 上次 loadfile 时刻 (retry 防堆积) */
} VideoBase;

typedef struct {
  /* base 必须首成员: wl 实例 (VideoWlCtx) 同样 base 开头, stop/pause/
   * retry/reload 只碰 base, 同一份实现双平台通用 (C11 首成员规则)。 */
  VideoBase base;
  XWWindow *win;
} VideoCtx;

/* Start playback asynchronously; if it fails, mpv reports via events.
 * 统一记录 loadfile 时刻 (启动/换壁纸/自愈共用), 供 video_retry 防堆积 —
 * 若只记 retry 自己的时刻, video_start 的首次 loadfile 尚未建立 track 时
 * 会被 retry 误判为无视频并重复加载。 */
static void load_file(VideoBase *m) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  m->last_load_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  const char *cmd[] = {"loadfile", m->path, "replace", NULL};
  mpv_command(m->mpv, cmd);
}

static int video_init(XWBackend *b, XWWindow *win, const XWBackendOpt *opt, XWError *err) {
  VideoCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->win = win;
  ctx->base.mute = opt->mute;
  ctx->base.volume = opt->volume;
  ctx->base.scale_mode = opt->scale_mode;

  ctx->base.mpv = mpv_create();
  if (!ctx->base.mpv) {
    XW_ERROR_SET(err, 1, "mpv_create failed");
    free(ctx);
    return -1;
  }

  /* mpv renders directly into the X window (own GL thread) */
  int64_t wid = (int64_t)win->win;
  if (mpv_set_property(ctx->base.mpv, "wid", MPV_FORMAT_INT64, &wid) < 0) {
    XW_ERROR_SET(err, 1, "mpv: cannot set wid");
    mpv_terminate_destroy(ctx->base.mpv);
    free(ctx);
    return -1;
  }
  mpv_set_option_string(ctx->base.mpv, "loop-file", "inf");
  mpv_set_option_string(ctx->base.mpv, "vo", "gpu");
  mpv_set_option_string(ctx->base.mpv, "hwdec", "auto-safe");
  mpv_set_option_string(ctx->base.mpv, "input-default-bindings", "no");
  mpv_set_option_string(ctx->base.mpv, "input-vo-keyboard", "no");
  mpv_set_option_string(ctx->base.mpv, "osc", "no");
  mpv_set_option_string(ctx->base.mpv, "osd-level", "0");
  /* identify the audio stream (PulseAudio/PipeWire/ALSA) with a friendly name */
  if (opt->audio_client_name && opt->audio_client_name[0])
    mpv_set_option_string(ctx->base.mpv, "audio-client-name", opt->audio_client_name);
  /* key bindings are the daemon's now: it parses the file and drives this
   * backend through control(), so mpv must not handle keys itself (which
   * would act on the same press twice). input-default-bindings and
   * input-vo-keyboard stay off above. */
  /* target frame rate: the fps filter drops frames to a fixed rate
   * without touching the timeline, so playback speed stays correct. */
  if (opt->fps > 0.0) {
    char vf[32];
    snprintf(vf, sizeof(vf), "fps=%g", opt->fps);
    mpv_set_option_string(ctx->base.mpv, "vf", vf);
  }

  /* 诊断: debug 时开启 mpv 日志文件 + 命令 IPC, 首次开机黑屏时可查
   * pause/playback 状态与解码器报错 (mpv log 写 /tmp/xw-mpv-<winid>.log,
   * IPC 用 socat 连 socket 后 get_property pause 等) */
  if (getenv("XW_DEBUG_DAEMON")) {
    char logf[128];
    snprintf(logf, sizeof(logf), "/tmp/xw-mpv-%lx.log",
             (unsigned long)win->win);
    mpv_set_option_string(ctx->base.mpv, "log-file", logf);
    mpv_request_log_messages(ctx->base.mpv, "debug");
    char sock[128];
    snprintf(sock, sizeof(sock), "/tmp/xw-mpv-%lx.sock",
             (unsigned long)win->win);
    mpv_set_option_string(ctx->base.mpv, "input-ipc-server", sock);
  }

  /* scale mode: FILL crops to fill (panscan 1.0), STRETCH disables aspect,
   * CONTAIN keeps aspect with bars (mpv default) */
  if (ctx->base.scale_mode == XW_SCALE_FILL) {
    mpv_set_option_string(ctx->base.mpv, "panscan", "1.0");
  } else if (ctx->base.scale_mode == XW_SCALE_STRETCH) {
    mpv_set_option_string(ctx->base.mpv, "keepaspect", "no");
  }

  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: mpv_initialize (win 0x%lx)...\n",
            (unsigned long)win->win);
  int merr = mpv_initialize(ctx->base.mpv);
  if (merr < 0) {
    XW_ERROR_SET(err, 1, "mpv_initialize failed: %s",
                 mpv_error_string(merr));
    mpv_terminate_destroy(ctx->base.mpv);
    free(ctx);
    return -1;
  }
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: mpv_initialize done\n");

  if (ctx->base.mute)
    mpv_set_property_string(ctx->base.mpv, "mute", "yes");
  else {
    char vol[16];
    snprintf(vol, sizeof(vol), "%.1f", ctx->base.volume);
    mpv_set_property_string(ctx->base.mpv, "volume", vol);
  }

  /* static rotation: initial angle for the video, e.g. portrait clips */
  if (opt->rotate != 0) {
    int64_t rot = opt->rotate;
    mpv_set_property(ctx->base.mpv, "video-rotate", MPV_FORMAT_INT64, &rot);
  }

  snprintf(ctx->base.path, sizeof(ctx->base.path), "%s", opt->path);
  b->priv = ctx;
  return 0;
}

static int video_start(XWBackend *b) {
  load_file((VideoBase *)b->priv);
  return 0;
}

static void video_stop(XWBackend *b) {
  VideoBase *m = (VideoBase *)b->priv;
  if (m)
    mpv_command_string(m->mpv, "stop");
}

/* Pause/resume playback: mpv stops rendering (no frame presents) while
 * paused, which is what lets DPMS stay off on the display side. */
static void video_set_paused(XWBackend *b, int paused) {
  VideoBase *m = (VideoBase *)b->priv;
  if (m)
    mpv_set_property_string(m->mpv, "pause", paused ? "yes" : "no");
}

/* 启动自愈: 首次开机 GPU/GL 未就绪时 vo/gpu 的 Vulkan swapchain 创建会失败
 * (VK_ERROR_INITIALIZATION_FAILED), mpv 因此丢掉 video track — 只剩音频在走,
 * 窗口存在但画面黑 (restore 认为成功, 不触发窗口重建)。检测 mpv 的 video
 * 属性, 无视频则重新 loadfile, 让 mpv 重建渲染路径, 环境就绪后即恢复。
 * 防堆积: 距上次 loadfile ≥1.5s — daemon 以 0.5s 节拍轮询, 但 4K HEVC
 * loadfile 本身需几百 ms, 间隔过短会让 loadfile 排队堆积。 */
static int video_retry(XWBackend *b) {
  VideoBase *m = (VideoBase *)b->priv;
  if (!m || !m->mpv)
    return -1;
  int v = 0;
  if (mpv_get_property(m->mpv, "video", MPV_FORMAT_FLAG, &v) < 0)
    return -1;
  if (v)
    return 0;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int64_t now_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  if (now_ms - m->last_load_ms < 1500)
    return 0;
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: video %.100s has no video track, reloading\n",
            m->path);
  load_file(m);
  return 1;
}

static void video_destroy(XWBackend *b) {
  if (!b || !b->priv)
    return;
  VideoBase *m = (VideoBase *)b->priv;
  mpv_terminate_destroy(m->mpv);
  free(m);
  b->priv = NULL;
}

/* 同类型复用: 只换播放文件 (mpv loadfile 异步, 不重建 GL/mpv 上下文)。
 * rotation 总是下发 (含 0) 以便从旋转状态复位。 */
static int video_reload(XWBackend *b, const XWBackendOpt *opt, XWError *err) {
  (void)err;
  VideoBase *m = (VideoBase *)b->priv;
  if (!m)
    return -1;
  snprintf(m->path, sizeof(m->path), "%s", opt->path);
  m->volume = opt->volume;
  m->mute = opt->mute;
  int64_t rot = opt->rotate;
  mpv_set_property(m->mpv, "video-rotate", MPV_FORMAT_INT64, &rot);
  load_file(m);
  return 0;
}

/* Runtime control for the key bindings and the volume/mute subcommands.
 * mpv owns the values, so every relative step reads the live property first
 * instead of trusting a cached copy. */
static int video_control(XWBackend *b, const char *name, const char *value,
                         XWError *err) {
  VideoBase *m = (b && b->priv) ? (VideoBase *)b->priv : NULL;
  int cur = 0, want;
  if (!m || !m->mpv || !name) {
    XW_ERROR_SET(err, 1, "video: not running");
    return -1;
  }
  if (strcmp(name, "volume") == 0) {
    double now = 0.0, next;
    if (mpv_get_property(m->mpv, "volume", MPV_FORMAT_DOUBLE, &now) < 0)
      return -1;
    if (xw_audio_apply((int)(now + 0.5), value, &cur) != 0) {
      XW_ERROR_SET(err, 1, XW_VOLUME_HINT);
      return -1;
    }
    next = (double)cur;
    if (mpv_set_property(m->mpv, "volume", MPV_FORMAT_DOUBLE, &next) < 0)
      return -1;
    /* raising the volume is meaningless while muted, so unmute with it */
    if (m->mute) {
      mpv_set_property_string(m->mpv, "mute", "no");
      m->mute = 0;
    }
    m->volume = next;
    return 0;
  }
  if (strcmp(name, "mute") == 0 || strcmp(name, "pause") == 0) {
    if (mpv_get_property(m->mpv, name, MPV_FORMAT_FLAG, &cur) < 0)
      return -1;
    if (xw_ctl_toggle(cur, value, &want) != 0) {
      XW_ERROR_SET(err, 1, "%s takes on, off or toggle", name);
      return -1;
    }
    if (mpv_set_property(m->mpv, name, MPV_FORMAT_FLAG, &want) < 0)
      return -1;
    if (strcmp(name, "mute") == 0)
      m->mute = want;
    return 0;
  }
  if (strcmp(name, "seek") == 0) {
    char secs[32];
    const char *args[4];
    if (!value || !value[0]) {
      XW_ERROR_SET(err, 1, "seek needs seconds");
      return -1;
    }
    snprintf(secs, sizeof(secs), "%s", value);
    args[0] = "seek";
    args[1] = secs;
    args[2] = "relative";
    args[3] = NULL;
    if (mpv_command(m->mpv, args) < 0) {
      XW_ERROR_SET(err, 1, "seek failed");
      return -1;
    }
    return 0;
  }
  if (strcmp(name, "rotate") == 0) {
    static const int ring[4] = {90, 180, 270, 0};
    int64_t now = 0, next;
    if (mpv_get_property(m->mpv, "video-rotate", MPV_FORMAT_INT64, &now) < 0)
      return -1;
    if (!value || !value[0] || strcmp(value, "cycle") == 0) {
      if (xw_values_cycle(ring, 4, (int)now, &cur) != 0)
        return -1;
      next = cur;
    } else {
      next = strtol(value, NULL, 10);
    }
    if (mpv_set_property(m->mpv, "video-rotate", MPV_FORMAT_INT64, &next) < 0)
      return -1;
    return 0;
  }
  XW_ERROR_SET(err, 1, "video does not support '%s'", name);
  return -1;
}

#ifdef XW_HAVE_WAYLAND
static int video_init_wl(XWBackend *b, XWWlWindow *win,
                         const XWBackendOpt *opt, XWError *err);
static int video_start_wl(XWBackend *b);
static void video_destroy_wl(XWBackend *b);
#endif

XWBackend *xw_video_backend_new(void) {
  static XWBackend backend = {
    .name = "video",
    .init = video_init,
    .start = video_start,
    .stop = video_stop,
    .destroy = video_destroy,
    .on_event = NULL,
    .set_paused = video_set_paused,
    .control = video_control,
    .reload = video_reload,
    .retry = video_retry,
#ifdef XW_HAVE_WAYLAND
    /* stop/set_paused/retry/reload touch only VideoBase (first member of
     * both ctx structs), so the X11 implementations serve wl instances. */
    .init_wl = video_init_wl,
    .start_wl = video_start_wl,
    .destroy_wl = video_destroy_wl,
    .reload_wl = video_reload,
#else
    .init_wl = NULL,
    .start_wl = NULL,
    .destroy_wl = NULL,
    .reload_wl = NULL,
#endif
  };
  return &backend;
}

#ifdef XW_HAVE_WAYLAND
#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <mpv/render.h>
#include <mpv/render_gl.h>

#include "wl.h"

/* Wayland video: libmpv render API into an EGL window surface on the
 * layer-shell surface (--wid does not exist on Wayland). All rendering
 * happens on the daemon main thread, paced by compositor frame callbacks:
 * mpv's update callback (mpv threads) only sets pending. No threads,
 * no locks. stop/set_paused/retry/reload reuse the X11 base-only code. */
typedef struct {
  VideoBase base; /* must stay first (see VideoCtx) */
  XWWlWindow *win;
  mpv_render_context *mpv_gl;
  EGLDisplay egl_dpy;
  EGLContext egl_ctx;
  EGLSurface egl_surf;
  struct wl_egl_window *egl_win;
  int gl_desktop; /* EGL_OPENGL_API (supports blit-flip) vs GLES */
  unsigned int flip_tex; /* offscreen render target (0 = direct render) */
  unsigned int flip_fbo;
  int active;  /* frame-callback chain armed */
  int pending; /* mpv produced a frame while we were idle */
  /* Frame callbacks outlive the backend: clear destroys mpv/EGL/surface
   * while a frame.done may still be in flight; the done arrives later via
   * the daemon dispatch and must not touch freed memory. frame_refs counts
   * outstanding callbacks (backend itself is not counted): destroy marks
   * dead and frees only when no done is outstanding, otherwise the last
   * done frees. dead ctx memory stays valid until refs hit 0. */
  int frame_refs;
  int dead;
} VideoWlCtx;

static void wl_load_file(VideoWlCtx *ctx) {
  load_file(&ctx->base);
}

static void wl_update_cb(void *data) {
  ((VideoWlCtx *)data)->pending = 1;
}

static void *wl_get_proc(void *fn_ctx, const char *name) {
  (void)fn_ctx;
  return (void *)eglGetProcAddress(name);
}

/* Minimal GL decls (no <GL/gl.h> dependency): offscreen flip target +
 * flipped blit to the EGL window surface. mpv renders bottom-up into
 * whatever FBO we hand it and FLIP_Y is unsupported, so without this
 * the video shows upside down. Desktop GL only (blit needs 3.0+). */
typedef unsigned int xw_GLenum;
typedef unsigned int xw_GLbitfield;
typedef int xw_GLint;
typedef int xw_GLsizei;
typedef unsigned int xw_GLuint;
#define XW_GL_TEXTURE_2D 0x0DE1
#define XW_GL_RGBA8 0x8058
#define XW_GL_RGBA 0x1908
#define XW_GL_UNSIGNED_BYTE 0x1401
#define XW_GL_TEXTURE_MIN_FILTER 0x2801
#define XW_GL_TEXTURE_MAG_FILTER 0x2800
#define XW_GL_LINEAR 0x2601
#define XW_GL_FRAMEBUFFER 0x8D40
#define XW_GL_READ_FRAMEBUFFER 0x8CA8
#define XW_GL_DRAW_FRAMEBUFFER 0x8CA9
#define XW_GL_COLOR_ATTACHMENT0 0x8CE0
#define XW_GL_COLOR_BUFFER_BIT 0x00004000
#define XW_GL_FRAMEBUFFER_COMPLETE 0x8CD5
typedef struct {
  void (*GenTextures)(xw_GLsizei, xw_GLuint *);
  void (*BindTexture)(xw_GLenum, xw_GLuint);
  void (*TexImage2D)(xw_GLenum, xw_GLint, xw_GLint, xw_GLsizei, xw_GLsizei,
                     xw_GLint, xw_GLenum, xw_GLenum, const void *);
  void (*TexParameteri)(xw_GLenum, xw_GLenum, xw_GLint);
  void (*GenFramebuffers)(xw_GLsizei, xw_GLuint *);
  void (*BindFramebuffer)(xw_GLenum, xw_GLuint);
  void (*FramebufferTexture2D)(xw_GLenum, xw_GLenum, xw_GLenum, xw_GLuint,
                               xw_GLint);
  xw_GLenum (*CheckFramebufferStatus)(xw_GLenum);
  void (*BlitFramebuffer)(xw_GLint, xw_GLint, xw_GLint, xw_GLint, xw_GLint,
                          xw_GLint, xw_GLint, xw_GLint, xw_GLbitfield,
                          xw_GLenum);
  void (*DeleteFramebuffers)(xw_GLsizei, const xw_GLuint *);
  void (*DeleteTextures)(xw_GLsizei, const xw_GLuint *);
} XWGL;
static XWGL xwgl;
static int xwgl_ok;

static void wl_gl_load(void) {
  if (xwgl_ok)
    return;
  xwgl.GenTextures = wl_get_proc(NULL, "glGenTextures");
  xwgl.BindTexture = wl_get_proc(NULL, "glBindTexture");
  xwgl.TexImage2D = wl_get_proc(NULL, "glTexImage2D");
  xwgl.TexParameteri = wl_get_proc(NULL, "glTexParameteri");
  xwgl.GenFramebuffers = wl_get_proc(NULL, "glGenFramebuffers");
  xwgl.BindFramebuffer = wl_get_proc(NULL, "glBindFramebuffer");
  xwgl.FramebufferTexture2D = wl_get_proc(NULL, "glFramebufferTexture2D");
  xwgl.CheckFramebufferStatus = wl_get_proc(NULL, "glCheckFramebufferStatus");
  xwgl.BlitFramebuffer = wl_get_proc(NULL, "glBlitFramebuffer");
  xwgl.DeleteFramebuffers = wl_get_proc(NULL, "glDeleteFramebuffers");
  xwgl.DeleteTextures = wl_get_proc(NULL, "glDeleteTextures");
  xwgl_ok = xwgl.GenTextures && xwgl.BindTexture && xwgl.TexImage2D &&
            xwgl.TexParameteri && xwgl.GenFramebuffers &&
            xwgl.BindFramebuffer && xwgl.FramebufferTexture2D &&
            xwgl.CheckFramebufferStatus && xwgl.BlitFramebuffer &&
            xwgl.DeleteFramebuffers && xwgl.DeleteTextures;
}

static const struct wl_callback_listener wl_frame_listener;

static void wl_paint(VideoWlCtx *ctx) {
  int W = (int)ctx->win->width, H = (int)ctx->win->height;
  eglMakeCurrent(ctx->egl_dpy, ctx->egl_surf, ctx->egl_surf, ctx->egl_ctx);
  if (ctx->flip_fbo) {
    mpv_opengl_fbo fbo = {
      .fbo = (int)ctx->flip_fbo, .w = W, .h = H, .internal_format = 0,
    };
    mpv_render_param params[] = {
      {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
      {MPV_RENDER_PARAM_INVALID, NULL},
    };
    mpv_render_context_render(ctx->mpv_gl, params);
    xwgl.BindFramebuffer(XW_GL_READ_FRAMEBUFFER, ctx->flip_fbo);
    xwgl.BindFramebuffer(XW_GL_DRAW_FRAMEBUFFER, 0);
    xwgl.BlitFramebuffer(0, 0, W, H, 0, H, W, 0,
                         XW_GL_COLOR_BUFFER_BIT, XW_GL_LINEAR);
    xwgl.BindFramebuffer(XW_GL_READ_FRAMEBUFFER, 0);
  } else {
    mpv_opengl_fbo fbo = {
      .fbo = 0, .w = W, .h = H, .internal_format = 0,
    };
    mpv_render_param params[] = {
      {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
      {MPV_RENDER_PARAM_INVALID, NULL},
    };
    mpv_render_context_render(ctx->mpv_gl, params);
  }
  eglSwapBuffers(ctx->egl_dpy, ctx->egl_surf);
}

/* offscreen flip target (context must be current) */
static void wl_flip_setup(VideoWlCtx *ctx, unsigned int w, unsigned int h) {
  ctx->flip_tex = 0;
  ctx->flip_fbo = 0;
  if (!ctx->gl_desktop)
    return;
  wl_gl_load();
  if (!xwgl_ok)
    return;
  xwgl.GenTextures(1, &ctx->flip_tex);
  xwgl.BindTexture(XW_GL_TEXTURE_2D, ctx->flip_tex);
  xwgl.TexParameteri(XW_GL_TEXTURE_2D, XW_GL_TEXTURE_MIN_FILTER, XW_GL_LINEAR);
  xwgl.TexParameteri(XW_GL_TEXTURE_2D, XW_GL_TEXTURE_MAG_FILTER, XW_GL_LINEAR);
  xwgl.TexImage2D(XW_GL_TEXTURE_2D, 0, XW_GL_RGBA8, (xw_GLsizei)w,
                  (xw_GLsizei)h, 0, XW_GL_RGBA, XW_GL_UNSIGNED_BYTE, NULL);
  xwgl.GenFramebuffers(1, &ctx->flip_fbo);
  xwgl.BindFramebuffer(XW_GL_FRAMEBUFFER, ctx->flip_fbo);
  xwgl.FramebufferTexture2D(XW_GL_FRAMEBUFFER, XW_GL_COLOR_ATTACHMENT0,
                            XW_GL_TEXTURE_2D, ctx->flip_tex, 0);
  if (xwgl.CheckFramebufferStatus(XW_GL_FRAMEBUFFER) !=
      XW_GL_FRAMEBUFFER_COMPLETE) {
    xwgl.DeleteFramebuffers(1, &ctx->flip_fbo);
    xwgl.DeleteTextures(1, &ctx->flip_tex);
    ctx->flip_fbo = 0;
    ctx->flip_tex = 0;
  }
  xwgl.BindFramebuffer(XW_GL_FRAMEBUFFER, 0);
}

static void wl_flip_teardown(VideoWlCtx *ctx) {
  if (!xwgl_ok)
    return;
  if (ctx->flip_fbo)
    xwgl.DeleteFramebuffers(1, &ctx->flip_fbo);
  if (ctx->flip_tex)
    xwgl.DeleteTextures(1, &ctx->flip_tex);
  ctx->flip_fbo = 0;
  ctx->flip_tex = 0;
}

static void wl_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
  VideoWlCtx *ctx = data;
  (void)time;
  wl_callback_destroy(cb);
  /* ctx may be dead (backend destroyed, refs keep the memory valid):
   * never touch mpv_gl/win/EGL below when dead. */
  if (!ctx->dead && ctx->pending && ctx->mpv_gl) {
    ctx->pending = 0;
    wl_paint(ctx);
    if (getenv("XW_DEBUG_DAEMON"))
      fprintf(stderr, "xwallpaperd: video render+swap %ux%u\n",
              ctx->win->width, ctx->win->height);
  }
  if (!ctx->dead && ctx->active) {
    ctx->frame_refs++;
    struct wl_surface *surf = (struct wl_surface *)ctx->win->surface;
    struct wl_callback *ncb = wl_surface_frame(surf);
    wl_callback_add_listener(ncb, &wl_frame_listener, ctx);
    wl_surface_commit(surf);
  }
  if (--ctx->frame_refs == 0 && ctx->dead)
    free(ctx);
}

static const struct wl_callback_listener wl_frame_listener = { wl_frame_done };

static void wl_kick(VideoWlCtx *ctx) {
  ctx->active = 1;
  ctx->pending = 1;
  /* First paint is synchronous: a bufferless layer surface never maps,
   * so no frame callback would ever arrive to start the chain (black
   * screen deadlock). Paint black-or-first-frame now; the callback
   * chain paces everything after. */
  if (ctx->mpv_gl)
    wl_paint(ctx);
  ctx->frame_refs++;
  struct wl_surface *surf = (struct wl_surface *)ctx->win->surface;
  struct wl_callback *cb = wl_surface_frame(surf);
  wl_callback_add_listener(cb, &wl_frame_listener, ctx);
  wl_surface_commit(surf);
}

static void wl_egl_teardown(VideoWlCtx *ctx) {
  if (ctx->egl_dpy == EGL_NO_DISPLAY)
    return;
  eglMakeCurrent(ctx->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);
  if (ctx->egl_surf != EGL_NO_SURFACE)
    eglDestroySurface(ctx->egl_dpy, ctx->egl_surf);
  if (ctx->egl_win)
    wl_egl_window_destroy(ctx->egl_win);
  if (ctx->egl_ctx != EGL_NO_CONTEXT)
    eglDestroyContext(ctx->egl_dpy, ctx->egl_ctx);
  eglTerminate(ctx->egl_dpy);
  ctx->egl_dpy = EGL_NO_DISPLAY;
}

static int wl_egl_setup(VideoWlCtx *ctx, struct wl_display *dpy,
                        unsigned int w, unsigned int h, XWError *err) {
  ctx->egl_dpy = EGL_NO_DISPLAY;
  ctx->egl_ctx = EGL_NO_CONTEXT;
  ctx->egl_surf = EGL_NO_SURFACE;
#ifdef EGL_VERSION_1_5
  ctx->egl_dpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, dpy, NULL);
#else
  ctx->egl_dpy = eglGetDisplay((EGLNativeDisplayType)dpy);
#endif
  if (ctx->egl_dpy == EGL_NO_DISPLAY) {
    XW_ERROR_SET(err, 1, "egl: no wayland display");
    return -1;
  }
  if (!eglInitialize(ctx->egl_dpy, NULL, NULL)) {
    XW_ERROR_SET(err, 1, "eglInitialize failed");
    ctx->egl_dpy = EGL_NO_DISPLAY;
    return -1;
  }

  EGLint cfg_attrs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE,
  };
  EGLConfig cfg;
  EGLint ncfg = 0;
  EGLint api = EGL_OPENGL_API;
  if (!eglChooseConfig(ctx->egl_dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg < 1) {
    cfg_attrs[11] = EGL_OPENGL_ES2_BIT;
    if (!eglChooseConfig(ctx->egl_dpy, cfg_attrs, &cfg, 1, &ncfg) ||
        ncfg < 1) {
      XW_ERROR_SET(err, 1, "egl: no suitable config");
      goto fail;
    }
    api = EGL_OPENGL_ES_API;
  }
  ctx->gl_desktop = (api == EGL_OPENGL_API);
  if (!eglBindAPI(api)) {
    XW_ERROR_SET(err, 1, "eglBindAPI failed");
    goto fail;
  }
  EGLint ctx_attrs[] = {EGL_NONE};
  EGLint es_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  ctx->egl_ctx = eglCreateContext(ctx->egl_dpy, cfg, EGL_NO_CONTEXT,
                                  api == EGL_OPENGL_ES_API ? es_attrs
                                                           : ctx_attrs);
  if (ctx->egl_ctx == EGL_NO_CONTEXT) {
    XW_ERROR_SET(err, 1, "eglCreateContext failed");
    goto fail;
  }
  struct wl_surface *surf = (struct wl_surface *)ctx->win->surface;
  ctx->egl_win = wl_egl_window_create(surf, (int)w, (int)h);
  if (!ctx->egl_win) {
    XW_ERROR_SET(err, 1, "wl_egl_window_create failed");
    goto fail;
  }
  ctx->egl_surf =
      eglCreateWindowSurface(ctx->egl_dpy, cfg, ctx->egl_win, NULL);
  if (ctx->egl_surf == EGL_NO_SURFACE) {
    XW_ERROR_SET(err, 1, "eglCreateWindowSurface failed");
    goto fail;
  }
  if (!eglMakeCurrent(ctx->egl_dpy, ctx->egl_surf, ctx->egl_surf,
                      ctx->egl_ctx)) {
    XW_ERROR_SET(err, 1, "eglMakeCurrent failed");
    goto fail;
  }
  eglSwapInterval(ctx->egl_dpy, 1);
  return 0;

fail:
  wl_egl_teardown(ctx);
  return -1;
}

static int video_init_wl(XWBackend *b, XWWlWindow *win,
                         const XWBackendOpt *opt, XWError *err) {
  if (win->width == 0 || win->height == 0) {
    XW_ERROR_SET(err, 1, "invalid window size");
    return -1;
  }
  struct wl_display *dpy = xw_wl_get_display((xw_wl_ctx *)win->wl_ctx);
  if (!dpy) {
    XW_ERROR_SET(err, 1, "wayland not connected");
    return -1;
  }
  VideoWlCtx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  ctx->win = win;
  ctx->base.mute = opt->mute;
  ctx->base.volume = opt->volume;
  ctx->base.scale_mode = opt->scale_mode;

  if (wl_egl_setup(ctx, dpy, win->width, win->height, err) != 0) {
    free(ctx);
    return -1;
  }
  wl_flip_setup(ctx, win->width, win->height);

  ctx->base.mpv = mpv_create();
  if (!ctx->base.mpv) {
    XW_ERROR_SET(err, 1, "mpv_create failed");
    wl_egl_teardown(ctx);
    free(ctx);
    return -1;
  }

  mpv_handle *mpv = ctx->base.mpv;
  mpv_set_option_string(mpv, "loop-file", "inf");
  mpv_set_option_string(mpv, "vo", "libmpv");
  mpv_set_option_string(mpv, "hwdec", "auto-safe");
  mpv_set_option_string(mpv, "input-default-bindings", "no");
  mpv_set_option_string(mpv, "input-vo-keyboard", "no");
  mpv_set_option_string(mpv, "osc", "no");
  mpv_set_option_string(mpv, "osd-level", "0");
  if (opt->audio_client_name && opt->audio_client_name[0])
    mpv_set_option_string(mpv, "audio-client-name", opt->audio_client_name);
  if (opt->fps > 0.0) {
    char vf[32];
    snprintf(vf, sizeof(vf), "fps=%g", opt->fps);
    mpv_set_option_string(mpv, "vf", vf);
  }
  if (getenv("XW_DEBUG_DAEMON")) {
    char logf[192];
    snprintf(logf, sizeof(logf), "/tmp/xw-mpv-wl-%.64s.log", win->output);
    mpv_set_option_string(mpv, "log-file", logf);
    mpv_request_log_messages(mpv, "debug");
  }
  if (ctx->base.scale_mode == XW_SCALE_FILL)
    mpv_set_option_string(mpv, "panscan", "1.0");
  else if (ctx->base.scale_mode == XW_SCALE_STRETCH)
    mpv_set_option_string(mpv, "keepaspect", "no");

  int merr = mpv_initialize(mpv);
  if (merr < 0) {
    XW_ERROR_SET(err, 1, "mpv_initialize failed: %s",
                 mpv_error_string(merr));
    mpv_terminate_destroy(mpv);
    wl_egl_teardown(ctx);
    free(ctx);
    return -1;
  }

  mpv_opengl_init_params gl_params = {
    .get_proc_address = wl_get_proc,
    .get_proc_address_ctx = NULL,
  };
  mpv_render_param render_params[] = {
    {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL},
    {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_params},
    {MPV_RENDER_PARAM_INVALID, NULL},
  };
  if (mpv_render_context_create(&ctx->mpv_gl, mpv, render_params) < 0) {
    XW_ERROR_SET(err, 1, "mpv_render_context_create failed");
    mpv_terminate_destroy(mpv);
    wl_egl_teardown(ctx);
    free(ctx);
    return -1;
  }
  mpv_render_context_set_update_callback(ctx->mpv_gl, wl_update_cb, ctx);

  if (ctx->base.mute) {
    mpv_set_property_string(mpv, "mute", "yes");
  } else {
    char vol[16];
    snprintf(vol, sizeof(vol), "%.1f", ctx->base.volume);
    mpv_set_property_string(mpv, "volume", vol);
  }
  if (opt->rotate != 0) {
    int64_t rot = opt->rotate;
    mpv_set_property(mpv, "video-rotate", MPV_FORMAT_INT64, &rot);
  }
  snprintf(ctx->base.path, sizeof(ctx->base.path), "%s", opt->path);
  b->priv = ctx;
  return 0;
}

static int video_start_wl(XWBackend *b) {
  VideoWlCtx *ctx = (VideoWlCtx *)b->priv;
  wl_load_file(ctx);
  wl_kick(ctx);
  return 0;
}

static void video_destroy_wl(XWBackend *b) {
  if (!b || !b->priv)
    return;
  VideoWlCtx *ctx = (VideoWlCtx *)b->priv;
  b->priv = NULL;
  ctx->dead = 1;
  ctx->active = 0;
  if (ctx->mpv_gl) {
    mpv_render_context_set_update_callback(ctx->mpv_gl, NULL, NULL);
    mpv_render_context_free(ctx->mpv_gl);
    ctx->mpv_gl = NULL;
  }
  if (ctx->base.mpv) {
    mpv_terminate_destroy(ctx->base.mpv);
    ctx->base.mpv = NULL;
  }
  if (ctx->egl_dpy != EGL_NO_DISPLAY)
    eglMakeCurrent(ctx->egl_dpy, ctx->egl_surf, ctx->egl_surf, ctx->egl_ctx);
  wl_flip_teardown(ctx);
  wl_egl_teardown(ctx);
  /* An in-flight frame.done still references ctx (frame_refs); the last
   * one frees it. Only free here when nothing is outstanding. */
  if (ctx->frame_refs == 0)
    free(ctx);
}
#endif /* XW_HAVE_WAYLAND */
