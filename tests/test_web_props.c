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

static const char kWithGroups[] =
    "{"
    "  \"general\": {"
    "    \"properties\": {"
    "      \"hdr\": {\"text\": \"ui_hdr\", \"order\": 1},"
    "      \"a\": {\"type\": \"bool\", \"value\": true},"
    "      \"hdr2\": {\"text\": \"ui_hdr2\", \"order\": 2},"
    "      \"b\": {\"type\": \"slider\", \"value\": 5}"
    "    }"
    "  }"
    "}";

static const char kLocalized[] =
    "{"
    "  \"general\": {"
    "    \"properties\": {"
    "      \"zoom\": {\"type\": \"slider\", \"value\": 100, \"text\": \"ui_zoom\"},"
    "      \"bg\": {\"type\": \"color\", \"value\": \"0 0 0\", \"text\": \"ui_bg\"},"
    "      \"note\": {\"type\": \"text\", \"value\": \"hi\", \"text\": \"ui_note\"},"
    "      \"hdr\": {\"text\": \"ui_hdr\", \"order\": 1}"
    "    },"
    "    \"localization\": {"
    "      \"en-us\": {\"ui_zoom\": \"Zoom, %\", \"ui_bg\": \"Background\", \"ui_note\": \"Note\"},"
    "      \"zh-chs\": {\"ui_zoom\": \"缩放, %\", \"ui_bg\": \"背景\", \"ui_note\": \"备注\"},"
    "      \"ru-ru\": {\"ui_zoom\": \"Масштаб, %\", \"ui_bg\": \"Фон\", \"ui_note\": \"Заметка\"}"
    "    }"
    "  }"
    "}";

static char *make_many_props(int count) {
  size_t cap = (size_t)count * 64 + 128;
  char *json = malloc(cap);
  size_t off;
  int i;
  if (!json)
    return NULL;
  off = (size_t)snprintf(json, cap, "{\"general\":{\"properties\":{");
  for (i = 0; i < count; i++) {
    int w = snprintf(json + off, cap - off,
                     "%s\"p%d\":{\"type\":\"slider\",\"value\":%d}",
                     i ? "," : "", i, i);
    if (w < 0 || (size_t)w >= cap - off) {
      free(json);
      return NULL;
    }
    off += (size_t)w;
  }
  if (snprintf(json + off, cap - off, "}}}") >= (int)(cap - off)) {
    free(json);
    return NULL;
  }
  return json;
}

static void test_parse_ok(void) {
  TEST_BEGIN("parse extracts name/type/value");
  XWWebProp *out = NULL;
  TEST_CHECK_EQ_INT(xw_web_props_parse(kProps, strlen(kProps), "en-us", &out), 5);
  TEST_CHECK(out != NULL);
  if (out) {
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
  free(out);
}

static void test_parse_bad(void) {
  TEST_BEGIN("parse rejects malformed input");
  XWWebProp *out = NULL;
  TEST_CHECK_EQ_INT(xw_web_props_parse("not json", strlen("not json"), "en-us", &out), -1);
  TEST_CHECK(out == NULL);
  TEST_CHECK_EQ_INT(xw_web_props_parse("{}", strlen("{}"), "en-us", &out), -1);
  TEST_CHECK_EQ_INT(
      xw_web_props_parse("{\"general\":{}}", strlen("{\"general\":{}}"), "en-us", &out), -1);
  TEST_CHECK_EQ_INT(xw_web_props_parse(NULL, 0, "en-us", &out), -1);
  TEST_CHECK_EQ_INT(xw_web_props_parse(kProps, strlen(kProps), "en-us", NULL), -1);
}

static void test_parse_groups_skipped(void) {
  TEST_BEGIN("group headings without a type are skipped");
  XWWebProp *out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kWithGroups, strlen(kWithGroups), "en-us", &out), 2);
  TEST_CHECK(out != NULL);
  if (out) {
    TEST_CHECK_EQ_STR(out[0].name, "a");
    TEST_CHECK_EQ_STR(out[1].name, "b");
  }
  free(out);
}

static void test_parse_large(void) {
  TEST_BEGIN("parse handles more than the old 256-property cap");
  char *json = make_many_props(300);
  XWWebProp *out = NULL;
  TEST_CHECK(json != NULL);
  if (!json)
    return;
  TEST_CHECK_EQ_INT(xw_web_props_parse(json, strlen(json), "en-us", &out), 300);
  TEST_CHECK(out != NULL);
  if (out) {
    TEST_CHECK_EQ_STR(out[0].name, "p0");
    TEST_CHECK_EQ_STR(out[0].value, "0");
    TEST_CHECK_EQ_STR(out[299].name, "p299");
    TEST_CHECK_EQ_STR(out[299].value, "299");
  }
  free(out);
  free(json);
}

static void test_parse_labels(void) {
  TEST_BEGIN("parse resolves localized labels");
  XWWebProp *out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kLocalized, strlen(kLocalized), "zh-chs", &out), 3);
  TEST_CHECK(out != NULL);
  if (out) {
    TEST_CHECK_EQ_STR(out[0].name, "zoom");
    TEST_CHECK_EQ_STR(out[0].label, "缩放, %");
    TEST_CHECK_EQ_STR(out[1].label, "背景");
    TEST_CHECK_EQ_STR(out[2].label, "备注");
  }
  free(out);

  out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kLocalized, strlen(kLocalized), "en-us", &out), 3);
  TEST_CHECK(out != NULL);
  if (out)
    TEST_CHECK_EQ_STR(out[0].label, "Zoom, %");
  free(out);

  out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kLocalized, strlen(kLocalized), "ru-ru", &out), 3);
  TEST_CHECK(out != NULL);
  if (out)
    TEST_CHECK_EQ_STR(out[0].label, "Масштаб, %");
  free(out);
}

static void test_label_fallback(void) {
  TEST_BEGIN("missing locale falls back to same language, then en-us");
  XWWebProp *out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kLocalized, strlen(kLocalized), "ja-jp", &out), 3);
  TEST_CHECK(out != NULL);
  if (out)
    TEST_CHECK_EQ_STR(out[0].label, "Zoom, %");
  free(out);

  out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kLocalized, strlen(kLocalized), "zh-cht", &out), 3);
  TEST_CHECK(out != NULL);
  if (out)
    TEST_CHECK_EQ_STR(out[0].label, "缩放, %");
  free(out);

  out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kLocalized, strlen(kLocalized), NULL, &out), 3);
  TEST_CHECK(out != NULL);
  if (out)
    TEST_CHECK_EQ_STR(out[0].label, "Zoom, %");
  free(out);
}

static void test_locale_mapping(void) {
  TEST_BEGIN("POSIX locale tags map to WE localization keys");
  char b[32];
  xw_web_props_locale_for("zh_CN.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "zh-chs");
  xw_web_props_locale_for("zh_TW.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "zh-cht");
  xw_web_props_locale_for("zh_HK", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "zh-cht");
  xw_web_props_locale_for("ja_JP.eucJP", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "ja-jp");
  xw_web_props_locale_for("ko_KR.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "ko-kr");
  xw_web_props_locale_for("ru_RU.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "ru-ru");
  xw_web_props_locale_for("en_US.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
  xw_web_props_locale_for("C.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
  xw_web_props_locale_for("POSIX", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
  xw_web_props_locale_for("de_DE.UTF-8", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
  xw_web_props_locale_for("kok_IN", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
  xw_web_props_locale_for("rue", b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
  xw_web_props_locale_for(NULL, b, sizeof(b));
  TEST_CHECK_EQ_STR(b, "en-us");
}

static void test_label_sanitized(void) {
  TEST_BEGIN("labels are safe for one TSV line");
  static const char kBad[] =
      "{\"general\":{\"properties\":{"
      "\"x\":{\"type\":\"text\",\"value\":\"v\",\"text\":\"ui_x\"}},"
      "\"localization\":{\"en-us\":{\"ui_x\":\"<b>Hi</b>\\tThere\\n\"}}}}";
  XWWebProp *out = NULL;
  TEST_CHECK_EQ_INT(
      xw_web_props_parse(kBad, strlen(kBad), "en-us", &out), 1);
  TEST_CHECK(out != NULL);
  if (out)
    TEST_CHECK_EQ_STR(out[0].label, "Hi There ");
  free(out);
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

static void test_object_js(void) {
  TEST_BEGIN("object literal for sequenced delivery");
  XWWebProp ps[1];
  snprintf(ps[0].name, sizeof(ps[0].name), "roleScale");
  snprintf(ps[0].type, sizeof(ps[0].type), "slider");
  snprintf(ps[0].value, sizeof(ps[0].value), "200");
  char js[512];
  TEST_CHECK_EQ_INT(xw_web_props_object_js(ps, 1, js, sizeof(js)), 0);
  TEST_CHECK_EQ_STR(js, "{\"roleScale\":{\"value\":\"200\"}}");
  TEST_CHECK_EQ_INT(xw_web_props_object_js(NULL, 0, js, sizeof(js)), -1);
  TEST_CHECK_EQ_INT((long long)xw_web_props_object_js_len(ps, 1),
                    (long long)(strlen(js) + 1));
}

static void test_object_js_len_escaped(void) {
  TEST_BEGIN("object length accounts for JSON escaping");
  XWWebProp ps[2];
  snprintf(ps[0].name, sizeof(ps[0].name), "quote");
  snprintf(ps[0].type, sizeof(ps[0].type), "text");
  snprintf(ps[0].value, sizeof(ps[0].value), "a\"b\\c\nd");
  snprintf(ps[1].name, sizeof(ps[1].name), "ctrl");
  snprintf(ps[1].type, sizeof(ps[1].type), "text");
  snprintf(ps[1].value, sizeof(ps[1].value), "x\x01y");
  char js[512];
  size_t need = xw_web_props_object_js_len(ps, 2);
  TEST_CHECK(need > 0 && need <= sizeof(js));
  TEST_CHECK_EQ_INT(xw_web_props_object_js(ps, 2, js, sizeof(js)), 0);
  TEST_CHECK_EQ_INT((long long)need, (long long)(strlen(js) + 1));
  TEST_CHECK_EQ_INT(xw_web_props_object_js_len(NULL, 1), 0);
  TEST_CHECK_EQ_INT(xw_web_props_object_js_len(ps, 0), 0);
}

static void test_apply_large(void) {
  TEST_BEGIN("apply handles payloads larger than one fixed 8K fragment");
  const int n = 600;
  XWWebProp *ps = calloc((size_t)n, sizeof(*ps));
  size_t need;
  char *js;
  int i;
  TEST_CHECK(ps != NULL);
  if (!ps)
    return;
  for (i = 0; i < n; i++) {
    snprintf(ps[i].name, sizeof(ps[i].name), "p%d", i);
    snprintf(ps[i].type, sizeof(ps[i].type), "slider");
    snprintf(ps[i].value, sizeof(ps[i].value), "%d", i);
  }
  need = xw_web_props_object_js_len(ps, n);
  TEST_CHECK(need > XW_WEB_PROP_JS_MAX);
  js = need ? malloc(need + 128) : NULL;
  TEST_CHECK(js != NULL);
  if (js) {
    TEST_CHECK_EQ_INT(xw_web_props_apply_js(ps, n, js, need + 128), 0);
    TEST_CHECK(strstr(js, "\"p0\":{\"value\":\"0\"}") != NULL);
    TEST_CHECK(strstr(js, "\"p599\":{\"value\":\"599\"}") != NULL);
  }
  free(js);
  free(ps);
}

static void test_normalize_color(void) {
  TEST_BEGIN("color values normalize to WE triple");
  char out[1024];
  /* native triple passes through */
  TEST_CHECK_EQ_INT(
      xw_web_props_normalize("color", "1.000000 1.000000 1.000000",
                              out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "1.000000 1.000000 1.000000");
  /* hex forms convert */
  TEST_CHECK_EQ_INT(
      xw_web_props_normalize("color", "#00ff00", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "0.000000 1.000000 0.000000");
  TEST_CHECK_EQ_INT(
      xw_web_props_normalize("color", "#0F0", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "0.000000 1.000000 0.000000");
  /* garbage is rejected, not silently ignored */
  TEST_CHECK_EQ_INT(
      xw_web_props_normalize("color", "green", out, sizeof(out)), -1);
  TEST_CHECK_EQ_INT(xw_web_props_normalize("color", "#12", out, sizeof(out)), -1);
  TEST_CHECK_EQ_INT(xw_web_props_normalize("color", "", out, sizeof(out)), -1);
  /* non-color types pass through untouched */
  TEST_CHECK_EQ_INT(
      xw_web_props_normalize("slider", "200", out, sizeof(out)), 0);
  TEST_CHECK_EQ_STR(out, "200");
  TEST_CHECK_EQ_INT(xw_web_props_normalize(NULL, "x", out, sizeof(out)), -1);
  TEST_CHECK_EQ_INT(
      xw_web_props_normalize("color", "#00ff00", NULL, 0), -1);
}

int main(void) {
  TEST_SUITE("web_props");
  test_parse_ok();
  test_parse_bad();
  test_parse_groups_skipped();
  test_parse_large();
  test_parse_labels();
  test_label_fallback();
  test_locale_mapping();
  test_label_sanitized();
  test_apply_js();
  test_object_js();
  test_object_js_len_escaped();
  test_apply_large();
  test_normalize_color();
  TEST_SUMMARY();
}
