#include <stdio.h>

#include "harness.h"
#include "../src/cli.h"
#include "../src/wl.h"

static const XWWlOutput outs[] = {
  {"DP-1", 0, 0, 1920, 1080},
  {"HDMI-1", 1920, 0, 1280, 720},
};

static void test_wl_geom_passthrough(void) {
  TEST_BEGIN("have_geom passes through verbatim");
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.have_geom = 1;
  cfg.geom_x = 10;
  cfg.geom_y = 20;
  cfg.geom_w = 800;
  cfg.geom_h = 600;
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  memset(name, 0xAA, sizeof(name));
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(outs, 2, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK_EQ_INT(x, 10);
  TEST_CHECK_EQ_INT(y, 20);
  TEST_CHECK_EQ_INT(w, 800);
  TEST_CHECK_EQ_INT(h, 600);
  TEST_CHECK_EQ_STR(name, "");
}

static void test_wl_output_hit(void) {
  TEST_BEGIN("TARGET_OUTPUT resolves by name");
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_OUTPUT;
  snprintf(cfg.output, sizeof(cfg.output), "HDMI-1");
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(outs, 2, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK_EQ_INT(x, 1920);
  TEST_CHECK_EQ_INT(y, 0);
  TEST_CHECK_EQ_INT(w, 1280);
  TEST_CHECK_EQ_INT(h, 720);
  TEST_CHECK_EQ_STR(name, "HDMI-1");
}

static void test_wl_output_miss(void) {
  TEST_BEGIN("unknown output fails");
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_OUTPUT;
  snprintf(cfg.output, sizeof(cfg.output), "NOPE-9");
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(outs, 2, &cfg, &x, &y, &w, &h, name), -1);
}

static void test_wl_primary(void) {
  TEST_BEGIN("TARGET_PRIMARY and TARGET_ALL use the largest output");
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  cfg.target = XW_TARGET_PRIMARY;
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(outs, 2, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK_EQ_INT(w, 1920);
  TEST_CHECK_EQ_STR(name, "DP-1");
  cfg.target = XW_TARGET_ALL;
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(outs, 2, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK_EQ_STR(name, "DP-1");
}

static void test_wl_primary_largest(void) {
  TEST_BEGIN("primary is the largest output regardless of order");
  static const XWWlOutput o[] = {
    {"SMALL", 0, 0, 1080, 1920},
    {"MID", 1080, 0, 1920, 1080},
    {"BIG", 3000, 0, 2560, 1440},
  };
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(o, 3, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK_EQ_STR(name, "BIG");
  TEST_CHECK_EQ_INT(x, 3000);
  TEST_CHECK_EQ_INT(w, 2560);
}

static void test_wl_primary_tie(void) {
  TEST_BEGIN("primary tie falls back to first enumerated");
  static const XWWlOutput o[] = {
    {"FIRST", 0, 0, 1920, 1080},
    {"SECOND", 1920, 0, 1080, 1920},
  };
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(o, 2, &cfg, &x, &y, &w, &h, name), 0);
  TEST_CHECK_EQ_STR(name, "FIRST");
}

static void test_wl_no_outputs(void) {
  TEST_BEGIN("primary with no outputs fails");
  XWConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.target = XW_TARGET_PRIMARY;
  int x = -1, y = -1;
  unsigned int w = 0, h = 0;
  char name[XW_WL_OUTPUT_NAME_LEN];
  TEST_CHECK_EQ_INT(
      xw_wl_resolve_geometry(NULL, 0, &cfg, &x, &y, &w, &h, name), -1);
}

static void test_wl_list(void) {
  TEST_BEGIN("list_outputs matches x11 format");
  char *buf = NULL;
  size_t len = 0;
  FILE *f = open_memstream(&buf, &len);
  xw_wl_list_outputs(outs, 2, f);
  fclose(f);
  TEST_CHECK_EQ_STR(buf, "  DP-1 1920x1080+0+0\n"
                         "  HDMI-1 1280x720+1920+0\n");
  free(buf);

  buf = NULL;
  len = 0;
  f = open_memstream(&buf, &len);
  xw_wl_list_outputs(NULL, 0, f);
  fclose(f);
  TEST_CHECK_EQ_STR(buf, "  (no wayland outputs available)\n");
  free(buf);
}

int main(void) {
  TEST_SUITE("wl");
  test_wl_geom_passthrough();
  test_wl_output_hit();
  test_wl_output_miss();
  test_wl_primary();
  test_wl_primary_largest();
  test_wl_primary_tie();
  test_wl_no_outputs();
  test_wl_list();
  TEST_SUMMARY();
}
