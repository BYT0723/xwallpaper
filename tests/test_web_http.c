#include "harness.h"
#include "../src/web_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

static void test_keepalive(void);
static void test_keepalive_range(void);
static void test_dir_mount(void);

static void test_mime(void) {
  TEST_BEGIN("wasm/js/html mime types map correctly");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.wasm"), "application/wasm");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.WASM"), "application/wasm");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.js"), "text/javascript");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.mjs"), "text/javascript");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.html"), "text/html; charset=utf-8");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.css"), "text/css");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.json"), "application/json");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.data"), "application/octet-stream");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.mp4"), "video/mp4");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.webm"), "video/webm");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.png"), "image/png");
  TEST_CHECK_EQ_STR(xw_web_http_mime("a.unknownext"), "application/octet-stream");
  TEST_CHECK_EQ_STR(xw_web_http_mime("noext"), "application/octet-stream");
}

static void test_resolve(void) {
  char out[4096];
  TEST_BEGIN("url target resolves inside root, traversal rejected");
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/app.wasm", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "/r/app.wasm");
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/app.wasm?v=2", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "/r/app.wasm");
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/sub/dir/file.js", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "/r/sub/dir/file.js");
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/%41pp.wasm", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "/r/App.wasm");
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/../etc/passwd", out, sizeof(out)), -2);
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/%2e%2e/x", out, sizeof(out)), -2);
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "/sub/../../x", out, sizeof(out)), -2);
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "app.wasm", out, sizeof(out)), -1);
  TEST_CHECK_EQ_INT(xw_web_http_resolve("/r", "", out, sizeof(out)), -1);
}

static void test_range(void) {
  long long a = -1, b = -1;
  TEST_BEGIN("single byte range parses, garbage rejected");
  TEST_CHECK_EQ_INT(xw_web_http_parse_range("bytes=0-99", 1000, &a, &b), 0);
  TEST_CHECK_EQ_INT(a, 0);
  TEST_CHECK_EQ_INT(b, 99);
  TEST_CHECK_EQ_INT(xw_web_http_parse_range("bytes=500-", 1000, &a, &b), 0);
  TEST_CHECK_EQ_INT(a, 500);
  TEST_CHECK_EQ_INT(b, 999);
  TEST_CHECK_EQ_INT(xw_web_http_parse_range("bytes=-100", 1000, &a, &b), 0);
  TEST_CHECK_EQ_INT(a, 900);
  TEST_CHECK_EQ_INT(b, 99 + 900);
  TEST_CHECK_EQ_INT(xw_web_http_parse_range("bytes=999-0", 1000, &a, &b), -1);
  TEST_CHECK_EQ_INT(xw_web_http_parse_range("bytes=1000-", 1000, &a, &b), -1);
  TEST_CHECK_EQ_INT(xw_web_http_parse_range("items=0-1", 1000, &a, &b), -1);
  TEST_CHECK_EQ_INT(xw_web_http_parse_range(NULL, 1000, &a, &b), -1);
}

static void test_alias(void) {
  XWWebHttp *h = NULL;
  XWError err;
  char *a = NULL, *b = NULL;
  TEST_BEGIN("file aliases map absolute paths to stable ids");
  memset(&err, 0, sizeof(err));
  h = xw_web_http_start("tests/data/webunity", 0, &err);
  TEST_CHECK(h != NULL);
  if (!h)
    return;
  TEST_CHECK(xw_web_http_port(h) > 0);
  a = realpath("tests/data/webunity/app.wasm", NULL);
  b = realpath("tests/data/red.png", NULL);
  TEST_CHECK(a != NULL && b != NULL);
  if (a && b) {
    TEST_CHECK_EQ_INT(xw_web_http_alias(h, a), 0);
    TEST_CHECK_EQ_INT(xw_web_http_alias(h, a), 0); /* stable */
    TEST_CHECK_EQ_INT(xw_web_http_alias(h, b), 1);
    TEST_CHECK_EQ_INT(xw_web_http_alias(h, "/no/such/file.png"), -1);
    TEST_CHECK_EQ_INT(xw_web_http_alias(h, "relative/x.png"), -1);
    TEST_CHECK_EQ_INT(xw_web_http_alias(h, NULL), -1);
    TEST_CHECK_EQ_INT(xw_web_http_alias(NULL, a), -1);
  }
  free(a);
  free(b);
  xw_web_http_stop(h);
}

static void test_preferred_port(void) {
  XWWebHttp *a = NULL, *b = NULL, *c = NULL;
  XWError err;
  int p1 = -1, p2 = -1, p3 = -1;
  TEST_BEGIN("preferred port rebinds, taken port falls back");
  memset(&err, 0, sizeof(err));
  a = xw_web_http_start("tests/data/webunity", 0, &err);
  TEST_CHECK(a != NULL);
  if (!a)
    return;
  p1 = xw_web_http_port(a);
  TEST_CHECK(p1 > 0);
  /* preferred taken: second server on the same root falls back. */
  b = xw_web_http_start("tests/data/webunity", p1, &err);
  TEST_CHECK(b != NULL);
  if (b)
    p2 = xw_web_http_port(b);
  TEST_CHECK(p2 > 0 && p2 != p1);
  xw_web_http_stop(a);
  xw_web_http_stop(b);
  /* freed preferred port rebinds (SO_REUSEADDR). */
  c = xw_web_http_start("tests/data/webunity", p1, &err);
  TEST_CHECK(c != NULL);
  if (c)
    p3 = xw_web_http_port(c);
  TEST_CHECK_EQ_INT(p3, p1);
  xw_web_http_stop(c);
  /* bad root still fails, garbage preferred is ignored. */
  TEST_CHECK(xw_web_http_start("/no/such/dir", 0, &err) == NULL);
  a = xw_web_http_start("tests/data/webunity", -5, &err);
  TEST_CHECK(a != NULL && xw_web_http_port(a) > 0);
  xw_web_http_stop(a);
}

int main(void) {
  TEST_SUITE("web local http (no X)");
  test_mime();
  test_resolve();
  test_range();
  test_alias();
  test_preferred_port();
  test_dir_mount();
  test_keepalive();
  test_keepalive_range();
  TEST_SUMMARY();
}

static int test_tcp_connect(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a;
  if (fd < 0)
    return -1;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int test_recv_all(int fd, char *out, size_t cap) {
  size_t total = 0;
  for (;;) {
    fd_set rfds;
    struct timeval tv = {5, 0};
    ssize_t nr;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
      return -1;
    nr = recv(fd, out + total, cap - 1 - total, 0);
    if (nr <= 0)
      return total > 0 ? (int)total : -1;
    total += (size_t)nr;
    out[total] = '\0';
    if (strstr(out, "\r\n\r\n"))
      break;
    if (total >= cap - 1)
      break;
  }
  return (int)total;
}

static int test_read_response(int fd, char *hdr, size_t hcap, char **body, size_t *body_len) {
  char buf[131072];
  int n, hdr_len = 0;
  char *eoh;
  long long clen = 0;
  size_t have = 0;
  static char store[1048576];
  n = test_recv_all(fd, buf, sizeof(buf));
  if (n <= 0)
    return -1;
  eoh = strstr(buf, "\r\n\r\n");
  if (!eoh)
    return -1;
  hdr_len = (int)(eoh - buf) + 4;
  if ((size_t)hdr_len >= hcap)
    return -1;
  memcpy(hdr, buf, (size_t)hdr_len);
  hdr[hdr_len] = '\0';
  {
    char *p = strstr(hdr, "Content-Length:");
    if (p)
      clen = atoll(p + 15);
  }
  have = (size_t)n - (size_t)hdr_len;
  if (clen < 0 || (size_t)clen > sizeof(store))
    return -1;
  if (have > (size_t)clen)
    have = (size_t)clen;
  memcpy(store, buf + hdr_len, have);
  while (have < (size_t)clen) {
    fd_set rfds;
    struct timeval tv = {5, 0};
    ssize_t nr;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
      return -1;
    nr = recv(fd, store + have, (size_t)clen - have, 0);
    if (nr <= 0)
      return -1;
    have += (size_t)nr;
  }
  *body = store;
  *body_len = have;
  return 0;
}

static void test_keepalive(void) {
  XWWebHttp *h = NULL;
  XWError err;
  int port = -1, fd = -1;
  char hdr[8192], *body = NULL;
  size_t blen = 0;
  const char *pipelined =
      "GET /app.wasm HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n"
      "GET /app.wasm HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  TEST_BEGIN("pipelined keep-alive serves both requests on one connection");
  memset(&err, 0, sizeof(err));
  h = xw_web_http_start("tests/data/webunity", 0, &err);
  TEST_CHECK(h != NULL);
  if (!h)
    return;
  port = xw_web_http_port(h);
  TEST_CHECK(port > 0);
  fd = test_tcp_connect(port);
  TEST_CHECK(fd >= 0);
  if (fd < 0) {
    xw_web_http_stop(h);
    return;
  }
  TEST_CHECK(send(fd, pipelined, strlen(pipelined), 0) == (ssize_t)strlen(pipelined));
  TEST_CHECK(test_read_response(fd, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "200") != NULL);
  TEST_CHECK(strstr(hdr, "keep-alive") != NULL);
  TEST_CHECK(blen > 0);
  TEST_CHECK(test_read_response(fd, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "200") != NULL);
  TEST_CHECK(blen > 0);
  close(fd);
  xw_web_http_stop(h);
}

static void test_keepalive_range(void) {
  XWWebHttp *h = NULL;
  XWError err;
  int port = -1, fd = -1;
  char hdr[8192], *body = NULL;
  size_t blen = 0;
  const char *req =
      "GET /app.wasm HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-99\r\nConnection: keep-alive\r\n\r\n"
      "GET /app.wasm HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: bytes=0-99\r\nConnection: close\r\n\r\n";
  TEST_BEGIN("pipelined ranges both return 206 without dropping the second");
  memset(&err, 0, sizeof(err));
  h = xw_web_http_start("tests/data/webunity", 0, &err);
  TEST_CHECK(h != NULL);
  if (!h)
    return;
  port = xw_web_http_port(h);
  fd = test_tcp_connect(port);
  TEST_CHECK(fd >= 0);
  if (fd < 0) {
    xw_web_http_stop(h);
    return;
  }
  TEST_CHECK(send(fd, req, strlen(req), 0) == (ssize_t)strlen(req));
  TEST_CHECK(test_read_response(fd, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "206") != NULL);
  TEST_CHECK(strstr(hdr, "Content-Range:") != NULL);
  TEST_CHECK(blen > 0);
  TEST_CHECK(test_read_response(fd, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "206") != NULL);
  TEST_CHECK(blen > 0);
  close(fd);
  xw_web_http_stop(h);
}

/* One-shot request: returns 0 when a response was read; body points into the
 * shared store inside test_read_response. Use only for GET (HEAD has no
 * body, so read it with test_recv_all instead). */
static int http_get(int port, const char *path, const char *range,
                    char *hdr, size_t hcap, char **body, size_t *blen) {
  int fd = test_tcp_connect(port);
  char req[1024];
  if (fd < 0)
    return -1;
  if (range)
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nRange: %s\r\n"
             "Connection: close\r\n\r\n", path, range);
  else
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
             "Connection: close\r\n\r\n", path);
  if (send(fd, req, strlen(req), 0) != (ssize_t)strlen(req)) {
    close(fd);
    return -1;
  }
  {
    int rc = test_read_response(fd, hdr, hcap, body, blen);
    close(fd);
    return rc;
  }
}

/* Directory mounts back WE "directory" user properties: a read-only route
 * outside the wallpaper root, with realpath containment. */
static void test_dir_mount(void) {
  XWWebHttp *h = NULL;
  XWError err;
  char tmpl[] = "/tmp/opencode/xwdir-XXXXXX";
  char root[512], sub[600], file[700], leak[700], mk[700];
  char secret[128], path[900], hdr[8192], *body = NULL;
  size_t blen = 0;
  FILE *f;
  int id, port, fd, i, n;
  TEST_BEGIN("directory mounts serve only files inside the root");

  memset(&err, 0, sizeof(err));
  h = xw_web_http_start("tests/data/webunity", 0, &err);
  TEST_CHECK(h != NULL);
  if (!h)
    return;
  port = xw_web_http_port(h);
  TEST_CHECK(port > 0);

  TEST_CHECK(mkdtemp(tmpl) != NULL);
  snprintf(root, sizeof(root), "%s", tmpl);
  snprintf(sub, sizeof(sub), "%s/sub", root);
  snprintf(file, sizeof(file), "%s/a.txt", sub);
  snprintf(leak, sizeof(leak), "%s/leak", root);
  TEST_CHECK(mkdir(sub, 0755) == 0);
  f = fopen(file, "w");
  TEST_CHECK(f != NULL);
  if (f) {
    fputs("hello", f);
    fclose(f);
  }
  TEST_CHECK(symlink("/etc/passwd", leak) == 0);
  snprintf(secret, sizeof(secret), "/tmp/opencode/xwdir-secret-%d.txt",
           (int)getpid());
  f = fopen(secret, "w");
  if (f) {
    fputs("nope", f);
    fclose(f);
  }

  id = xw_web_http_mount_dir(h, root);
  TEST_CHECK(id >= 0);
  TEST_CHECK_EQ_INT(xw_web_http_mount_dir(h, root), id);
  TEST_CHECK_EQ_INT(xw_web_http_mount_dir(h, file), -1);
  TEST_CHECK_EQ_INT(xw_web_http_mount_dir(h, "/no/such/dir"), -1);
  TEST_CHECK_EQ_INT(xw_web_http_mount_dir(h, NULL), -1);
  TEST_CHECK_EQ_INT(xw_web_http_mount_dir(NULL, root), -1);

  snprintf(path, sizeof(path), "/_xw/dir/%d/sub/a.txt", id);
  TEST_CHECK(http_get(port, path, NULL, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "200") != NULL);
  TEST_CHECK(strstr(hdr, "text/plain") != NULL);
  TEST_CHECK(blen == 5 && memcmp(body, "hello", 5) == 0);

  /* range still works through the directory route */
  TEST_CHECK(http_get(port, path, "bytes=1-3", hdr, sizeof(hdr), &body,
                      &blen) == 0);
  TEST_CHECK(strstr(hdr, "206") != NULL);
  TEST_CHECK(blen == 3 && memcmp(body, "ell", 3) == 0);

  /* traversal and symlink escape are refused, not served */
  snprintf(path, sizeof(path), "/_xw/dir/%d/../%s", id,
           strrchr(secret, '/') + 1);
  TEST_CHECK(http_get(port, path, NULL, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "403") != NULL);
  snprintf(path, sizeof(path), "/_xw/dir/%d/leak", id);
  TEST_CHECK(http_get(port, path, NULL, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "403") != NULL);
  snprintf(path, sizeof(path), "/_xw/dir/99/sub/a.txt");
  TEST_CHECK(http_get(port, path, NULL, hdr, sizeof(hdr), &body, &blen) == 0);
  TEST_CHECK(strstr(hdr, "404") != NULL);

  /* HEAD returns headers only */
  snprintf(path, sizeof(path), "/_xw/dir/%d/sub/a.txt", id);
  fd = test_tcp_connect(port);
  TEST_CHECK(fd >= 0);
  if (fd >= 0) {
    char req[1024], buf[4096];
    snprintf(req, sizeof(req),
             "HEAD %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
             path);
    TEST_CHECK(send(fd, req, strlen(req), 0) == (ssize_t)strlen(req));
    n = test_recv_all(fd, buf, sizeof(buf));
    TEST_CHECK(n > 0 && strstr(buf, "200") != NULL);
    {
      char *eoh = n > 0 ? strstr(buf, "\r\n\r\n") : NULL;
      TEST_CHECK(eoh != NULL && (size_t)(eoh - buf) + 4 == (size_t)n);
    }
    close(fd);
  }

  /* mount cap: stable ids for distinct roots, then a hard failure */
  for (i = 1; i < XW_HTTP_DIR_MAX; i++) {
    snprintf(mk, sizeof(mk), "%s/m%d", root, i);
    TEST_CHECK(mkdir(mk, 0755) == 0);
    TEST_CHECK(xw_web_http_mount_dir(h, mk) >= 0);
  }
  snprintf(mk, sizeof(mk), "%s/over", root);
  TEST_CHECK(mkdir(mk, 0755) == 0);
  TEST_CHECK_EQ_INT(xw_web_http_mount_dir(h, mk), -1);

  xw_web_http_stop(h);
  unlink(leak);
  unlink(file);
  rmdir(sub);
  unlink(secret);
  for (i = 1; i < XW_HTTP_DIR_MAX; i++) {
    snprintf(mk, sizeof(mk), "%s/m%d", root, i);
    rmdir(mk);
  }
  snprintf(mk, sizeof(mk), "%s/over", root);
  rmdir(mk);
  rmdir(root);
}
