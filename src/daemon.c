#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>

#include "app.h"
#include "daemon.h"
#include "proto.h"
#include "x11.h"

static void daemon_sigsegv(int sig) {
  (void)sig;
  fprintf(stderr, "xwallpaperd: FATAL: caught signal %d\n", sig);
  _exit(1);
}

/* Build an error reply: "error" NUL <msg> LF. Returns bytes (excl NUL term). */
static int reply_error(char *buf, size_t n, const char *msg) {
  size_t off = 0;
  const char *tag = "error";
  memcpy(buf + off, tag, 5);
  off += 5;
  buf[off++] = '\0';
  size_t ml = strlen(msg);
  if (off + ml + 1 < n) {
    memcpy(buf + off, msg, ml);
    off += ml;
    buf[off++] = '\n';
  }
  buf[off] = '\0';
  return (int)off;
}

/* ------------------------------------------------------------------ *
 *  Decode: parse a NUL-separated command message into XWConfig.
 * ------------------------------------------------------------------ */

typedef struct {
  char *fields[64]; /* a command has at most ~15 fields */
  int nfields;
} Decoded;

static void split_fields(char *msg, size_t n, Decoded *d) {
  d->nfields = 0;
  size_t off = 0;
  while (off < n && d->nfields < (int)(sizeof(d->fields) / sizeof(d->fields[0]))) {
    d->fields[d->nfields++] = msg + off;
    const char *end = memchr(msg + off, '\0', n - off);
    if (!end)
      break;
    off = (size_t)(end + 1 - msg);
  }
}

static int kv_int(const Decoded *d, const char *key, int *out) {
  size_t kl = strlen(key);
  for (int i = 0; i < d->nfields; i++) {
    if (strncmp(d->fields[i], key, kl) == 0 && d->fields[i][kl] == '=') {
      *out = atoi(d->fields[i] + kl + 1);
      return 0;
    }
  }
  return -1;
}

static int kv_str(const Decoded *d, const char *key, char *out, size_t on) {
  size_t kl = strlen(key);
  for (int i = 0; i < d->nfields; i++) {
    if (strncmp(d->fields[i], key, kl) == 0 && d->fields[i][kl] == '=') {
      snprintf(out, on, "%s", d->fields[i] + kl + 1);
      return 0;
    }
  }
  return -1;
}

static int type_from_name(const char *n) {
  if (strcmp(n, "image") == 0) return XW_BACKEND_IMAGE;
  if (strcmp(n, "video") == 0) return XW_BACKEND_VIDEO;
  if (strcmp(n, "web") == 0)   return XW_BACKEND_WEB;
  return -1;
}

static int scale_from_name(const char *n) {
  if (strcmp(n, "stretch") == 0) return XW_SCALE_STRETCH;
  if (strcmp(n, "contain") == 0) return XW_SCALE_CONTAIN;
  if (strcmp(n, "tile") == 0)    return XW_SCALE_TILE;
  return XW_SCALE_FILL;
}

static int decode_set(char *msg, size_t n, XWConfig *cfg, char *errbuf, size_t en) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->command = XW_CMD_SET;
  cfg->backend_explicit = -1;
  cfg->scale_mode = XW_SCALE_FILL;
  cfg->volume = 100.0;
  cfg->opacity = 1.0;
  snprintf(cfg->audio_client_name, sizeof(cfg->audio_client_name), "xwallpaper");

  Decoded d;
  split_fields(msg, n, &d);
  if (d.nfields < 4) {
    snprintf(errbuf, en, "malformed set command");
    return -1;
  }
  /* d.fields[0] = "set", [1]=target, [2]=type, [3]=path */

  if (strcmp(d.fields[1], "all") == 0) {
    cfg->target = XW_TARGET_ALL;
  } else if (strcmp(d.fields[1], "geom") == 0) {
    cfg->target = XW_TARGET_GEOM;
  } else if (strcmp(d.fields[1], "primary") == 0) {
    cfg->target = XW_TARGET_PRIMARY;
  } else {
    cfg->target = XW_TARGET_OUTPUT;
    snprintf(cfg->output, sizeof(cfg->output), "%s", d.fields[1]);
  }

  int kind = type_from_name(d.fields[2]);
  if (kind < 0) {
    snprintf(errbuf, en, "unknown backend type '%s'", d.fields[2]);
    return -1;
  }
  cfg->backend_explicit = kind;
  snprintf(cfg->path, sizeof(cfg->path), "%s", d.fields[3]);

  int v;
  char scaleval[16];
  if (kv_str(&d, "scale", scaleval, sizeof(scaleval)) == 0)
    cfg->scale_mode = scale_from_name(scaleval);
  if (kv_int(&d, "mute", &v) == 0) cfg->mute = v;
  if (kv_int(&d, "rotate", &v) == 0) {
    if (v == 0 || v == 90 || v == 180 || v == 270)
      cfg->rotate = v;
  }
  char kb[1024];
  if (kv_str(&d, "keybinds", kb, sizeof(kb)) == 0)
    snprintf(cfg->keybinds, sizeof(cfg->keybinds), "%s", kb);
  double dv;
  char fp[32];
  if (kv_str(&d, "fps", fp, sizeof(fp)) == 0) {
    dv = atof(fp);
    if (dv > 0.0) cfg->fps = dv;
  }
  char vol[32];
  if (kv_str(&d, "volume", vol, sizeof(vol)) == 0) {
    dv = atof(vol);
    if (dv >= 0 && dv <= 100) cfg->volume = dv;
  }
  char acn[64];
  if (kv_str(&d, "audio_client_name", acn, sizeof(acn)) == 0)
    snprintf(cfg->audio_client_name, sizeof(cfg->audio_client_name), "%s", acn);
  char op[32];
  if (kv_str(&d, "opacity", op, sizeof(op)) == 0) {
    dv = atof(op);
    if (dv >= 0 && dv <= 1) cfg->opacity = dv;
  }
  if (kv_int(&d, "below", &v) == 0) cfg->below = v;
  if (kv_int(&d, "sticky", &v) == 0) cfg->sticky = v;
  if (kv_int(&d, "skip_taskbar", &v) == 0) cfg->skip_taskbar = v;
  if (kv_int(&d, "skip_pager", &v) == 0) cfg->skip_pager = v;
  if (kv_int(&d, "no_focus", &v) == 0) cfg->no_focus = v;

  char g[64];
  if (kv_str(&d, "geom", g, sizeof(g)) == 0) {
    if (xw_geom_parse(g, &cfg->geom_x, &cfg->geom_y, &cfg->geom_w, &cfg->geom_h))
      cfg->have_geom = 1;
  }
  char nm[64];
  if (kv_str(&d, "name", nm, sizeof(nm)) == 0)
    snprintf(cfg->name, sizeof(cfg->name), "%s", nm);
  return 0;
}

static int decode_clear(char *msg, size_t n, XWConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->command = XW_CMD_CLEAR;
  Decoded d;
  split_fields(msg, n, &d);
  if (d.nfields < 2 || strcmp(d.fields[1], "all") == 0) {
    cfg->target = XW_TARGET_ALL;
  } else {
    cfg->target = XW_TARGET_OUTPUT;
    snprintf(cfg->output, sizeof(cfg->output), "%s", d.fields[1]);
  }
  int keep = 0;
  if (kv_int(&d, "keep", &keep) == 0)
    cfg->keep = keep;
  return 0;
}

/* ------------------------------------------------------------------ *
 *  Server loop
 * ------------------------------------------------------------------ */

static int xfd = -1;
static int server_fd = -1;

static void daemon_signal(int sig) {
  (void)sig;
  XWApp *app = xw_app_get();
  if (app)
    xw_app_stop(app);
}

int xw_daemon_main(void) {
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");
  setvbuf(stderr, NULL, _IONBF, 0);
  /* a daemon must never die on SIGPIPE (writing to a closed client conn) */
  signal(SIGPIPE, SIG_IGN);
  signal(SIGSEGV, daemon_sigsegv);
  signal(SIGABRT, daemon_sigsegv);

  char path[XW_MSG_MAX];
  xw_daemon_socket_path(path, sizeof(path));
  char dir[XW_MSG_MAX];
  xw_daemon_socket_path(dir, sizeof(dir));
  char *slash = strrchr(dir, '/');
  if (slash)
    *slash = '\0';
  mkdir(dir, 0700);
  /* 单例: 若已有 daemon 正在监听同一 socket, 直接退出而非 unlink+bind
   * 抢占 (否则旧 daemon 变孤儿: 监听已删 inode, 空转渲染但收不到请求) */
  int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe >= 0) {
    struct sockaddr_un pa;
    memset(&pa, 0, sizeof(pa));
    pa.sun_family = AF_UNIX;
    snprintf(pa.sun_path, sizeof(pa.sun_path), "%.107s", path);
    if (connect(probe, (struct sockaddr *)&pa, sizeof(pa)) == 0) {
      close(probe);
      return 0; /* daemon 已存在 */
    }
    close(probe);
  }
  unlink(path);

  server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server_fd < 0) {
    fprintf(stderr, "xwallpaperd: socket: %s\n", strerror(errno));
    return 1;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", path);
  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "xwallpaperd: bind %s: %s\n", path, strerror(errno));
    close(server_fd);
    return 1;
  }
  listen(server_fd, 4);

  XWError err;
  memset(&err, 0, sizeof(err));
  XWApp *app = xw_app_create(&err);
  if (!app) {
    fprintf(stderr, "xwallpaperd: %s\n", err.msg);
    close(server_fd);
    unlink(path);
    return 1;
  }
  /* 恢复上次会话的壁纸 (keep=0 条目) */
  struct timespec rt0, rt1;
  clock_gettime(CLOCK_MONOTONIC, &rt0);
  xw_app_restore_startup(app);
  if (getenv("XW_DEBUG_DAEMON")) {
    clock_gettime(CLOCK_MONOTONIC, &rt1);
    fprintf(stderr, "xwallpaperd: restore startup done in %.3fs\n",
            (double)(rt1.tv_sec - rt0.tv_sec) +
                (double)(rt1.tv_nsec - rt0.tv_nsec) / 1e9);
  }
  xfd = ConnectionNumber(xw_app_display(app));

  signal(SIGTERM, daemon_signal);
  signal(SIGINT, daemon_signal);

  /* 首次启动时 GPU/GL 可能尚未就绪, 视频 backend init 失败 → 部分 restore
   * 条目未恢复 (pending>0), 或 mpv 因 swapchain 失败丢掉 video track (窗口在、
   * 只剩音频)。启动后 2 分钟窗口内以 0.5s 节拍轮询检测并自愈 — GPU 就绪后
   * 立即出视频, 避免黑屏/明显滞后。 */
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  int64_t startup_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  int64_t last_selfheal_ms = startup_ms;

  while (xw_app_get() && !xw_app_stopped(xw_app_get())) {
    XWApp *cur = xw_app_get();
    if (!cur || xw_app_stopped(cur))
      break;

    /* Poll our X connection + the socket + the GTK/GDK main-context fds.
     * GDK has its own X connection; polling it lets webkit frame events
     * wake us.
     *
     * 自适应 poll 超时: 默认无限阻塞 (gtimeout=-1), 事件到达才醒 — 旧的固定
     * 16ms 下限会让常驻 daemon 空转时每秒无谓醒来 ~60 次 (实测 ~0.2% CPU)。
     * webkit 动画与 GDK frame clock 是 glib 定时器/可轮询 fd, 由 gtimeout/
     * gfds 反映, 不会被饿死。 */
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;

    gint gprio = G_PRIORITY_DEFAULT, gtimeout = -1;
    g_main_context_prepare(NULL, &gprio);
    GPollFD gfds[8];
    int gnfds = g_main_context_query(NULL, gprio, &gtimeout, gfds, 8);

    struct pollfd fds[2 + gnfds];
    int nfds = 0;
    fds[nfds].fd = server_fd;
    fds[nfds].events = POLLIN;
    fds[nfds].revents = 0;
    nfds++;
    fds[nfds].fd = xfd;
    fds[nfds].events = POLLIN;
    fds[nfds].revents = 0;
    nfds++;
    for (int i = 0; i < gnfds; i++) {
      fds[nfds].fd = gfds[i].fd;
      fds[nfds].events = gfds[i].events;
      fds[nfds].revents = 0;
      nfds++;
    }

    int timeout = gtimeout; /* -1 = block until an fd/event arrives */
    if (xw_app_dpms_watch_required(cur) || now_ms - startup_ms < 120000) {
      /* 需要退化为 500ms 节拍: (a) 有持续出帧的渲染后端 (video) — 必须轮询
       * DPMS, 熄屏时暂停它防止点亮显示器; (b) 启动自愈窗口 (前 2 分钟) — 原
       * 逻辑窗口内本就每 500ms 检测 (restore_pending 读盘 + video retry),
       * 保持该节拍避免自愈被无限阻塞饿死。窗口仅 2 分钟, 2Hz 唤醒成本可忽略;
       * restore_pending 读盘只在该节拍触发时进行, 勿提前到每次迭代。 */
      if (timeout < 0 || timeout > 500)
        timeout = 500;
    }
    int r = poll(fds, nfds, timeout);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    /* pause/resume rendering backends when the display DPMS state changes
     * (throttled internally; keeps video wallpapers from re-waking the
     * display after lock/screen-off) */
    xw_app_dpms_poll(cur);

    if (now_ms - startup_ms < 120000) {
      if (now_ms - last_selfheal_ms >= 500) {
        last_selfheal_ms = now_ms;
        if (xw_app_restore_pending(cur) > 0) {
          if (getenv("XW_DEBUG_DAEMON"))
            fprintf(stderr, "xwallpaperd: retry pending restore\n");
          xw_app_restore_startup(cur);
        }
        int v = xw_app_retry_backends(cur);
        if (v > 0 && getenv("XW_DEBUG_DAEMON"))
          fprintf(stderr, "xwallpaperd: retry %d video(s) without frames\n",
                  v);
      }
    }

    for (int i = 0; i < gnfds; i++)
      gfds[i].revents = fds[2 + i].revents;
    g_main_context_check(NULL, gprio, gfds, gnfds);
    while (g_main_context_pending(NULL))
      g_main_context_iteration(NULL, FALSE);

    if (fds[1].revents & POLLIN) {
      Display *dpy = xw_app_display(cur);
      while (dpy && XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        xw_app_handle_event(cur, &ev);
      }
    }

    if (fds[0].revents & POLLIN) {
      int c = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
      if (c >= 0) {
        /* don't block on a client that connects but never sends: poll the
         * connection for data with a short timeout, then read what arrived */
        struct pollfd cf = {c, POLLIN, 0};
        int cr = poll(&cf, 1, 2000);
        char msg[XW_MSG_MAX];
        ssize_t r;
        if (cr <= 0)
          r = 0;
        else
          r = read(c, msg, sizeof(msg) - 1);
        char reply[XW_REPLY_MAX];
        int reply_n = 0;
        int do_exit = 0;
        if (r <= 0) {
          reply_n = reply_error(reply, sizeof(reply), "bad request");
        } else {
          if (strncmp(msg, "set", 3) == 0) {
            XWConfig cfg;
            char ebuf[256];
            if (decode_set(msg, (size_t)r, &cfg, ebuf, sizeof(ebuf)) != 0) {
              reply_n = reply_error(reply, sizeof(reply), ebuf);
            } else {
              if (getenv("XW_DEBUG_DAEMON"))
                fprintf(stderr, "xwallpaperd: set target=%d type=%d path=%s\n",
                        cfg.target, cfg.backend_explicit, cfg.path);
              XWError aerr;
              memset(&aerr, 0, sizeof(aerr));
              struct timespec t0, t1;
              clock_gettime(CLOCK_MONOTONIC, &t0);
              if (xw_app_set(cur, &cfg, &aerr) == 0) {
                reply_n = snprintf(reply, sizeof(reply), "ok\n");
                xw_app_save_state(cur);
              } else {
                reply_n = reply_error(reply, sizeof(reply), aerr.msg);
              }
              if (getenv("XW_DEBUG_DAEMON")) {
                clock_gettime(CLOCK_MONOTONIC, &t1);
                fprintf(stderr, "xwallpaperd: set done in %.3fs\n",
                        (double)(t1.tv_sec - t0.tv_sec) +
                        (double)(t1.tv_nsec - t0.tv_nsec) / 1e9);
              }
            }
          } else if (strncmp(msg, "clear", 5) == 0) {
            XWConfig cfg;
            decode_clear(msg, (size_t)r, &cfg);
            XWError aerr;
            memset(&aerr, 0, sizeof(aerr));
            /* clear --keep: 先把目标窗口状态持久化为 keep=1 (保留 last),
             * 再清窗口; restore 命令可将其恢复 */
            if (cfg.keep) {
              if (cfg.target == XW_TARGET_ALL)
                xw_app_keep_all(cur);
              else if (cfg.output[0])
                xw_app_keep(cur, cfg.output);
            }
            xw_app_clear(cur, &cfg, &aerr);
            xw_app_save_state(cur);
            reply_n = snprintf(reply, sizeof(reply), "ok\n");
          } else if (strncmp(msg, "restore", 7) == 0) {
            xw_app_restore_kept(cur);
            reply_n = snprintf(reply, sizeof(reply), "ok\n");
          } else if (strncmp(msg, "list", 4) == 0) {
            int nw = xw_app_window_count(cur);
            /* build the whole reply into one buffer, then send once */
            char *out = malloc(XW_REPLY_MAX);
            if (!out) {
              reply_n = reply_error(reply, sizeof(reply), "out of memory");
            } else {
              size_t off = 0;
              memcpy(out, "list", 4);
              off += 4;
              out[off++] = '\0';
              off += (size_t)snprintf(out + off, XW_REPLY_MAX - off, "%d\n", nw);
              for (int i = 0; i < nw; i++) {
                XWWindow *win = xw_app_window(cur, i);
                char type[16];
                switch (win->type) {
                case XW_BACKEND_IMAGE: strcpy(type, "image"); break;
                case XW_BACKEND_VIDEO: strcpy(type, "video"); break;
                case XW_BACKEND_WEB:   strcpy(type, "web"); break;
                default: strcpy(type, "unknown"); break;
                }
                /* identity: xrandr name for outputs, --name for -g windows */
                const char *oname = win->output[0] ? win->output : "unnamed";
                char rot[16];
                snprintf(rot, sizeof(rot), "%d", win->rotation);
                size_t olen = strlen(oname), tlen = strlen(type), plen = strlen(win->path);
                size_t rlen = strlen(rot);
                size_t line_len = olen + 1 + tlen + 1 + plen + 1 + rlen + 1;
                if (off + line_len < XW_REPLY_MAX) {
                  memcpy(out + off, oname, olen);
                  off += olen;
                  out[off++] = '\0';
                  memcpy(out + off, type, tlen);
                  off += tlen;
                  out[off++] = '\0';
                  memcpy(out + off, win->path, plen);
                  off += plen;
                  out[off++] = '\0';
                  memcpy(out + off, rot, rlen);
                  off += rlen;
                  out[off++] = '\n';
                }
              }
              write(c, out, off);
              free(out);
              close(c);
              continue;
            }
          } else if (strncmp(msg, "exit", 4) == 0) {
            reply_n = snprintf(reply, sizeof(reply), "ok\n");
            do_exit = 1;
          } else {
            reply_n = reply_error(reply, sizeof(reply), "unknown command");
          }
        }
        if (reply_n > 0)
          write(c, reply, (size_t)reply_n);
        close(c);
        if (do_exit) {
          xw_app_stop(cur);
          break;
        }
      }
    }
  }

  xw_app_destroy(app);
  close(server_fd);
  unlink(path);
  return 0;
}
