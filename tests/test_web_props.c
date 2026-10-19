#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "../src/web_props.h"

static const char kProps[] =
    "{"
    "  \"general\": {"
    "    \"properties\": {"
    "      \"bgColor\": {\"type\": \"color\", \"value\": \"1.000000 1.000000 1.000000\"},"
    "      \"roleScale\": {\"type\": \"slider\", \"value\": 100},"
    "      \"bgImage\": {\"type\": \"file\", \"value\": \"\"},"
    "      \"useFx\": {\"type\": \"bool\", \"value\": true},"
    "      \"combo\": {\"type\": \"combo\", \"value\": \"a'b\\\\c\"}"
    "    }"
    "  }"
    "}";

static void test_parse_ok(void) {
  TEST_BEGIN("parse extracts name/type/value");
  XWWebProp out[8];
  TEST_CHECK_EQ_INT(xw_web_props_parse(kProps, strlen(kProps), out, 8), 5);
  TEST_CHECK_EQ_STR(out[0].name, "bgColor");
  TEST_CHECK_EQ_STR(out[0].type, "color");
  TEST_CHECK_EQ_STR(out[0].value, "1.000000 1.000000 1.000000");
  TEST_CHECK_EQ_STR(out[1].name, "roleScale");
  TEST_CHECK_EQ_STR(out[1].type, "slider");
  TEST_CHECK_EQ_STR(out[1].value, "100");
  TEST_CHECK_EQ_STR(out[2].name, "bgImage");
  TEST_CHECK_EQ_STR(out[2].value, "");
  TEST_CHECK_EQ_STR(out[3].name, "useFx");
  TEST_CHECK_EQ_STR(out[3].value, "true");
}

static void test_parse_bad(void) {
  TEST_BEGIN("parse rejects malformed input");
  XWWebProp out[8];
  TEST_CHECK_EQ_INT(xw_web_props_parse("not json", 8, out, 8), -1);
  TEST_CHECK_EQ_INT(xw_web_props_parse("{}", 2, out, 8), -1);
  TEST_CHECK_EQ_INT(
      xw_web_props_parse("{\"general\":{}}", 13, out, 8), -1);
  TEST_CHECK_EQ_INT(xw_web_props_parse(NULL, 0, out, 8), -1);
  TEST_CHECK_EQ_INT(xw_web_props_parse(kProps, strlen(kProps), NULL, 8), -1);
  TEST_CHECK_EQ_INT(xw_web_props_parse(kProps, strlen(kProps), out, 0), -1);
  /* over capacity */
  XWWebProp one[1];
  TEST_CHECK_EQ_INT(xw_web_props_parse(kProps, strlen(kProps), one, 1), -1);
}

static void test_apply_js(void) {
  TEST_BEGIN("apply fragment escapes and wraps values");
  XWWebProp ps[2];
  snprintf(ps[0].name, sizeof(ps[0].name), "bgColor");
  snprintf(ps[0].type, sizeof(ps[0].type), "color");
  snprintf(ps[0].value, sizeof(ps[0].value), "0 1 0");
  snprintf(ps[1].name, sizeof(ps[1].name), "combo");
  snprintf(ps[1].type, sizeof(ps[1].type), "combo");
  snprintf(ps[1].value, sizeof(ps[1].value), "a'b\\c\nd");
  char js[2048];
  TEST_CHECK_EQ_INT(xw_web_props_apply_js(ps, 2, js, sizeof(js)), 0);
  TEST_CHECK(strstr(js, "applyUserProperties") != NULL);
  TEST_CHECK(strstr(js, "\"bgColor\":{\"value\":\"0 1 0\"}") != NULL);
  TEST_CHECK(strstr(js, "\"combo\":{\"value\":\"a'b\\\\c\\nd\"}") != NULL);
  TEST_CHECK_EQ_INT(xw_web_props_apply_js(NULL, 0, js, sizeof(js)), -1);
  TEST_CHECK_EQ_INT(xw_web_props_apply_js(ps, 0, js, sizeof(js)), -1);
}

int main(void) {
  TEST_SUITE("web_props");
  test_parse_ok();
  test_parse_bad();
  test_apply_js();
  TEST_SUMMARY();
}
