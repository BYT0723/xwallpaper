#include "harness.h"
#include "../src/cli.h"

static void test_geom_basic(void) {
  TEST_BEGIN("parses WxH+X+Y");
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  TEST_CHECK_EQ_INT(xw_geom_parse("1920x1080+100+200", &x, &y, &w, &h), 1);
  TEST_CHECK_EQ_INT(w, 1920);
  TEST_CHECK_EQ_INT(h, 1080);
  TEST_CHECK_EQ_INT(x, 100);
  TEST_CHECK_EQ_INT(y, 200);
}

static void test_geom_no_offset(void) {
  TEST_BEGIN("parses WxH without offset");
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  TEST_CHECK_EQ_INT(xw_geom_parse("800x600", &x, &y, &w, &h), 1);
  TEST_CHECK_EQ_INT(w, 800);
  TEST_CHECK_EQ_INT(h, 600);
  TEST_CHECK_EQ_INT(x, 0);
  TEST_CHECK_EQ_INT(y, 0);
}

static void test_geom_negative_offset(void) {
  TEST_BEGIN("parses negative offsets WxH-X-Y");
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  TEST_CHECK_EQ_INT(xw_geom_parse("640x480-50-30", &x, &y, &w, &h), 1);
  TEST_CHECK_EQ_INT(w, 640);
  TEST_CHECK_EQ_INT(h, 480);
  TEST_CHECK_EQ_INT(x, -50);
  TEST_CHECK_EQ_INT(y, -30);
}

static void test_geom_malformed(void) {
  TEST_BEGIN("malformed geometry rejected");
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  TEST_CHECK_EQ_INT(xw_geom_parse("abc", &x, &y, &w, &h), 0);
  TEST_CHECK_EQ_INT(xw_geom_parse("1920", &x, &y, &w, &h), 0);
  TEST_CHECK_EQ_INT(xw_geom_parse("1920x", &x, &y, &w, &h), 0);
  TEST_CHECK_EQ_INT(xw_geom_parse("x1080", &x, &y, &w, &h), 0);
  TEST_CHECK_EQ_INT(xw_geom_parse("", &x, &y, &w, &h), 0);
}

int main(void) {
  TEST_SUITE("geom");
  test_geom_basic();
  test_geom_no_offset();
  test_geom_negative_offset();
  test_geom_malformed();
  TEST_SUMMARY();
}
