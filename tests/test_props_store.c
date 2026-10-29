#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/props_store.h"

static void make_tmp_state(char *out, size_t n) {
  static int seq = 0;
  snprintf(out, n, "/tmp/opencode/xw-props-test-%d-%d", (int)getpid(), seq++);
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", out);
  (void)system(cmd);
  setenv("XDG_STATE_HOME", out, 1);
}

static void test_key_stable(void) {
  TEST_BEGIN("same root has stable key, different roots differ");
  char a[1024], b[1024], c[1024];
  TEST_CHECK_EQ_INT(xw_props_store_path("/wall/bd2", a, sizeof(a)), 0);
  TEST_CHECK_EQ_INT(xw_props_store_path("/wall/bd2", b, sizeof(b)), 0);
  TEST_CHECK_EQ_STR(a, b);
  TEST_CHECK_EQ_INT(xw_props_store_path("/wall/other", c, sizeof(c)), 0);
  TEST_CHECK(!(strcmp(a, c) == 0));
  TEST_CHECK_EQ_INT(xw_props_store_path(NULL, a, sizeof(a)), -1);
  TEST_CHECK_EQ_INT(xw_props_store_path("", a, sizeof(a)), -1);
}

static void test_roundtrip(void) {
  TEST_BEGIN("save/load roundtrips type and value");
  char base[256];
  make_tmp_state(base, sizeof(base));
  XWStoredProp in[2];
  snprintf(in[0].name, sizeof(in[0].name), "zoom");
  snprintf(in[0].type, sizeof(in[0].type), "slider");
  snprintf(in[0].value, sizeof(in[0].value), "200");
  snprintf(in[1].name, sizeof(in[1].name), "bg");
  snprintf(in[1].type, sizeof(in[1].type), "color");
  snprintf(in[1].value, sizeof(in[1].value), "0.000000 1.000000 0.000000");
  TEST_CHECK_EQ_INT(xw_props_store_save("/wall/bd2", in, 2), 0);
  XWStoredProp out[8];
  int n = 0;
  TEST_CHECK_EQ_INT(xw_props_store_load("/wall/bd2", out, 8, &n), 0);
  TEST_CHECK_EQ_INT(n, 2);
  TEST_CHECK_EQ_STR(out[0].name, "zoom");
  TEST_CHECK_EQ_STR(out[0].value, "200");
  TEST_CHECK_EQ_STR(out[1].type, "color");
}

static void test_missing_and_bad(void) {
  TEST_BEGIN("missing returns 1, bad file returns -1 without crash");
  char base[256];
  make_tmp_state(base, sizeof(base));
  XWStoredProp out[8];
  int n = 0;
  TEST_CHECK_EQ_INT(xw_props_store_load("/wall/nonexistent", out, 8, &n), 1);
  /* write garbage where the sidecar should be */
  char path[1024];
  TEST_CHECK_EQ_INT(xw_props_store_path("/wall/bd2", path, sizeof(path)), 0);
  char dir[1024];
  snprintf(dir, sizeof(dir), "%s", path);
  char *slash = strrchr(dir, '/');
  if (slash) {
    *slash = '\0';
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s && echo 'not json' > %s", dir, path);
    (void)system(cmd);
  }
  TEST_CHECK_EQ_INT(xw_props_store_load("/wall/bd2", out, 8, &n), -1);
}

static void test_empty_deletes(void) {
  TEST_BEGIN("saving empty set deletes the sidecar");
  char base[256];
  make_tmp_state(base, sizeof(base));
  XWStoredProp one[1];
  snprintf(one[0].name, sizeof(one[0].name), "zoom");
  snprintf(one[0].type, sizeof(one[0].type), "slider");
  snprintf(one[0].value, sizeof(one[0].value), "200");
  TEST_CHECK_EQ_INT(xw_props_store_save("/wall/bd2", one, 1), 0);
  TEST_CHECK_EQ_INT(xw_props_store_save("/wall/bd2", NULL, 0), 0);
  XWStoredProp out[8];
  int n = 0;
  TEST_CHECK_EQ_INT(xw_props_store_load("/wall/bd2", out, 8, &n), 1);
}

static void test_port_roundtrip(void) {
  TEST_BEGIN("http port sidecar roundtrips, garbage rejected");
  char base[256];
  make_tmp_state(base, sizeof(base));
  TEST_CHECK_EQ_INT(xw_props_port_load("/wall/bd2"), -1);
  TEST_CHECK_EQ_INT(xw_props_port_save("/wall/bd2", 43779), 0);
  TEST_CHECK_EQ_INT(xw_props_port_load("/wall/bd2"), 43779);
  TEST_CHECK_EQ_INT(xw_props_port_save("/wall/bd2", 0), -1);
  TEST_CHECK_EQ_INT(xw_props_port_save("/wall/bd2", 70000), -1);
  TEST_CHECK_EQ_INT(xw_props_port_save(NULL, 80), -1);
  TEST_CHECK_EQ_INT(xw_props_port_load(NULL), -1);
  TEST_CHECK_EQ_INT(xw_props_port_load("/wall/bd2"), 43779);
}

static void test_migrated_marker(void) {
  TEST_BEGIN("migration marker sets once and sticks");
  char base[256];
  make_tmp_state(base, sizeof(base));
  TEST_CHECK_EQ_INT(xw_props_migrated_done("/wall/bd2"), 0);
  TEST_CHECK_EQ_INT(xw_props_migrated_set("/wall/bd2"), 0);
  TEST_CHECK_EQ_INT(xw_props_migrated_done("/wall/bd2"), 1);
  TEST_CHECK_EQ_INT(xw_props_migrated_done(NULL), 0);
  TEST_CHECK_EQ_INT(xw_props_migrated_set(NULL), -1);
}

int main(void) {
  TEST_SUITE("props_store");
  test_key_stable();
  test_roundtrip();
  test_missing_and_bad();
  test_empty_deletes();
  test_port_roundtrip();
  test_migrated_marker();
  TEST_SUMMARY();
}
