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

void xw_daemon_socket_path(char *out, size_t n) {
  const char *rt = getenv(XW_SOCKET_DIR_ENV);
  if (rt && rt[0])
    snprintf(out, n, "%s/%s/%s", rt, XW_SOCKET_SUBDIR, XW_SOCKET_NAME);
  else
    snprintf(out, n, "%s/%s/%s", "/tmp", XW_SOCKET_SUBDIR, XW_SOCKET_NAME);
}

static int append_field(char *msg, size_t n, size_t *off, const char *field) {
  size_t len = strlen(field);
  if (*off + len + 1 >= n) /* field + NUL, leave room for LF */
    return -1;
  memcpy(msg + *off, field, len);
  *off += len;
  msg[(*off)++] = '\0';
  return 0;
}

/* append a "key=value" field without a scratch buffer; fails on overflow
 * instead of truncating, so oversized values never silently corrupt the msg */
static int append_kv(char *msg, size_t n, size_t *off,
                     const char *key, const char *value) {
  size_t kl = strlen(key), vl = strlen(value);
  if (*off + kl + vl + 1 >= n)
    return -1;
  memcpy(msg + *off, key, kl);
  *off += kl;
  memcpy(msg + *off, value, vl);
  *off += vl;
  msg[(*off)++] = '\0';
  return 0;
}

static int finish(char *msg, size_t n, size_t *off) {
  if (*off + 1 >= n)
    return -1;
  msg[(*off)++] = '\n';
  msg[*off] = '\0';
  return (int)*off;
}

static const char *type_name(int kind) {
  switch (kind) {
  case XW_BACKEND_IMAGE: return "image";
  case XW_BACKEND_VIDEO: return "video";
  case XW_BACKEND_WEB:   return "web";
  default: return "unknown";
  }
}

static const char *scale_name(int mode) {
  switch (mode) {
  case XW_SCALE_STRETCH: return "stretch";
  case XW_SCALE_FILL:    return "fill";
  case XW_SCALE_TILE:    return "tile";
  default:               return "contain";
  }
}

static int encode_common(const char *verb, const XWConfig *cfg,
                         char *msg, size_t n) {
  size_t off = 0;
  if (append_field(msg, n, &off, verb) < 0)
    return -1;

  if (strcmp(verb, "clear") == 0) {
    const char *t = (cfg->target == XW_TARGET_OUTPUT) ? cfg->output : "all";
    if (append_field(msg, n, &off, t) < 0)
      return -1;
    return finish(msg, n, &off);
  }

  /* set */
  const char *target;
  if (cfg->target == XW_TARGET_ALL)
    target = "all";
  else if (cfg->target == XW_TARGET_GEOM)
    target = "geom";
  else
    target = cfg->output[0] ? cfg->output : "primary";
  if (append_field(msg, n, &off, target) < 0)
    return -1;
  if (append_field(msg, n, &off, type_name(cfg->backend_explicit < 0
        ? xw_cli_detect_backend(cfg->path) : cfg->backend_explicit)) < 0)
    return -1;
  if (append_field(msg, n, &off, cfg->path) < 0)
    return -1;

  char kv[128];
  snprintf(kv, sizeof(kv), "scale=%s", scale_name(cfg->scale_mode));
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  snprintf(kv, sizeof(kv), "mute=%d", cfg->mute);
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  if (cfg->rotate != 0) {
    snprintf(kv, sizeof(kv), "rotate=%d", cfg->rotate);
    if (append_field(msg, n, &off, kv) < 0)
      return -1;
  }
  if (cfg->keybinds[0]) {
    if (append_kv(msg, n, &off, "keybinds=", cfg->keybinds) < 0)
      return -1;
  }
  if (cfg->fps > 0.0) {
    snprintf(kv, sizeof(kv), "fps=%g", cfg->fps);
    if (append_field(msg, n, &off, kv) < 0)
      return -1;
  }
  if (cfg->audio_client_name[0]) {
    if (append_kv(msg, n, &off, "audio_client_name=", cfg->audio_client_name) < 0)
      return -1;
  }
  snprintf(kv, sizeof(kv), "volume=%.0f", cfg->volume);
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  snprintf(kv, sizeof(kv), "opacity=%.3f", cfg->opacity);
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  if (cfg->below)     { if (append_field(msg, n, &off, "below=1") < 0) return -1; }
  if (cfg->sticky)    { if (append_field(msg, n, &off, "sticky=1") < 0) return -1; }
  if (cfg->skip_taskbar) { if (append_field(msg, n, &off, "skip_taskbar=1") < 0) return -1; }
  if (cfg->skip_pager)   { if (append_field(msg, n, &off, "skip_pager=1") < 0) return -1; }
  if (cfg->no_focus)  { if (append_field(msg, n, &off, "no_focus=1") < 0) return -1; }
  if (cfg->have_geom) {
    char g[64];
    snprintf(g, sizeof(g), "geom=%dx%d+%d+%d",
             cfg->geom_w, cfg->geom_h, cfg->geom_x, cfg->geom_y);
    if (append_field(msg, n, &off, g) < 0)
      return -1;
  }
  if (cfg->name[0]) {
    if (append_kv(msg, n, &off, "name=", cfg->name) < 0)
      return -1;
  }
  return finish(msg, n, &off);
}

int xw_proto_encode_set(const XWConfig *cfg, char *msg, size_t n) {
  return encode_common("set", cfg, msg, n);
}

int xw_proto_encode_clear(const XWConfig *cfg, char *msg, size_t n) {
  return encode_common("clear", cfg, msg, n);
}

int xw_proto_encode_list(char *msg, size_t n) {
  return snprintf(msg, n, "list\n") < (int)n ? (int)strlen("list\n") : -1;
}

int xw_proto_encode_exit(char *msg, size_t n) {
  return snprintf(msg, n, "exit\n") < (int)n ? (int)strlen("exit\n") : -1;
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
  xfd = ConnectionNumber(xw_app_display(app));

  signal(SIGTERM, daemon_signal);
  signal(SIGINT, daemon_signal);

  while (xw_app_get() && !xw_app_stopped(xw_app_get())) {
    XWApp *cur = xw_app_get();
    if (!cur || xw_app_stopped(cur))
      break;

    /* Poll our X connection + the socket + the GTK/GDK main-context fds.
     * GDK has its own X connection; polling it lets webkit frame events
     * wake us. Cap the timeout so animation frames aren't starved. */
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

    int timeout = (gtimeout >= 0 && gtimeout < 32) ? gtimeout : 16;
    int r = poll(fds, nfds, timeout);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      break;
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
            xw_app_clear(cur, &cfg, &aerr);
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
                size_t olen = strlen(oname), tlen = strlen(type), plen = strlen(win->path);
                size_t line_len = olen + 1 + tlen + 1 + plen + 1;
                if (off + line_len < XW_REPLY_MAX) {
                  memcpy(out + off, oname, olen);
                  off += olen;
                  out[off++] = '\0';
                  memcpy(out + off, type, tlen);
                  off += tlen;
                  out[off++] = '\0';
                  memcpy(out + off, win->path, plen);
                  off += plen;
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
