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

static void test_text_layer(void) {
  XWTextLayer *l = NULL;
  XWError err = {0};
  /* the shape the corpus uses: scriptProperties with defaults, and an
   * update(value) that returns the new text */
  const char *src =
      "'use strict';\n"
      "export var scriptProperties = createScriptProperties()\n"
      "  .addCheckbox({ name: 'upper', value: true })\n"
      "  .addText({ name: 'sep', value: '-' })\n"
      "  .finish();\n"
      "export function update(value) {\n"
      "  var v = value + scriptProperties.sep + 'x';\n"
      "  return scriptProperties.upper ? v.toUpperCase() : v;\n"
      "}\n";
  TEST_BEGIN("text layers: update() drives the layer's text");
  TEST_CHECK_EQ_INT(xw_script_text_layer_create(&l, src, "ab", &err), 0);
  TEST_CHECK(l != NULL);
  if (!l)
    return;
  TEST_CHECK_EQ_STR(xw_script_text_layer_update(l), "AB-X");
  /* update() receives the layer's *current* text, so a script that appends
   * accumulates - the engine feeds the previous result back in too */
  TEST_CHECK_EQ_STR(xw_script_text_layer_update(l), "AB-X-X");
  xw_script_text_layer_free(l);

  /* a script without update() keeps whatever text it was given */
  l = NULL;
  TEST_CHECK_EQ_INT(xw_script_text_layer_create(&l, "var x = 1;\n", "keep", &err),
                    0);
  if (l) {
    TEST_CHECK_EQ_STR(xw_script_text_layer_update(l), "keep");
    xw_script_text_layer_free(l);
  }

  /* a broken script is rejected instead of crashing */
  l = NULL;
  TEST_CHECK_EQ_INT(xw_script_text_layer_create(&l, "function ( {", "t", &err),
                    -1);
  TEST_CHECK(l == NULL);
  TEST_CHECK_EQ_STR(xw_script_text_layer_update(NULL), "");
}

static void test_prop_scripts(void) {
  XWPropScript *p = NULL;
  XWError err = {0};
  float in[3] = {1.0f, 3.0f, 0.0f}, out[3] = {0, 0, 0};
  TEST_BEGIN("property scripts: update(value) rewrites a Vec3 property");
  TEST_CHECK_EQ_INT(xw_script_prop_create(
                        &p, "export function update(value) { value.x = value.y * 2; return value; }\n",
                        &err),
                    0);
  if (p) {
    TEST_CHECK_EQ_INT(xw_script_prop_update(p, in, out), 0);
    TEST_CHECK(out[0] > 5.99f && out[0] < 6.01f);
    TEST_CHECK(out[1] > 2.99f && out[1] < 3.01f);
    xw_script_prop_free(p);
  }

  /* the real corpus script: 2794098047's "秒数.scale" */
  p = NULL;
  TEST_CHECK_EQ_INT(
      xw_script_prop_create(
          &p,
          "'use strict';\n"
          "export let __workshopId = '2760603383';\n"
          "export function update(value) {\n"
          "  let second = new Date().getSeconds();\n"
          "  value.x = second / 60;\n"
          "  return value;\n"
          "}\n",
          &err),
      0);
  if (p) {
    out[0] = out[1] = out[2] = -1.0f;
    TEST_CHECK_EQ_INT(xw_script_prop_update(p, in, out), 0);
    TEST_CHECK(out[0] >= 0.0f && out[0] < 1.0f); /* 0..59 seconds / 60 */
    TEST_CHECK(out[1] > 2.99f && out[1] < 3.01f);
    xw_script_prop_free(p);
  }

  /* scriptProperties + WEMath, the helpers the corpus scripts use */
  p = NULL;
  TEST_CHECK_EQ_INT(
      xw_script_prop_create(
          &p,
          "export var scriptProperties = createScriptProperties()\n"
          "  .addSlider({ name: 'gain', value: 2 })\n"
          "  .finish();\n"
          "export function update(value) {\n"
          "  value.x = WEMath.mix(value.x, 10, scriptProperties.gain / 4);\n"
          "  return value;\n"
          "}\n",
          &err),
      0);
  if (p) {
    in[0] = 0.0f;
    TEST_CHECK_EQ_INT(xw_script_prop_update(p, in, out), 0);
    TEST_CHECK(out[0] > 4.99f && out[0] < 5.01f); /* mix(0,10,0.5) */
    xw_script_prop_free(p);
  }

  /* a script that throws (e.g. thisScene is not modelled yet) is refused so
   * the caller keeps the property's base value */
  p = NULL;
  TEST_CHECK_EQ_INT(xw_script_prop_create(
                        &p, "export function update(value) { return thisScene.getLayer(1); }\n",
                        &err),
                    0);
  if (p) {
    TEST_CHECK_EQ_INT(xw_script_prop_update(p, in, out), -1);
    xw_script_prop_free(p);
  }

  /* a broken script is rejected at create time */
  p = NULL;
  TEST_CHECK_EQ_INT(xw_script_prop_create(&p, "function ( {", &err), -1);
  TEST_CHECK(p == NULL);
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
  test_text_layer();
  test_prop_scripts();
  TEST_SUMMARY();
}
