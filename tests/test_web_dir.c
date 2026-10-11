#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "harness.h"
#include "../src/web_dir.h"

static void test_is_http_url(void) {
  TEST_BEGIN("http/https urls detected");
  TEST_CHECK_EQ_INT(xw_web_is_http_url("http://localhost:8000/"), 1);
  TEST_CHECK_EQ_INT(xw_web_is_http_url("https://example.com/page"), 1);
  TEST_CHECK_EQ_INT(xw_web_is_http_url("/home/u/wall"), 0);
  TEST_CHECK_EQ_INT(xw_web_is_http_url("xw://local/index.html"), 0);
  TEST_CHECK_EQ_INT(xw_web_is_http_url(""), 0);
  TEST_CHECK_EQ_INT(xw_web_is_http_url(NULL), 0);
}

static void test_resolve_url(void) {
  TEST_BEGIN("resolve passes http urls through");
  char root[1024], entry[256];
  TEST_CHECK_EQ_INT(xw_web_resolve("https://example.com/", root, sizeof(root), entry, sizeof(entry)), 1);
}

static char *make_dir(int with_index) {
  char *t = strdup("/tmp/xw-webdir-XXXXXX");
  if (!mkdtemp(t)) {
    free(t);
    return NULL;
  }
  if (with_index) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/index.html", t);
    FILE *f = fopen(p, "w");
    if (f) {
      fputs("<html></html>", f);
      fclose(f);
    }
  }
  return t;
}

static void test_resolve_dir_ok(void) {
  TEST_BEGIN("resolve accepts dir with index.html");
  char *d = make_dir(1);
  TEST_CHECK(d != NULL);
  if (!d)
    return;
  char root[1024], entry[256];
  TEST_CHECK_EQ_INT(xw_web_resolve(d, root, sizeof(root), entry, sizeof(entry)), 0);
  TEST_CHECK_EQ_STR(entry, "index.html");
  TEST_CHECK(strncmp(root, "/", 1) == 0);
  char idx[1100];
  snprintf(idx, sizeof(idx), "%s/index.html", d);
  unlink(idx);
  rmdir(d);
  free(d);
}

static void test_resolve_dir_no_index(void) {
  TEST_BEGIN("resolve rejects dir without index.html");
  char *d = make_dir(0);
  TEST_CHECK(d != NULL);
  if (!d)
    return;
  char root[1024], entry[256];
  TEST_CHECK_EQ_INT(xw_web_resolve(d, root, sizeof(root), entry, sizeof(entry)), -1);
  rmdir(d);
  free(d);
}

static void test_resolve_file_rejected(void) {
  TEST_BEGIN("resolve rejects plain files (no single-file mode)");
  char t[] = "/tmp/xw-webfile-XXXXXX";
  int fd = mkstemp(t);
  TEST_CHECK(fd >= 0);
  if (fd >= 0)
    close(fd);
  char root[1024], entry[256];
  TEST_CHECK_EQ_INT(xw_web_resolve(t, root, sizeof(root), entry, sizeof(entry)), -1);
  TEST_CHECK_EQ_INT(xw_web_resolve("tests/data/page.html", root, sizeof(root), entry, sizeof(entry)), -1);
  unlink(t);
  TEST_CHECK_EQ_INT(xw_web_resolve("/nonexistent-xyz", root, sizeof(root), entry, sizeof(entry)), -1);
  TEST_CHECK_EQ_INT(xw_web_resolve("", root, sizeof(root), entry, sizeof(entry)), -1);
  TEST_CHECK_EQ_INT(xw_web_resolve(NULL, root, sizeof(root), entry, sizeof(entry)), -1);
}

static void test_mime(void) {
  TEST_BEGIN("mime table covers wallpaper assets");
  TEST_CHECK_EQ_STR(xw_web_mime_for("index.html"), "text/html");
  TEST_CHECK_EQ_STR(xw_web_mime_for("a.CSS"), "text/css");
  TEST_CHECK_EQ_STR(xw_web_mime_for("app.js"), "text/javascript");
  TEST_CHECK_EQ_STR(xw_web_mime_for("m.mjs"), "text/javascript");
  TEST_CHECK_EQ_STR(xw_web_mime_for("data.json"), "application/json");
  TEST_CHECK_EQ_STR(xw_web_mime_for("p.png"), "image/png");
  TEST_CHECK_EQ_STR(xw_web_mime_for("p.svg"), "image/svg+xml");
  TEST_CHECK_EQ_STR(xw_web_mime_for("f.woff2"), "font/woff2");
  TEST_CHECK_EQ_STR(xw_web_mime_for("m.wasm"), "application/wasm");
  TEST_CHECK_EQ_STR(xw_web_mime_for("noext"), "application/octet-stream");
  TEST_CHECK_EQ_STR(xw_web_mime_for(NULL), "application/octet-stream");
}

static void test_req_to_rel(void) {
  TEST_BEGIN("request path maps to relative file");
  char rel[1024];
  TEST_CHECK_EQ_INT(xw_web_req_to_rel("/index.html", rel, sizeof(rel)), 0);
  TEST_CHECK_EQ_STR(rel, "index.html");
  TEST_CHECK_EQ_INT(xw_web_req_to_rel("/", rel, sizeof(rel)), 0);
  TEST_CHECK_EQ_STR(rel, "index.html");
  TEST_CHECK_EQ_INT(xw_web_req_to_rel("/sub/app.js", rel, sizeof(rel)), 0);
  TEST_CHECK_EQ_STR(rel, "sub/app.js");
  TEST_CHECK_EQ_INT(xw_web_req_to_rel("/%73tyle.css", rel, sizeof(rel)), 0);
  TEST_CHECK_EQ_STR(rel, "style.css");
  TEST_CHECK_EQ_INT(xw_web_req_to_rel("/a/../b", rel, sizeof(rel)), -1);
  TEST_CHECK_EQ_INT(xw_web_req_to_rel("/..", rel, sizeof(rel)), -1);
  TEST_CHECK_EQ_INT(xw_web_req_to_rel(NULL, rel, sizeof(rel)), -1);
}

int main(void) {
  TEST_SUITE("web_dir");
  test_is_http_url();
  test_resolve_url();
  test_resolve_dir_ok();
  test_resolve_dir_no_index();
  test_resolve_file_rejected();
  test_mime();
  test_req_to_rel();
  TEST_SUMMARY();
}
