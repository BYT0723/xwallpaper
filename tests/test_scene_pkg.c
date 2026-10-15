#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/scene_pkg.h"

static void test_open_count_find(void) {
  TEST_BEGIN("open mini.pkg, count/find/names");
  XWPkg *p = NULL;
  TEST_CHECK_EQ_INT(xw_pkg_open("tests/data/scene/mini.pkg", &p), 0);
  TEST_CHECK(p != NULL);
  if (!p) return;
  TEST_CHECK_EQ_INT(xw_pkg_count(p), 3);
  TEST_CHECK_EQ_INT(xw_pkg_find(p, "hello.txt"), 0);
  TEST_CHECK_EQ_INT(xw_pkg_find(p, "data.bin"), 1);
  TEST_CHECK_EQ_INT(xw_pkg_find(p, "sub/dir.bin"), 2);
  TEST_CHECK_EQ_INT(xw_pkg_find(p, "missing"), -1);
  char buf[64];
  TEST_CHECK_EQ_INT(xw_pkg_name(p, 0, buf, sizeof(buf)), 0);
  TEST_CHECK_EQ_STR(buf, "hello.txt");
  TEST_CHECK_EQ_INT(xw_pkg_name(p, 1, buf, sizeof(buf)), 0);
  TEST_CHECK_EQ_STR(buf, "data.bin");
  xw_pkg_close(p);
}

static void test_read_bytes(void) {
  TEST_BEGIN("read returns exact bytes");
  XWPkg *p = NULL;
  TEST_CHECK_EQ_INT(xw_pkg_open("tests/data/scene/mini.pkg", &p), 0);
  if (!p) return;
  unsigned char *out = NULL;
  size_t len = 0;
  TEST_CHECK_EQ_INT(xw_pkg_read(p, 0, &out, &len), 0);
  TEST_CHECK_EQ_INT((int)len, 11);
  if (out) {
    TEST_CHECK(memcmp(out, "hello scene", 11) == 0);
    free(out);
  }
  out = NULL;
  len = 0;
  TEST_CHECK_EQ_INT(xw_pkg_read(p, 1, &out, &len), 0);
  TEST_CHECK_EQ_INT((int)len, 16);
  if (out) {
    int ok = 1;
    for (int i = 0; i < 16; i++)
      if (out[i] != (unsigned char)i) ok = 0;
    TEST_CHECK(ok);
    free(out);
  }
  xw_pkg_close(p);
}

static void test_bad_inputs(void) {
  TEST_BEGIN("bad magic/truncated/oob rejected, no crash");
  XWPkg *p = NULL;
  TEST_CHECK_EQ_INT(xw_pkg_open("tests/data/scene/mini.tex", &p), -1);
  TEST_CHECK_EQ_INT(xw_pkg_open("/nonexistent/x.pkg", &p), -1);
  TEST_CHECK_EQ_INT(xw_pkg_open(NULL, &p), -1);
  TEST_CHECK_EQ_INT(xw_pkg_open("tests/data/scene/mini.pkg", NULL), -1);
  XWPkg *q = NULL;
  if (xw_pkg_open("tests/data/scene/mini.pkg", &q) == 0 && q) {
    TEST_CHECK_EQ_INT(xw_pkg_read(q, 99, NULL, NULL), -1);
    char buf[64];
    TEST_CHECK_EQ_INT(xw_pkg_name(q, 99, buf, sizeof(buf)), -1);
    TEST_CHECK_EQ_INT(xw_pkg_name(q, 0, NULL, 0), -1);
    xw_pkg_close(q);
  } else {
    TEST_FAIL("fixture should open");
  }
  xw_pkg_close(NULL);
}

static void test_extract_all(void) {
  TEST_BEGIN("extract-all round-trips entries incl. subdirs");
  XWPkg *p = NULL;
  TEST_CHECK_EQ_INT(xw_pkg_open("tests/data/scene/mini.pkg", &p), 0);
  if (!p)
    return;
  char *t = strdup("/tmp/xw-pkg-XXXXXX");
  TEST_CHECK(t != NULL && mkdtemp(t) != NULL);
  if (!t) {
    xw_pkg_close(p);
    return;
  }
  /* nested name exercises mkdir -p */
  TEST_CHECK_EQ_INT(xw_pkg_extract_all(p, t), 0);
  TEST_CHECK_EQ_INT(xw_pkg_extract_all(NULL, t), -1);
  TEST_CHECK_EQ_INT(xw_pkg_extract_all(p, NULL), -1);
  char hp[1024], dp[1024], sp[1024];
  snprintf(hp, sizeof(hp), "%s/hello.txt", t);
  snprintf(dp, sizeof(dp), "%s/data.bin", t);
  snprintf(sp, sizeof(sp), "%s/sub/dir.bin", t);
  FILE *f = fopen(hp, "rb");
  TEST_CHECK(f != NULL);
  if (f) {
    char buf[32] = {0};
    TEST_CHECK(fread(buf, 1, 11, f) == 11);
    TEST_CHECK(memcmp(buf, "hello scene", 11) == 0);
    fclose(f);
  }
  f = fopen(dp, "rb");
  TEST_CHECK(f != NULL);
  if (f) {
    unsigned char buf[16];
    TEST_CHECK(fread(buf, 1, 16, f) == 16);
    int ok = 1;
    for (int i = 0; i < 16; i++)
      if (buf[i] != (unsigned char)i)
        ok = 0;
    TEST_CHECK(ok);
    fclose(f);
  }
  f = fopen(sp, "rb");
  TEST_CHECK(f != NULL);
  if (f) {
    unsigned char buf[4];
    TEST_CHECK(fread(buf, 1, 4, f) == 4);
    TEST_CHECK(buf[0] == 0xde && buf[3] == 0xef);
    fclose(f);
  }
  unlink(hp);
  unlink(dp);
  unlink(sp);
  rmdir(t);
  char sub[1024];
  snprintf(sub, sizeof(sub), "%s/sub", t);
  rmdir(sub);
  free(t);
  xw_pkg_close(p);
}

int main(void) {
  TEST_SUITE("scene_pkg");
  test_open_count_find();
  test_read_bytes();
  test_bad_inputs();
  test_extract_all();
  TEST_SUMMARY();
}
