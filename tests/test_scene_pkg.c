#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../src/scene_pkg.h"

static void test_open_count_find(void) {
  TEST_BEGIN("open mini.pkg, count/find/names");
  XWPkg *p = NULL;
  TEST_CHECK_EQ_INT(xw_pkg_open("tests/data/scene/mini.pkg", &p), 0);
  TEST_CHECK(p != NULL);
  if (!p) return;
  TEST_CHECK_EQ_INT(xw_pkg_count(p), 2);
  TEST_CHECK_EQ_INT(xw_pkg_find(p, "hello.txt"), 0);
  TEST_CHECK_EQ_INT(xw_pkg_find(p, "data.bin"), 1);
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

int main(void) {
  TEST_SUITE("scene_pkg");
  test_open_count_find();
  test_read_bytes();
  test_bad_inputs();
  TEST_SUMMARY();
}
