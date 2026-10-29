#include "harness.h"
#include "../src/web_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  TEST_SUMMARY();
}
