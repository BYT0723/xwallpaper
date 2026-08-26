#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>
#include <glib.h>

#include "app.h"
#include "backends.h"
#include "state.h"
#include "x11.h"

struct XWApp {
  Display *dpy;
  XWWindow **wins;
  XWBackend **backends;
  int nwins;
  int stop_requested;
  int uses_gtk;
  /* DPMS state (lazily probed; -1 power level = never polled yet) */
  int dpms_available;
  int dpms_queried;
  int dpms_power_level;
  uint64_t dpms_last_poll_ms;
};

static XWApp *the_app = NULL;

static void apply_dpms_to_backend(XWApp *app, XWBackend *b);

static XWBackend *make_backend(const XWConfig *cfg, XWError *err) {
  int k = cfg->backend_explicit;
  if (k < 0) {
    k = xw_cli_detect_backend(cfg->path);
    if (k < 0) {
      XW_ERROR_SET(err, 1, "cannot determine backend for '%.100s' "
                   "(use --image/--video/--web)", cfg->path);
      return NULL;
    }
  }
  switch (k) {
  case XW_BACKEND_IMAGE: return xw_image_backend_new();
  case XW_BACKEND_VIDEO: return xw_video_backend_new();
  case XW_BACKEND_WEB:   return xw_web_backend_new();
  default:
    XW_ERROR_SET(err, 1, "unknown backend kind %d", k);
    return NULL;
  }
}

static void fill_backend_opt(XWBackendOpt *opt, const XWConfig *cfg) {
  memset(opt, 0, sizeof(*opt));
  opt->type = cfg->backend_explicit < 0
                ? xw_cli_detect_backend(cfg->path)
                : cfg->backend_explicit;
  opt->path = cfg->path;
  opt->scale_mode = cfg->scale_mode;
  opt->mute = cfg->mute;
  opt->volume = cfg->volume;
  opt->rotate = cfg->rotate;
  opt->keybinds = cfg->keybinds[0] ? cfg->keybinds : NULL;
  opt->fps = cfg->fps;
  opt->audio_client_name = cfg->audio_client_name[0] ? cfg->audio_client_name : NULL;
}

/* Find a window by its identity key. Windows are identified by win->output:
 * real outputs use the xrandr name, -g windows use their --name.
 * Returns index or -1. */
static int find_window_by_key(XWApp *app, const char *key) {
  if (!key || !key[0])
    return -1;
  for (int i = 0; i < app->nwins; i++) {
    XWWindow *win = app->wins[i];
    if (win && win->output[0] && strcmp(win->output, key) == 0)
      return i;
  }
  return -1;
}

/* Resolve the identity key a config addresses: real outputs use the xrandr
 * name, -g windows use their --name. Returns "" if no key. */
static void window_key(const XWConfig *cfg, const char *resolved_output,
                       char *out, size_t n) {
  if (cfg->have_geom)
    snprintf(out, n, "%s", cfg->name);
  else if (cfg->target == XW_TARGET_OUTPUT || cfg->target == XW_TARGET_PRIMARY)
    snprintf(out, n, "%s", resolved_output[0] ? resolved_output : cfg->output);
  else
    out[0] = '\0';
}

/* remove window index i (destroy backend + window, compact arrays) */
static void remove_window(XWApp *app, int i) {
  if (app->backends[i])
    app->backends[i]->destroy(app->backends[i]);
  if (app->wins[i])
    xw_x11_destroy_window(app->wins[i]);
  app->wins[i] = app->wins[app->nwins - 1];
  app->backends[i] = app->backends[app->nwins - 1];
  app->nwins--;
}

/* create a fresh window + backend for the given rect/output */
static int create_window(XWApp *app, const XWConfig *cfg,
                         int x, int y, unsigned int w, unsigned int h,
                         const char *output, int is_geom, XWError *err) {
  XWWindow *win = xw_x11_create_window(app->dpy, cfg, x, y, w, h, err);
  if (!win)
    return -1;
  if (output)
    snprintf(win->output, sizeof(win->output), "%s", output);
  win->is_geometry = is_geom;

  XWBackend *tmpl = make_backend(cfg, err);
  if (!tmpl) {
    xw_x11_destroy_window(win);
    return -1;
  }
  XWBackend *b = malloc(sizeof(*b));
  if (!b) {
    xw_x11_destroy_window(win);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  *b = *tmpl;
  b->priv = NULL;

  XWBackendOpt opt;
  fill_backend_opt(&opt, cfg);
  if (b->init(b, win, &opt, err) != 0) {
    free(b);
    xw_x11_destroy_window(win);
    return -1;
  }
  if (b->uses_gtk)
    app->uses_gtk = 1;
  win->type = opt.type;
  snprintf(win->path, sizeof(win->path), "%s", opt.path ? opt.path : "");
  win->rotation = cfg->rotate;

  app->wins = realloc(app->wins, sizeof(XWWindow *) * (app->nwins + 1));
  app->backends = realloc(app->backends, sizeof(XWBackend *) * (app->nwins + 1));
  if (!app->wins || !app->backends) {
    b->destroy(b);
    xw_x11_destroy_window(win);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  app->wins[app->nwins] = win;
  app->backends[app->nwins] = b;
  app->nwins++;
  return 0;
}

XWApp *xw_app_create(XWError *err) {
  Display *dpy = xw_x11_open_display(err);
  if (!dpy)
    return NULL;
  XWApp *app = calloc(1, sizeof(*app));
  if (!app) {
    XW_ERROR_SET(err, 1, "out of memory");
    xw_x11_close_display(dpy);
    return NULL;
  }
  app->dpy = dpy;
  app->dpms_power_level = -1;
  the_app = app;
  return app;
}

int xw_app_set(XWApp *app, const XWConfig *cfg, XWError *err) {
  if (cfg->target == XW_TARGET_ALL) {
    /* replace all windows: one per connected output */
    xw_app_clear_all(app);
    Window root = RootWindow(app->dpy, DefaultScreen(app->dpy));
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(app->dpy, root);
    if (!res) {
      XW_ERROR_SET(err, 1, "xrandr: no outputs");
      return -1;
    }
    for (int i = 0; i < res->noutput; i++) {
      XRROutputInfo *oi = XRRGetOutputInfo(app->dpy, res, res->outputs[i]);
      if (!oi)
        continue;
      if (oi->crtc && oi->connection == RR_Connected) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo(app->dpy, res, oi->crtc);
        if (ci) {
          if (create_window(app, cfg, ci->x, ci->y, ci->width, ci->height,
                            oi->name, 0, err) != 0) {
            XRRFreeCrtcInfo(ci);
            XRRFreeOutputInfo(oi);
            XRRFreeScreenResources(res);
            return -1;
          }
          XRRFreeCrtcInfo(ci);
        }
      }
      XRRFreeOutputInfo(oi);
    }
    XRRFreeScreenResources(res);
    if (app->nwins == 0) {
      XW_ERROR_SET(err, 1, "no connected outputs found");
      return -1;
    }
    for (int i = 0; i < app->nwins; i++) {
      if (app->backends[i]->start(app->backends[i]) != 0) {
        XW_ERROR_SET(err, 1, "backend failed to start");
        return -1;
      }
      apply_dpms_to_backend(app, app->backends[i]);
    }
    return 0;
  }

  /* single window: output or geom (primary resolves to its output name) */
  int x, y;
  unsigned int w, h;
  char name[XW_OUTPUT_NAME_LEN] = {0};
  if (xw_x11_resolve_geometry(app->dpy, cfg, &x, &y, &w, &h, name) != 0) {
    XW_ERROR_SET(err, 1, "cannot resolve output '%s' "
                 "(use --list to see available outputs)", cfg->output);
    return -1;
  }

  char lookup[XW_OUTPUT_NAME_LEN] = {0};
  window_key(cfg, name, lookup, sizeof(lookup));
  int idx = find_window_by_key(app, lookup);

  if (idx >= 0) {
    /* 同类型切换: 复用 backend 只换资源 (mpv loadfile / 图片重绘 / webkit 加载),
     * 避免 destroy+init 重建 GL/mpv 上下文 — 后者在单线程 daemon 里可能阻塞
     * 整个事件循环 (表现为切换壁纸卡死)。 */
    XWBackendOpt opt;
    fill_backend_opt(&opt, cfg);
    XWBackend *old = app->backends[idx];
    if (old && old->reload && app->wins[idx]->type == opt.type) {
      if (old->reload(old, &opt, err) != 0)
        return -1; /* 失败保留旧壁纸, 窗口元数据也保持旧值 */
      app->wins[idx]->type = opt.type;
      snprintf(app->wins[idx]->path, sizeof(app->wins[idx]->path), "%s",
               opt.path ? opt.path : "");
      app->wins[idx]->rotation = cfg->rotate;
      apply_dpms_to_backend(app, old);
      return 0;
    }
    /* 不同类型才销毁重建 */
    if (old)
      old->destroy(old);

    XWBackend *tmpl = make_backend(cfg, err);
    if (!tmpl)
      return -1;
    XWBackend *b = malloc(sizeof(*b));
    if (!b) {
      XW_ERROR_SET(err, 1, "out of memory");
      return -1;
    }
    *b = *tmpl;
    b->priv = NULL;
    fill_backend_opt(&opt, cfg);
    if (b->init(b, app->wins[idx], &opt, err) != 0) {
      free(b);
      return -1;
    }
    if (b->uses_gtk)
      app->uses_gtk = 1;
    app->backends[idx] = b;
    app->wins[idx]->type = opt.type;
    snprintf(app->wins[idx]->path, sizeof(app->wins[idx]->path), "%s",
             opt.path ? opt.path : "");
    app->wins[idx]->rotation = cfg->rotate;
    if (b->start(b) != 0) {
      b->destroy(b);
      app->backends[idx] = NULL;
      XW_ERROR_SET(err, 1, "backend failed to start");
      return -1;
    }
    apply_dpms_to_backend(app, b);
    return 0;
  }

  /* no existing window: create one. -g windows carry their --name as the
   * identity (win->output); real outputs carry the xrandr name. */
  const char *out;
  if (cfg->have_geom)
    out = cfg->name[0] ? cfg->name : NULL;
  else
    out = name[0] ? name : NULL;
  if (create_window(app, cfg, x, y, w, h, out, cfg->have_geom, err) != 0)
    return -1;
  if (app->backends[app->nwins - 1]->start(app->backends[app->nwins - 1]) != 0) {
    remove_window(app, app->nwins - 1);
    XW_ERROR_SET(err, 1, "backend failed to start");
    return -1;
  }
  apply_dpms_to_backend(app, app->backends[app->nwins - 1]);
  return 0;
}

int xw_app_clear(XWApp *app, const XWConfig *cfg, XWError *err) {
  (void)err;
  if (cfg->target == XW_TARGET_ALL) {
    xw_app_clear_all(app);
    return 0;
  }
  /* resolve the output name so PRIMARY matches the actual primary output */
  char key[XW_OUTPUT_NAME_LEN] = {0};
  if (cfg->have_geom) {
    snprintf(key, sizeof(key), "%s", cfg->name);
  } else {
    int x, y;
    unsigned int w, h;
    char name[XW_OUTPUT_NAME_LEN] = {0};
    if (xw_x11_resolve_geometry(app->dpy, cfg, &x, &y, &w, &h, name) == 0)
      window_key(cfg, name, key, sizeof(key));
    else
      snprintf(key, sizeof(key), "%s", cfg->output);
  }
  int idx = find_window_by_key(app, key);
  if (idx >= 0)
    remove_window(app, idx);
  return 0;
}

int xw_app_clear_all(XWApp *app) {
  while (app->nwins > 0)
    remove_window(app, 0);
  return 0;
}

void xw_app_stop(XWApp *app) {
  app->stop_requested = 1;
}

int xw_app_stopped(XWApp *app) {
  return app->stop_requested;
}

int xw_app_window_count(XWApp *app) {
  return app->nwins;
}

XWWindow *xw_app_window(XWApp *app, int i) {
  return app->wins[i];
}

Display *xw_app_display(XWApp *app) {
  return app->dpy;
}

int xw_app_handle_event(XWApp *app, XEvent *ev) {
  for (int i = 0; i < app->nwins; i++) {
    XWWindow *win = app->wins[i];
    if (win && ev->xany.window == win->win) {
      xw_x11_handle_event(win, ev);
      if (app->backends[i] && app->backends[i]->on_event)
        app->backends[i]->on_event(app->backends[i], ev);
      return 1;
    }
  }
  return 0;
}

#define DPMS_POLL_INTERVAL_MS 500

static uint64_t now_monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* Distribute the current DPMS state to one backend. Newly created backends
 * must start paused if the display is already powered off, otherwise they
 * keep presenting frames until the next state change. */
static void apply_dpms_to_backend(XWApp *app, XWBackend *b) {
  if (app->dpms_power_level >= 0 && b && b->set_paused)
    b->set_paused(b, app->dpms_power_level != DPMSModeOn);
}

void xw_app_dpms_poll(XWApp *app) {
  if (!app)
    return;
  if (!app->dpms_queried) {
    app->dpms_queried = 1;
    /* libXext's DPMSQueryExtension writes both bases unconditionally —
     * pass real locals, never NULL */
    int ev_base, err_base;
    app->dpms_available =
        DPMSQueryExtension(app->dpy, &ev_base, &err_base);
  }
  if (!app->dpms_available)
    return;
  uint64_t now = now_monotonic_ms();
  if (now - app->dpms_last_poll_ms < DPMS_POLL_INTERVAL_MS)
    return;
  app->dpms_last_poll_ms = now;

  CARD16 power_level;
  BOOL state;
  if (!DPMSInfo(app->dpy, &power_level, &state))
    return;
  if (power_level == app->dpms_power_level)
    return;
  app->dpms_power_level = power_level;

  /* every powered-off mode (standby/suspend/off) must stop presenting */
  for (int i = 0; i < app->nwins; i++)
    apply_dpms_to_backend(app, app->backends[i]);
}

static void pump_gtk(void) {
  /* non-blocking: drain sources that are ready now */
  g_main_context_iteration(NULL, FALSE);
}

int xw_app_run(XWApp *app) {
  int xfd = ConnectionNumber(app->dpy);

  while (!app->stop_requested) {
    if (app->uses_gtk)
      pump_gtk();
    xw_app_dpms_poll(app);

    struct pollfd pfd = {xfd, POLLIN, 0};
    int r = poll(&pfd, 1, 100); /* 100ms timeout keeps signals responsive */

    if (r < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (!(pfd.revents & POLLIN))
      continue;

    while (XPending(app->dpy) > 0 && !app->stop_requested) {
      XEvent ev;
      XNextEvent(app->dpy, &ev);
      xw_app_handle_event(app, &ev);
    }
  }
  return 0;
}

XWApp *xw_app_get(void) {
  return the_app;
}

void xw_app_destroy(XWApp *app) {
  if (!app)
    return;
  xw_app_clear_all(app);
  free(app->backends);
  free(app->wins);
  if (app->dpy)
    xw_x11_close_display(app->dpy);
  if (the_app == app)
    the_app = NULL;
  free(app);
}

/* ------------------------------------------------------------------ *
 *  Persistent per-name state
 * ------------------------------------------------------------------ */

static void entry_from_window(XWStateEntry *e, const XWWindow *win, int keep) {
  memset(e, 0, sizeof(*e));
  snprintf(e->name, sizeof(e->name), "%s",
           win->output[0] ? win->output : "unnamed");
  e->type = win->type;
  snprintf(e->path, sizeof(e->path), "%s", win->path);
  e->rotation = win->rotation;
  /* 总是保存几何; 恢复时若 name 命中当前 xrandr output 则重解析 */
  e->have_geom = 1;
  e->x = win->x;
  e->y = win->y;
  e->w = win->width;
  e->h = win->height;
  e->keep = keep;
}

int xw_app_save_state(XWApp *app) {
  char path[1024];
  if (xw_state_path(path, sizeof(path)) != 0)
    return -1;

  /* keep=1 且无活跃窗口的旧条目 (clear --keep 保留的 last) 继续保留 */
  XWStateEntry old[XW_STATE_MAX_ENTRIES];
  int on = 0;
  xw_state_load(path, old, XW_STATE_MAX_ENTRIES, &on);

  XWStateEntry out[XW_STATE_MAX_ENTRIES];
  int n = 0;
  for (int i = 0; i < on && n < XW_STATE_MAX_ENTRIES; i++) {
    if (old[i].keep && find_window_by_key(app, old[i].name) < 0)
      out[n++] = old[i];
  }
  for (int i = 0; i < app->nwins && n < XW_STATE_MAX_ENTRIES; i++) {
    entry_from_window(&out[n], app->wins[i], 0);
    n++;
  }
  return xw_state_save(path, out, n);
}

int xw_app_keep(XWApp *app, const char *name) {
  char path[1024];
  if (xw_state_path(path, sizeof(path)) != 0)
    return -1;
  int idx = find_window_by_key(app, name);
  if (idx < 0)
    return 0; /* nothing to keep */
  XWStateEntry e;
  entry_from_window(&e, app->wins[idx], 1);
  return xw_state_upsert(path, &e);
}

int xw_app_keep_all(XWApp *app) {
  int r = 0;
  for (int i = 0; i < app->nwins; i++) {
    if (app->wins[i]->output[0] && xw_app_keep(app, app->wins[i]->output) != 0)
      r = -1;
  }
  return r;
}

/* 重建一个状态条目为活跃窗口。
 * 优先把 name 当作 xrandr output 解析 (几何实时), 失败再回退保存的 geom
 * (grp_* / Screen 这类 -g 窗口)。返回 0 成功或跳过, -1 失败。 */
static int restore_entry(XWApp *app, const XWStateEntry *e) {
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: restore: %s type=%d path=%s\n", e->name,
            e->type, e->path);
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.command = XW_CMD_SET;
  cfg.backend_explicit = e->type;
  /* 与 CLI set 默认一致: memset(0) 会漏掉 opacity/scale_mode/volume 等,
   * 尤其 opacity=0 会让窗口被设 _NET_WM_WINDOW_OPACITY=0 → 合成器下全透明 */
  cfg.scale_mode = XW_SCALE_FILL;
  cfg.volume = 100.0;
  cfg.opacity = 1.0;
  snprintf(cfg.audio_client_name, sizeof(cfg.audio_client_name), "xwallpaper");
  snprintf(cfg.path, sizeof(cfg.path), "%s", e->path);
  cfg.rotate = e->rotation;
  if (e->type == XW_BACKEND_VIDEO)
    cfg.mute = 1; /* 壁纸视频恢复时不应出声 */

  cfg.target = XW_TARGET_OUTPUT;
  /* %.*s: 显式边界 (最多读 sizeof(e->name)-1), 避免 %s 触发
   * -Wformat-truncation 保守告警 (目标 output[64] 恰好容纳) */
  snprintf(cfg.output, sizeof(cfg.output), "%.*s", (int)sizeof(e->name) - 1,
           e->name);
  int x, y;
  unsigned int w, h;
  char oname[XW_OUTPUT_NAME_LEN];
  const char *mode = "output";
  if (xw_x11_resolve_geometry(app->dpy, &cfg, &x, &y, &w, &h, oname) != 0 ||
      !oname[0]) {
    if (!e->have_geom) {
      if (getenv("XW_DEBUG_DAEMON"))
        fprintf(stderr,
                "xwallpaperd: restore: %s skip (not an output, no saved geom)\n",
                e->name);
      return 0; /* 既非 output 也无保存几何: 跳过 */
    }
    mode = "geom";
    cfg.target = XW_TARGET_GEOM;
    cfg.have_geom = 1;
    cfg.geom_x = e->x;
    cfg.geom_y = e->y;
    cfg.geom_w = e->w;
    cfg.geom_h = e->h;
    snprintf(cfg.name, sizeof(cfg.name), "%.*s", (int)sizeof(e->name) - 1,
             e->name);
  }
  XWError err;
  memset(&err, 0, sizeof(err));
  int r = xw_app_set(app, &cfg, &err);
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: restore: %s %s %s (%s)\n", e->name, mode,
            r == 0 ? "ok" : "FAIL", r == 0 ? "" : err.msg);
  return r == 0 ? 0 : -1;
}

/* keep_flag 为 0 时重建当前窗口 (启动恢复), 为 1 时重建 kept last (restore
 * 命令)。两者共享同一恢复流程, 仅过滤条件与是否回写快照不同。 */
static int restore_entries(XWApp *app, int keep_flag) {
  char path[1024];
  if (xw_state_path(path, sizeof(path)) != 0)
    return -1;
  XWStateEntry entries[XW_STATE_MAX_ENTRIES];
  int count = 0;
  if (xw_state_load(path, entries, XW_STATE_MAX_ENTRIES, &count) != 0) {
    if (getenv("XW_DEBUG_DAEMON"))
      fprintf(stderr, "xwallpaperd: restore: no usable state file (%s)\n",
              path);
    return 0; /* 无可用状态 */
  }
  if (getenv("XW_DEBUG_DAEMON"))
    fprintf(stderr, "xwallpaperd: restore: keep=%d path=%s entries=%d\n",
            keep_flag, path, count);
  for (int i = 0; i < count; i++) {
    if (entries[i].keep != keep_flag)
      continue;
    if (find_window_by_key(app, entries[i].name) >= 0) {
      if (getenv("XW_DEBUG_DAEMON"))
        fprintf(stderr,
                "xwallpaperd: restore: %s skip (already an active window)\n",
                entries[i].name);
      continue; /* 已是活跃窗口 */
    }
    restore_entry(app, &entries[i]);
  }
  if (keep_flag)
    return xw_app_save_state(
        app); /* 活跃窗口变 keep=0, 未恢复的 keep 条目保持 keep=1 */
  return 0;
}

int xw_app_restore_startup(XWApp *app) { return restore_entries(app, 0); }

int xw_app_restore_kept(XWApp *app) { return restore_entries(app, 1); }
