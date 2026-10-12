#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/scene_model.h"
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

static void test_bindings_rw(void) {
  TEST_BEGIN("scene object read/write from JS, unknown id throws");
  XWScene scene;
  XWUserProp props[1];
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  memset(&scene, 0, sizeof(scene));
  scene.n_objects = 2;
  scene.objects = calloc(2, sizeof(XWSceneObject));
  TEST_CHECK(scene.objects != NULL);
  if (!scene.objects) {
    xw_script_destroy(s);
    return;
  }
  snprintf(scene.objects[0].id, sizeof(scene.objects[0].id), "bg");
  scene.objects[0].type = XW_OBJ_IMAGE;
  scene.objects[0].alpha.kind = XW_DYN_CONSTANT;
  scene.objects[0].alpha.constant = 0.8f;
  scene.objects[0].origin[0] = 10.0f;
  scene.objects[0].origin[1] = 20.0f;
  scene.objects[0].scale[0] = 1.0f;
  scene.objects[0].scale[1] = 1.0f;
  scene.objects[0].visible = 1;
  snprintf(scene.objects[1].id, sizeof(scene.objects[1].id), "logo");
  scene.objects[1].type = XW_OBJ_IMAGE;
  scene.objects[1].alpha.kind = XW_DYN_USER;
  snprintf(scene.objects[1].alpha.ref, sizeof(scene.objects[1].alpha.ref),
           "opacity");
  scene.objects[1].alpha.constant = 1.0f;
  scene.objects[1].visible = 1;
  memset(props, 0, sizeof(props));
  snprintf(props[0].name, sizeof(props[0].name), "opacity");
  snprintf(props[0].type, sizeof(props[0].type), "slider");
  snprintf(props[0].value, sizeof(props[0].value), "0.5");
  TEST_CHECK_EQ_INT(xw_script_set_scene(s, &scene), 0);
  TEST_CHECK_EQ_INT(xw_script_set_props(s, props, 1), 0);

  XWError err = {0};
  double v = 0;
  TEST_CHECK_EQ_INT(xw_script_eval(s, "scene.setAlpha('bg', 0.3);", "t.js", &err),
                    0);
  TEST_CHECK_EQ_INT(
      xw_script_eval(s, "a = scene.getAlpha('bg'); scene.setOrigin('bg', 5, 6);"
                        "scene.setScale('bg', 2, 3); scene.setVisible('bg', false);",
                     "t.js", &err),
      0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "a", &v), 0);
  TEST_CHECK(v > 0.29 && v < 0.31);
  TEST_CHECK(scene.objects[0].alpha.constant > 0.29 &&
             scene.objects[0].alpha.constant < 0.31);
  TEST_CHECK_EQ_INT((int)scene.objects[0].origin[0], 5);
  TEST_CHECK_EQ_INT((int)scene.objects[0].origin[1], 6);
  TEST_CHECK_EQ_INT((int)scene.objects[0].scale[0], 2);
  TEST_CHECK_EQ_INT(scene.objects[0].visible, 0);
  TEST_CHECK_EQ_INT(xw_script_eval(s, "v = scene.isVisible('bg');", "t.js", &err),
                    0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "v", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 0);
  TEST_CHECK_EQ_INT(xw_script_eval(s, "o = scene.getOrigin('bg');", "t.js", &err),
                    0);
  /* unknown id setter throws, engine survives */
  memset(&err, 0, sizeof(err));
  TEST_CHECK_EQ_INT(xw_script_eval(s, "scene.setAlpha('nope', 1);", "t.js", &err),
                    -1);
  TEST_CHECK(err.code != 0);
  free(scene.objects);
  xw_script_destroy(s);
}

static void test_props_and_resolve(void) {
  TEST_BEGIN("user props get/set, USER alpha resolves with fallback");
  XWScene scene;
  XWUserProp props[1];
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  memset(&scene, 0, sizeof(scene));
  scene.n_objects = 1;
  scene.objects = calloc(1, sizeof(XWSceneObject));
  TEST_CHECK(scene.objects != NULL);
  if (!scene.objects) {
    xw_script_destroy(s);
    return;
  }
  snprintf(scene.objects[0].id, sizeof(scene.objects[0].id), "logo");
  scene.objects[0].alpha.kind = XW_DYN_USER;
  snprintf(scene.objects[0].alpha.ref, sizeof(scene.objects[0].alpha.ref),
           "opacity");
  scene.objects[0].alpha.constant = 1.0f;
  memset(props, 0, sizeof(props));
  snprintf(props[0].name, sizeof(props[0].name), "opacity");
  snprintf(props[0].value, sizeof(props[0].value), "0.5");
  TEST_CHECK_EQ_INT(xw_script_set_scene(s, &scene), 0);
  TEST_CHECK_EQ_INT(xw_script_set_props(s, props, 1), 0);

  XWError err = {0};
  double v = 0;
  TEST_CHECK_EQ_INT(xw_script_eval(s, "p = scene.getProperty('opacity');", "t.js",
                                  &err),
                    0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "p", &v), 0);
  TEST_CHECK(v > 0.49 && v < 0.51);
  TEST_CHECK(xw_script_resolve_alpha(s, &scene.objects[0]) > 0.49 &&
             xw_script_resolve_alpha(s, &scene.objects[0]) < 0.51);
  /* setAlpha on a USER ref writes an override, visible to resolve */
  TEST_CHECK_EQ_INT(xw_script_eval(s, "scene.setAlpha('logo', 0.9);", "t.js", &err),
                    0);
  TEST_CHECK(xw_script_resolve_alpha(s, &scene.objects[0]) > 0.89 &&
             xw_script_resolve_alpha(s, &scene.objects[0]) < 0.91);
  /* C-side override API + missing prop fallback */
  char buf[64];
  TEST_CHECK_EQ_INT(xw_script_set_user_prop(s, "rain", "true"), 0);
  TEST_CHECK_EQ_INT(xw_script_get_user_prop(s, "rain", buf, sizeof(buf)), 0);
  TEST_CHECK_EQ_STR(buf, "true");
  TEST_CHECK_EQ_INT(xw_script_eval(s, "r = scene.getProperty('rain');", "t.js",
                                  &err),
                    0);
  snprintf(scene.objects[0].alpha.ref, sizeof(scene.objects[0].alpha.ref),
           "ghost");
  TEST_CHECK(xw_script_resolve_alpha(s, &scene.objects[0]) > 0.99);
  TEST_CHECK(xw_script_resolve_alpha(NULL, NULL) > 0.99);
  free(scene.objects);
  xw_script_destroy(s);
}

static void test_mouse_and_no_scene(void) {
  TEST_BEGIN("mouse feed readable, empty engine answers zeros");
  XWScript *s = make_engine();
  TEST_CHECK(s != NULL);
  if (!s)
    return;
  XWError err = {0};
  double v = 0;
  xw_script_set_mouse(s, 12.0f, 34.0f);
  TEST_CHECK_EQ_INT(
      xw_script_eval(s, "mx = input.getMouseX(); my = input.getMouseY();",
                     "t.js", &err),
      0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "mx", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 12);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "my", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 34);
  TEST_CHECK_EQ_INT(xw_script_eval(s, "c = scene.getObjectCount();", "t.js", &err),
                    0);
  TEST_CHECK_EQ_INT(xw_script_get_number(s, "c", &v), 0);
  TEST_CHECK_EQ_INT((int)v, 0);
  TEST_CHECK_EQ_INT(xw_script_set_scene(NULL, NULL), -1);
  TEST_CHECK_EQ_INT(xw_script_set_props(NULL, NULL, 0), -1);
  xw_script_set_mouse(NULL, 0, 0);
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
  test_bindings_rw();
  test_props_and_resolve();
  test_mouse_and_no_scene();
  TEST_SUMMARY();
}
