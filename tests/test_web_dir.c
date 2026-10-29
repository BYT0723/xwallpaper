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
  TEST_CHECK_EQ_INT(xw_web_is_http_url("file:///x/index.html"), 0);
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
  TEST_CHECK(root[0] == '/'); /* resolved to an absolute path */
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

static void write_file(const char *dir, const char *name, const char *content) {
  char p[1024];
  snprintf(p, sizeof(p), "%s/%s", dir, name);
  FILE *f = fopen(p, "w");
  if (f) {
    fputs(content, f);
    fclose(f);
  }
}

static void test_resolve_project_file(void) {
  TEST_BEGIN("resolve honors project.json file entry (wallpaper.html)");
  char *d = make_dir(0);
  TEST_CHECK(d != NULL);
  if (!d)
    return;
  write_file(d, "project.json",
             "{\"title\":\"t\",\"preview\":\"preview.jpg\","
             "\"file\":\"wallpaper.html\",\"type\":\"web\"}");
  write_file(d, "wallpaper.html", "<html></html>");
  char root[1024], entry[256];
  TEST_CHECK_EQ_INT(xw_web_resolve(d, root, sizeof(root), entry, sizeof(entry)), 0);
  TEST_CHECK_EQ_STR(entry, "wallpaper.html");
  char p[1100];
  snprintf(p, sizeof(p), "%s/wallpaper.html", d);
  unlink(p);
  snprintf(p, sizeof(p), "%s/project.json", d);
  unlink(p);
  rmdir(d);
  free(d);
}

static void test_resolve_project_file_missing_fallback(void) {
  TEST_BEGIN("resolve falls back to index.html when project file entry is absent");
  char *d = make_dir(1);
  TEST_CHECK(d != NULL);
  if (!d)
    return;
  write_file(d, "project.json",
             "{\"title\":\"t\",\"file\":\"wallpaper.html\",\"type\":\"web\"}");
  char root[1024], entry[256];
  TEST_CHECK_EQ_INT(xw_web_resolve(d, root, sizeof(root), entry, sizeof(entry)), 0);
  TEST_CHECK_EQ_STR(entry, "index.html");
  char p[1100];
  snprintf(p, sizeof(p), "%s/index.html", d);
  unlink(p);
  snprintf(p, sizeof(p), "%s/project.json", d);
  unlink(p);
  rmdir(d);
  free(d);
}

static void test_resolve_project_file_unsafe_ignored(void) {
  TEST_BEGIN("resolve ignores unsafe project.json file values");
  const char *bad[] = {"../evil.html", "/abs.html", "sub/dir.html", "..", ""};
  size_t i;
  for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    char *d = make_dir(1);
    TEST_CHECK(d != NULL);
    if (!d)
      return;
    /* sub/dir.html must really exist: without the basename guard the
     * resolver would adopt it (stat passes), so this pins the validator */
    if (strcmp(bad[i], "sub/dir.html") == 0) {
      char sub[1024];
      snprintf(sub, sizeof(sub), "%s/sub", d);
      mkdir(sub, 0755);
      write_file(d, "sub/dir.html", "<html></html>");
    }
    char pj[2048];
    snprintf(pj, sizeof(pj), "{\"title\":\"t\",\"file\":\"%s\",\"type\":\"web\"}", bad[i]);
    write_file(d, "project.json", pj);
    char root[1024], entry[256];
    TEST_CHECK_EQ_INT(xw_web_resolve(d, root, sizeof(root), entry, sizeof(entry)), 0);
    TEST_CHECK_EQ_STR(entry, "index.html");
    char p[1100];
    snprintf(p, sizeof(p), "%s/index.html", d);
    unlink(p);
    snprintf(p, sizeof(p), "%s/project.json", d);
    unlink(p);
    snprintf(p, sizeof(p), "%s/sub/dir.html", d);
    unlink(p);
    snprintf(p, sizeof(p), "%s/sub", d);
    rmdir(p);
    rmdir(d);
    free(d);
  }
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

int main(void) {
  TEST_SUITE("web_dir");
  test_is_http_url();
  test_resolve_url();
  test_resolve_dir_ok();
  test_resolve_dir_no_index();
  test_resolve_project_file();
  test_resolve_project_file_missing_fallback();
  test_resolve_project_file_unsafe_ignored();
  test_resolve_file_rejected();
  TEST_SUMMARY();
}
