#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/scene_script.h"

static XWScript *make_engine(void) {
  XWScript *s = NULL;
  XWError err = {0};
  if (xw_script_create(&s, &err) != 0)
    return NULL;
  return s;
}

static void test_eval_arithmetic(void) {
  TEST_BEGIN("evaluates expressions, reads globals");
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  XWError err = {0};
  double v = 0;
  TEST_CHECK_EQ_INT(xw_script_eval(s, "x = 40 + 2;", "t.js", &err), 0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "x", &v), 0);
  TEST_CHECK(v > 41.9 && v < 42.1);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "missing", &v), -1);
  xw_script_destroy(s);
}

static void test_syntax_error(void) {
  TEST_BEGIN("syntax errors fail with a message");
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  XWError err = {0};
  TEST_CHECK_EQ_INT(xw_script_eval(s, "function (((;", "bad.js", &err), -1);
  TEST_CHECK(err.code != 0 && err.msg[0] != '\0');
  /* engine still usable after a bad eval */
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(xw_script_eval(s, "ok = 1;", "t.js", &err), 0);
  xw_script_destroy(s);
}

static void test_timeout(void) {
  TEST_BEGIN("setTimeout fires once after its delay");
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  XWError err = {0};
  double v = 0;
  TEST_CHECK_EQ_INT(
      xw_script_eval(s, "fired = 0; setTimeout(function() { fired = 1; }, 20);",
                     "t.js", &err),
      0);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "fired", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 0);
  usleep(40000);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "fired", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 1);
  usleep(40000);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "fired", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 1);
  xw_script_destroy(s);
}

static void test_interval_and_cancel(void) {
  TEST_BEGIN("setInterval repeats, clearTimeout cancels");
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  XWError err = {0};
  double v = 0;
  TEST_CHECK_EQ_INT(
      xw_script_eval(s,
                     "n = 0; id = setInterval(function() { n = n + 1; }, 10);"
                     "cancelled = 0; setTimeout(function() { cancelled = 1; }, 15);"
                     "clearTimeout(99999);",
                     "t.js", &err),
      0);
  /* cancel the timeout before it fires (id 2: interval took id 1) */
  TEST_CHECK_EQ_INT(xw_script_eval(s, "clearTimeout(2);", "t.js", &err), 0);
  usleep(55000);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "n", &v), 0);
  TEST_CHECK(v >= 1.0);
  usleep(30000);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "n", &v), 0);
  TEST_CHECK(v >= 2.0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "cancelled", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 0);
  TEST_CHECK_EQ_INT(xw_script_eval(s, "clearInterval(id);", "t.js", &err), 0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "n", &v), 0);
  double before = v;
  usleep(30000);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "n", &v), 0);
  TEST_CHECK(v == before);
  xw_script_destroy(s);
}

static void test_time_and_error_isolation(void) {
  TEST_BEGIN("getTime/setTime plus throwing timers don't kill the engine");
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  XWError err = {0};
  double v = 0;
  TEST_CHECK_EQ_INT(xw_script_eval(s, "t0 = getTime();", "t.js", &err), 0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "t0", &v), 0);
  TEST_CHECK(v >= 0.0 && v < 60000.0);
  TEST_CHECK_EQ_INT(xw_script_eval(s, "setTime(1000); t1 = getTime();", "t.js", &err),
                    0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "t1", &v), 0);
  TEST_CHECK(v >= 1000.0 && v < 2000.0);
  TEST_CHECK_EQ_INT(
      xw_script_eval(s, "ok = 0; setTimeout(function() { throw 'boom'; }, 5);"
                        "setTimeout(function() { ok = 1; }, 30);",
                     "t.js", &err),
      0);
  usleep(15000);
  xw_script_tick(s);
  TEST_CHECK(xw_script_last_error(s)[0] != '\0');
  usleep(30000);
  xw_script_tick(s);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "ok", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 1);
  xw_script_destroy(s);
  TEST_CHECK_EQ_INT(xw_script_create(NULL, &err), -1);
  TEST_CHECK_EQ_INT(xw_script_eval(NULL, "1;", "t.js", &err), -1);
  xw_script_destroy(NULL);
  xw_script_tick(NULL);
}

int main(void) {
  TEST_SUITE("scene_script");
  test_eval_arithmetic();
  test_syntax_error();
  test_timeout();
  test_interval_and_cancel();
  test_time_and_error_isolation();
  TEST_SUMMARY();
}
