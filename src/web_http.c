#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <pthread.h>

#include <gio/gio.h>
#include <glib.h>

#include "web_http.h"
#include "xw_err.h"

struct XWWebHttp {
  char *root;
  GSocketService *svc; /* owned by the server thread while running */
  GMainLoop *loop;     /* owned by the server thread while running */
  GThread *thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  int ready;
  int preferred; /* bind wish from the main thread, 0 = ephemeral */
  char fail[256];
  volatile gint port;
  volatile gint active; /* in-flight connection workers */
  char *aliases[XW_HTTP_ALIAS_MAX];
  int n_alias;
};

const char *xw_web_http_mime(const char *path) {
  static const struct {
    const char *ext, *mime;
  } kTab[] = {
    {"html", "text/html; charset=utf-8"},
    {"htm", "text/html; charset=utf-8"},
    {"css", "text/css"},
    {"js", "text/javascript"},
    {"mjs", "text/javascript"},
    {"json", "application/json"},
    {"wasm", "application/wasm"},
    {"mp4", "video/mp4"},
    {"webm", "video/webm"},
    {"ogv", "video/ogg"},
    {"mp3", "audio/mpeg"},
    {"ogg", "audio/ogg"},
    {"wav", "audio/wav"},
    {"flac", "audio/flac"},
    {"png", "image/png"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"gif", "image/gif"},
    {"webp", "image/webp"},
    {"svg", "image/svg+xml"},
    {"ico", "image/x-icon"},
    {"woff", "font/woff"},
    {"woff2", "font/woff2"},
    {"ttf", "font/ttf"},
    {"otf", "font/otf"},
    {"txt", "text/plain; charset=utf-8"},
    {"xml", "application/xml"},
    {"map", "application/json"},
  };
  const char *dot;
  size_t i;
  if (!path)
    return "application/octet-stream";
  dot = strrchr(path, '/');
  dot = strrchr(dot ? dot : path, '.');
  if (!dot || !dot[1])
    return "application/octet-stream";
  for (i = 0; i < sizeof(kTab) / sizeof(kTab[0]); i++)
    if (strcasecmp(dot + 1, kTab[i].ext) == 0)
      return kTab[i].mime;
  return "application/octet-stream";
}

int xw_web_http_resolve(const char *root, const char *target, char *out,
                        size_t n) {
  char *path = NULL, *dec = NULL;
  size_t need;
  int rc = -1;
  if (!root || !root[0] || !target || target[0] != '/' || !out || n == 0)
    return -1;
  path = g_strdup(target);
  if (!path)
    return -1;
  {
    char *q = strchr(path, '?');
    if (q)
      *q = '\0';
    q = strchr(path, '#');
    if (q)
      *q = '\0';
  }
  dec = g_uri_unescape_string(path, NULL);
  g_free(path);
  if (!dec || dec[0] != '/')
    goto out;
  {
    const char *p = dec;
    while (*p) {
      const char *slash;
      size_t len;
      while (*p == '/')
        p++;
      if (!*p)
        break;
      slash = strchr(p, '/');
      len = slash ? (size_t)(slash - p) : strlen(p);
      if (len == 2 && p[0] == '.' && p[1] == '.') {
        rc = -2;
        goto out;
      }
      if (!slash)
        break;
      p = slash + 1;
    }
  }
  need = strlen(root) + strlen(dec) + 16;
  if (need > n)
    goto out;
  if (strcmp(dec, "/") == 0)
    snprintf(out, n, "%s/index.html", root);
  else
    snprintf(out, n, "%s%s", root, dec);
  rc = 0;
out:
  g_free(dec);
  return rc;
}

int xw_web_http_parse_range(const char *hdr, long long size, long long *a,
                            long long *b) {
  long long lo = 0, hi = -1;
  const char *p;
  char *end, *e2;
  if (!hdr || !a || !b || size <= 0)
    return -1;
  if (strncmp(hdr, "bytes=", 6) != 0)
    return -1;
  p = hdr + 6;
  if (strchr(p, ','))
    return -1;
  if (*p == '-') {
    lo = (long long)strtoll(p + 1, &end, 10);
    if (end == p + 1 || *end || lo <= 0)
      return -1;
    if (lo > size)
      lo = size;
    *a = size - lo;
    *b = size - 1;
    return 0;
  }
  lo = (long long)strtoll(p, &end, 10);
  if (end == p || lo < 0)
    return -1;
  if (*end == '\0')
    hi = size - 1;
  else if (*end == '-') {
    if (*(end + 1) == '\0')
      hi = size - 1;
    else {
      hi = (long long)strtoll(end + 1, &e2, 10);
      if (e2 == end + 1 || *e2)
        return -1;
    }
  } else {
    return -1;
  }
  if (lo >= size || hi < lo)
    return -1;
  if (hi >= size)
    hi = size - 1;
  *a = lo;
  *b = hi;
  return 0;
}

static void http_write(GOutputStream *os, const void *buf, size_t n) {
  gsize wrote = 0;
  if (n == 0)
    return;
  g_output_stream_write_all(os, buf, n, &wrote, NULL, NULL);
}

static void http_send_file(GOutputStream *os, int head_only,
                           const char *full, long long lo, long long hi,
                           int partial) {
  FILE *f = fopen(full, "rb");
  char hdr[512];
  struct stat st;
  long long size, len;
  int n;
  if (!f)
    return;
  if (stat(full, &st) != 0) {
    fclose(f);
    return;
  }
  size = (long long)st.st_size;
  len = hi - lo + 1;
  if (partial)
    n = snprintf(hdr, sizeof(hdr),
                 "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %lld\r\n"
                 "Content-Range: bytes %lld-%lld/%lld\r\n"
                 "Accept-Ranges: bytes\r\n"
                 "Connection: close\r\n\r\n",
                 xw_web_http_mime(full), len, lo, hi, size);
  else
    n = snprintf(hdr, sizeof(hdr),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %lld\r\n"
                 "Accept-Ranges: bytes\r\n"
                 "Connection: close\r\n\r\n",
                 xw_web_http_mime(full), len);
  http_write(os, hdr, (size_t)n);
  if (!head_only && fseek(f, lo, SEEK_SET) == 0) {
    char buf[65536];
    while (len > 0) {
      size_t want =
          len > (long long)sizeof(buf) ? sizeof(buf) : (size_t)len;
      size_t nr = fread(buf, 1, want, f);
      if (nr == 0)
        break;
      http_write(os, buf, nr);
      len -= (long long)nr;
    }
  }
  fclose(f);
}

static void http_send_status(GOutputStream *os, int code, const char *phrase,
                             const char *extra) {
  char hdr[512];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Length: 0\r\n"
                   "%s"
                   "Connection: close\r\n\r\n",
                   code, phrase, extra ? extra : "");
  http_write(os, hdr, (size_t)n);
}

int xw_web_http_alias(XWWebHttp *h, const char *abs_path) {
  struct stat st;
  int i, id = -1;
  if (!h || !abs_path || abs_path[0] != '/')
    return -1;
  if (stat(abs_path, &st) != 0 || !S_ISREG(st.st_mode))
    return -1;
  pthread_mutex_lock(&h->lock);
  for (i = 0; i < h->n_alias; i++) {
    if (strcmp(h->aliases[i], abs_path) == 0) {
      id = i;
      break;
    }
  }
  if (id < 0 && h->n_alias < XW_HTTP_ALIAS_MAX) {
    char *copy = strdup(abs_path);
    if (copy) {
      id = h->n_alias;
      h->aliases[id] = copy;
      h->n_alias++;
    }
  }
  pthread_mutex_unlock(&h->lock);
  return id;
}

/* Alias route: /_xw/file/<id> maps to a registered absolute path.
 * The path is copied to the caller's buffer under the lock; entries
 * themselves are immutable and freed only at stop (after all workers
 * drained). */
static int http_alias_lookup(XWWebHttp *h, const char *target,
                             char *out, size_t n) {
  static const char *kPre = "/_xw/file/";
  const char *p;
  char *end;
  long id;
  const char *path = NULL;
  if (!h || !target || !out || n == 0)
    return -1;
  if (strncmp(target, kPre, strlen(kPre)) != 0)
    return -1;
  p = target + strlen(kPre);
  if (*p < '0' || *p > '9')
    return -1;
  id = strtol(p, &end, 10);
  if (*end != '\0' && *end != '?' && *end != '#')
    return -1;
  pthread_mutex_lock(&h->lock);
  if (id >= 0 && id < h->n_alias)
    path = h->aliases[id];
  if (path && snprintf(out, n, "%s", path) >= (int)n)
    path = NULL;
  pthread_mutex_unlock(&h->lock);
  return path ? 0 : -1;
}

/* Connection worker: blocking I/O runs here so an idle peer (TCP
 * preconnect, half-open socket) can never stall the server thread, let
 * alone the daemon's GTK main loop. */
typedef struct {
  XWWebHttp *h;
  GSocketConnection *conn;
} XWConnTask;

static void http_serve(XWWebHttp *h, GSocketConnection *conn) {
  const char *root = h ? h->root : NULL;
  GInputStream *is = g_io_stream_get_input_stream(G_IO_STREAM(conn));
  GOutputStream *os = g_io_stream_get_output_stream(G_IO_STREAM(conn));
  char req[65536];
  gsize total = 0;
  char *eoh, *line_end, *method, *target, *sp;
  char full[8192];
  char abuf[4096];
  const char *serve = full;
  struct stat st;
  int head_only = 0;
  if (!root)
    return;
  /* Idle peers (TCP preconnects, half-open sockets) must be reaped instead
   * of parking a worker forever. */
  g_socket_set_timeout(g_socket_connection_get_socket(conn), 10);
  for (;;) {
    gssize nr = g_input_stream_read(is, req + total,
                                    sizeof(req) - 1 - total, NULL, NULL);
    if (nr <= 0)
      goto done;
    total += (gsize)nr;
    req[total] = '\0';
    if (total >= sizeof(req) - 1)
      break;
    if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n"))
      break;
  }
  eoh = strstr(req, "\r\n\r\n");
  if (!eoh)
    eoh = strstr(req, "\n\n");
  if (!eoh) {
    http_send_status(os, 400, "Bad Request", NULL);
    goto done;
  }
  line_end = strstr(req, "\r\n");
  if (!line_end)
    line_end = strchr(req, '\n');
  *line_end = '\0';
  method = req;
  target = strchr(method, ' ');
  if (!target) {
    http_send_status(os, 400, "Bad Request", NULL);
    goto done;
  }
  *target++ = '\0';
  sp = strchr(target, ' ');
  if (sp)
    *sp = '\0';
  if (strcasecmp(method, "GET") == 0)
    head_only = 0;
  else if (strcasecmp(method, "HEAD") == 0)
    head_only = 1;
  else {
    http_send_status(os, 405, "Method Not Allowed",
                     "Allow: GET, HEAD\r\n");
    goto done;
  }
  /* Alias URLs are registered explicitly (WE file properties outside
   * the root), so they bypass the root-containment check by design. */
  if (http_alias_lookup(h, target, abuf, sizeof(abuf)) == 0) {
    serve = abuf;
  } else {
    {
      int rc = xw_web_http_resolve(root, target, full, sizeof(full));
      if (rc == -2) {
        http_send_status(os, 403, "Forbidden", NULL);
        goto done;
      }
      if (rc != 0) {
        http_send_status(os, 400, "Bad Request", NULL);
        goto done;
      }
    }
    {
      char *rp = realpath(full, NULL);
      size_t rl = strlen(root);
      int inside;
      if (!rp) {
        http_send_status(os, 404, "Not Found", NULL);
        goto done;
      }
      inside = strncmp(rp, root, rl) == 0 &&
               (rp[rl] == '/' || rp[rl] == '\0');
      free(rp);
      if (!inside) {
        http_send_status(os, 403, "Forbidden", NULL);
        goto done;
      }
    }
  }
  if (stat(serve, &st) != 0 || !S_ISREG(st.st_mode)) {
    http_send_status(os, 404, "Not Found", NULL);
    goto done;
  }
  {
    long long size = (long long)st.st_size, a = 0, b = size - 1;
    int partial = 0;
    char *val = NULL;
    char *l = line_end + 1;
    while (l < eoh) {
      char *nl = strchr(l, '\n');
      char *name = l;
      char *end;
      if (!nl || nl > eoh + 1)
        break;
      end = nl;
      while (end > name && (end[-1] == '\r' || end[-1] == ' ' ||
                            end[-1] == '\t'))
        end--;
      while (*name == ' ' || *name == '\t')
        name++;
      if (strncasecmp(name, "Range:", 6) == 0) {
        val = name + 6;
        while (*val == ' ' || *val == '\t')
          val++;
        *end = '\0';
        break;
      }
      l = nl + 1;
    }
    if (val && xw_web_http_parse_range(val, size, &a, &b) == 0)
      partial = 1;
    else {
      a = 0;
      b = size - 1;
    }
    if (size == 0) {
      http_send_file(os, head_only, serve, 0, -1, 0);
      goto done;
    }
    http_send_file(os, head_only, serve, a, b, partial);
  }
done:
  g_socket_close(g_socket_connection_get_socket(conn), NULL);
}

static gpointer http_worker(gpointer ud) {
  XWConnTask *t = (XWConnTask *)ud;
  http_serve(t->h, t->conn);
  g_object_unref(t->conn);
  g_atomic_int_dec_and_test(&t->h->active);
  free(t);
  return NULL;
}

/* Runs in the server thread: never blocks, just hands the connection to a
 * worker. GSocketService serializes nothing for us; one idle peer must not
 * starve the page's other subresources. */
static gboolean http_incoming(GSocketService *svc, GSocketConnection *conn,
                              GObject *src, gpointer ud) {
  XWWebHttp *h = (XWWebHttp *)ud;
  XWConnTask *t = malloc(sizeof(*t));
  GThread *th;
  (void)svc;
  (void)src;
  if (!t)
    return TRUE;
  t->h = h;
  t->conn = g_object_ref(conn);
  g_atomic_int_inc(&h->active);
  th = g_thread_new("xw-http-conn", http_worker, t);
  if (th)
    g_thread_unref(th);
  else {
    g_object_unref(t->conn);
    g_atomic_int_dec_and_test(&h->active);
    free(t);
  }
  return TRUE;
}

/* The service lives on its own thread with a private main context, so no
 * blocking socket work ever runs on the daemon's GTK main loop. */
static gpointer http_server_thread(gpointer ud) {
  XWWebHttp *h = (XWWebHttp *)ud;
  GMainContext *mctx = g_main_context_new();
  GSocketAddress *addr = NULL;
  GInetAddress *loop = NULL;
  GError *gerr = NULL;
  g_main_context_push_thread_default(mctx);
  h->svc = g_socket_service_new();
  if (h->svc) {
    GSocket *sock =
        g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
                     G_SOCKET_PROTOCOL_TCP, &gerr);
    if (!sock) {
      pthread_mutex_lock(&h->lock);
      snprintf(h->fail, sizeof(h->fail), "socket new failed: %s",
               gerr ? gerr->message : "?");
      pthread_mutex_unlock(&h->lock);
      g_clear_error(&gerr);
      g_object_unref(h->svc);
      h->svc = NULL;
    } else {
      /* SO_REUSEADDR (via allow_reuse) so a restart rebinds its stable
       * port despite TIME_WAIT leftovers; a taken preferred port falls
       * back to ephemeral instead of failing. */
      int try_port =
          (h->preferred > 0 && h->preferred <= 65535) ? h->preferred : 0;
      loop = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
      for (;;) {
        addr = g_inet_socket_address_new(loop, (guint16)try_port);
        if (g_socket_bind(sock, addr, TRUE, &gerr))
          break;
        g_object_unref(addr);
        addr = NULL;
        if (try_port == 0)
          break;
        g_clear_error(&gerr);
        try_port = 0;
      }
      g_object_unref(loop);
      loop = NULL;
      if (!addr) {
        pthread_mutex_lock(&h->lock);
        snprintf(h->fail, sizeof(h->fail), "bind 127.0.0.1 failed: %s",
                 gerr ? gerr->message : "?");
        pthread_mutex_unlock(&h->lock);
        g_clear_error(&gerr);
        g_object_unref(h->svc);
        h->svc = NULL;
      } else if (!g_socket_listen(sock, &gerr)) {
        pthread_mutex_lock(&h->lock);
        snprintf(h->fail, sizeof(h->fail), "listen failed: %s",
                 gerr ? gerr->message : "?");
        pthread_mutex_unlock(&h->lock);
        g_clear_error(&gerr);
        g_object_unref(h->svc);
        h->svc = NULL;
      } else if (!g_socket_listener_add_socket(G_SOCKET_LISTENER(h->svc),
                                               sock, NULL, &gerr)) {
        pthread_mutex_lock(&h->lock);
        snprintf(h->fail, sizeof(h->fail), "add socket failed: %s",
                 gerr ? gerr->message : "?");
        pthread_mutex_unlock(&h->lock);
        g_clear_error(&gerr);
        g_object_unref(h->svc);
        h->svc = NULL;
      } else {
        GSocketAddress *local = g_socket_get_local_address(sock, &gerr);
        if (!local) {
          pthread_mutex_lock(&h->lock);
          snprintf(h->fail, sizeof(h->fail), "local address failed: %s",
                   gerr ? gerr->message : "?");
          pthread_mutex_unlock(&h->lock);
          g_clear_error(&gerr);
          g_object_unref(h->svc);
          h->svc = NULL;
        } else {
          g_atomic_int_set(&h->port,
                           g_inet_socket_address_get_port(
                               G_INET_SOCKET_ADDRESS(local)));
          g_object_unref(local);
        }
      }
      g_object_unref(sock);
    }
  } else {
    pthread_mutex_lock(&h->lock);
    snprintf(h->fail, sizeof(h->fail), "service new failed");
    pthread_mutex_unlock(&h->lock);
  }
  if (addr)
    g_object_unref(addr);
  if (h->svc && g_atomic_int_get(&h->port) > 0) {
    g_signal_connect(h->svc, "incoming", G_CALLBACK(http_incoming), h);
    g_socket_service_start(h->svc);
    pthread_mutex_lock(&h->lock);
    h->loop = g_main_loop_new(mctx, FALSE);
    pthread_mutex_unlock(&h->lock);
  }
  pthread_mutex_lock(&h->lock);
  h->ready = 1;
  pthread_cond_broadcast(&h->cond);
  pthread_mutex_unlock(&h->lock);
  if (h->loop)
    g_main_loop_run(h->loop);
  if (h->svc) {
    /* Stop first so failed accepts are not re-armed, then close to fail
     * the one pending async accept, then drain its completion on this
     * context: without a running loop the GTask and its sources would
     * never complete and leak per wallpaper switch. */
    int i;
    g_socket_service_stop(h->svc);
    g_socket_listener_close(G_SOCKET_LISTENER(h->svc));
    for (i = 0; i < 1000 && g_main_context_pending(mctx); i++)
      g_main_context_iteration(mctx, FALSE);
    g_object_unref(h->svc);
    h->svc = NULL;
  }
  pthread_mutex_lock(&h->lock);
  if (h->loop) {
    g_main_loop_unref(h->loop);
    h->loop = NULL;
  }
  pthread_mutex_unlock(&h->lock);
  g_main_context_pop_thread_default(mctx);
  g_main_context_unref(mctx);
  return NULL;
}

XWWebHttp *xw_web_http_start(const char *root, int preferred_port,
                             XWError *err) {
  XWWebHttp *h = NULL;
  char *abs = NULL;
  struct stat st;
  if (!root || !root[0]) {
    XW_ERROR_SET(err, 1, "web http: empty root");
    return NULL;
  }
  abs = realpath(root, NULL);
  if (!abs || stat(abs, &st) != 0 || !S_ISDIR(st.st_mode)) {
    XW_ERROR_SET(err, 1, "web http: bad root '%.100s'", root);
    free(abs);
    return NULL;
  }
  h = calloc(1, sizeof(*h));
  if (!h) {
    XW_ERROR_SET(err, 1, "out of memory");
    free(abs);
    return NULL;
  }
  h->root = abs;
  h->preferred = preferred_port;
  pthread_mutex_init(&h->lock, NULL);
  pthread_cond_init(&h->cond, NULL);
  h->thread = g_thread_new("xw-http", http_server_thread, h);
  if (!h->thread) {
    XW_ERROR_SET(err, 1, "web http: thread spawn failed");
    pthread_cond_destroy(&h->cond);
    pthread_mutex_destroy(&h->lock);
    free(h->root);
    free(h);
    return NULL;
  }
  pthread_mutex_lock(&h->lock);
  while (!h->ready)
    pthread_cond_wait(&h->cond, &h->lock);
  {
    int port = g_atomic_int_get(&h->port);
    char fail[256];
    snprintf(fail, sizeof(fail), "%s", h->fail);
    pthread_mutex_unlock(&h->lock);
    if (!fail[0] && port <= 0)
      snprintf(fail, sizeof(fail), "no port");
    if (fail[0]) {
      XW_ERROR_SET(err, 1, "web http: %.200s", fail);
      g_thread_join(h->thread);
      pthread_cond_destroy(&h->cond);
      pthread_mutex_destroy(&h->lock);
      free(h->root);
      free(h);
      return NULL;
    }
  }
  return h;
}

int xw_web_http_port(const XWWebHttp *h) {
  return h ? g_atomic_int_get(&h->port) : -1;
}

void xw_web_http_stop(XWWebHttp *h) {
  GMainLoop *loop;
  int i;
  if (!h)
    return;
  pthread_mutex_lock(&h->lock);
  loop = h->loop;
  pthread_mutex_unlock(&h->lock);
  if (loop)
    g_main_loop_quit(loop);
  if (h->thread)
    g_thread_join(h->thread);
  for (i = 0; i < 30000 && g_atomic_int_get(&h->active) > 0; i++)
    g_usleep(1000);
  for (i = 0; i < h->n_alias; i++)
    free(h->aliases[i]);
  pthread_cond_destroy(&h->cond);
  pthread_mutex_destroy(&h->lock);
  free(h->root);
  free(h);
}
