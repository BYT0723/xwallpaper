#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/state.h"

static void test_path_xdg(void) {
  TEST_BEGIN("state path uses XDG_STATE_HOME when set");
  setenv("XDG_STATE_HOME", "/tmp/xstate", 1);
  setenv("HOME", "/home/u", 1);
  char p[256];
  TEST_CHECK_EQ_INT(xw_state_path(p, sizeof(p)), 0);
  TEST_CHECK_EQ_STR(p, "/tmp/xstate/xwallpaper/state");
}

static void test_path_home_fallback(void) {
  TEST_BEGIN("state path falls back to HOME/.local/state");
  unsetenv("XDG_STATE_HOME");
  setenv("HOME", "/home/u", 1);
  char p[256];
  TEST_CHECK_EQ_INT(xw_state_path(p, sizeof(p)), 0);
  TEST_CHECK_EQ_STR(p, "/home/u/.local/state/xwallpaper/state");
}

static void test_path_for_wayland(void) {
  TEST_BEGIN("state path for wayland uses the state-wayland file");
  setenv("XDG_STATE_HOME", "/tmp/xstate", 1);
  char p[256];
  TEST_CHECK_EQ_INT(xw_state_path_for(1, p, sizeof(p)), 0);
  TEST_CHECK_EQ_STR(p, "/tmp/xstate/xwallpaper/state-wayland");
}

static void test_path_for_x11_matches_legacy(void) {
  TEST_BEGIN("state path for x11 matches the legacy path");
  setenv("XDG_STATE_HOME", "/tmp/xstate", 1);
  char a[256], b[256];
  TEST_CHECK_EQ_INT(xw_state_path_for(0, a, sizeof(a)), 0);
  TEST_CHECK_EQ_INT(xw_state_path(b, sizeof(b)), 0);
  TEST_CHECK_EQ_STR(a, b);
  TEST_CHECK_EQ_STR(a, "/tmp/xstate/xwallpaper/state");
}

static void test_path_for_wayland_fallback(void) {
  TEST_BEGIN("state-wayland falls back to HOME/.local/state too");
  unsetenv("XDG_STATE_HOME");
  setenv("HOME", "/home/u", 1);
  char p[256];
  TEST_CHECK_EQ_INT(xw_state_path_for(1, p, sizeof(p)), 0);
  TEST_CHECK_EQ_STR(p, "/home/u/.local/state/xwallpaper/state-wayland");
}

static void test_path_for_truncation(void) {
  TEST_BEGIN("state path reports truncation on a tiny buffer");
  setenv("XDG_STATE_HOME", "/tmp/xstate", 1);
  char p[10];
  TEST_CHECK_EQ_INT(xw_state_path_for(1, p, sizeof(p)), -1);
  TEST_CHECK_EQ_INT(xw_state_path_for(0, p, sizeof(p)), -1);
}

static void test_roundtrip_geom(void) {
  TEST_BEGIN("format/parse round-trips a geom entry");
  XWStateEntry e;
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "grp_dual");
  e.type = XW_BACKEND_VIDEO;
  snprintf(e.path, sizeof(e.path), "/w/with space/clip.mp4");
  e.rotation = 90;
  e.have_geom = 1;
  e.x = 100;
  e.y = 200;
  e.w = 1920;
  e.h = 1080;
  e.keep = 1;

  char line[256];
  TEST_CHECK(xw_state_format_line(&e, line, sizeof(line)) > 0);
  TEST_CHECK_EQ_STR(line, "grp_dual\tvideo\t/w/with space/clip.mp4\t90\t1920x1080+100+200\t1\n");

  XWStateEntry o;
  memset(&o, 0, sizeof(o));
  TEST_CHECK_EQ_INT(xw_state_parse_line(line, &o), 0);
  TEST_CHECK_EQ_STR(o.name, "grp_dual");
  TEST_CHECK_EQ_INT(o.type, XW_BACKEND_VIDEO);
  TEST_CHECK_EQ_STR(o.path, "/w/with space/clip.mp4");
  TEST_CHECK_EQ_INT(o.rotation, 90);
  TEST_CHECK_EQ_INT(o.have_geom, 1);
  TEST_CHECK_EQ_INT(o.x, 100);
  TEST_CHECK_EQ_INT(o.y, 200);
  TEST_CHECK_EQ_INT((int)o.w, 1920);
  TEST_CHECK_EQ_INT((int)o.h, 1080);
  TEST_CHECK_EQ_INT(o.keep, 1);
}

static void test_roundtrip_output(void) {
  TEST_BEGIN("format/parse round-trips an output entry (geom '-')");
  XWStateEntry e;
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "DP-1");
  e.type = XW_BACKEND_IMAGE;
  snprintf(e.path, sizeof(e.path), "/w/wall.png");
  e.rotation = 0;
  e.have_geom = 0;
  e.keep = 0;

  char line[256];
  TEST_CHECK(xw_state_format_line(&e, line, sizeof(line)) > 0);
  TEST_CHECK_EQ_STR(line, "DP-1\timage\t/w/wall.png\t0\t-\t0\n");

  XWStateEntry o;
  memset(&o, 0, sizeof(o));
  TEST_CHECK_EQ_INT(xw_state_parse_line(line, &o), 0);
  TEST_CHECK_EQ_STR(o.name, "DP-1");
  TEST_CHECK_EQ_INT(o.type, XW_BACKEND_IMAGE);
  TEST_CHECK_EQ_INT(o.have_geom, 0);
  TEST_CHECK_EQ_INT(o.keep, 0);
}

static void test_roundtrip_escape(void) {
  TEST_BEGIN("tab/newline/backslash in name & path round-trip via escaping");
  XWStateEntry e;
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "grp_a\tb");
  e.type = XW_BACKEND_IMAGE;
  snprintf(e.path, sizeof(e.path), "/w/line\nbreak\\and\t tab.png");
  e.rotation = 0;
  e.have_geom = 1;
  e.x = 0;
  e.y = 0;
  e.w = 100;
  e.h = 100;
  e.keep = 0;

  char line[256];
  TEST_CHECK(xw_state_format_line(&e, line, sizeof(line)) > 0);
  TEST_CHECK_EQ_STR(line,
                    "grp_a\\tb\timage\t/w/line\\nbreak\\\\and\\t tab.png\t0\t100x100+0+0\t0\n");

  XWStateEntry o;
  memset(&o, 0, sizeof(o));
  TEST_CHECK_EQ_INT(xw_state_parse_line(line, &o), 0);
  TEST_CHECK_EQ_STR(o.name, "grp_a\tb");
  TEST_CHECK_EQ_STR(o.path, "/w/line\nbreak\\and\t tab.png");
  TEST_CHECK_EQ_INT(o.keep, 0);
}

static void test_parse_bad(void) {
  TEST_BEGIN("malformed lines are rejected");
  XWStateEntry e;
  memset(&e, 0, sizeof(e));
  TEST_CHECK(xw_state_parse_line("not-enough-fields\n", &e) != 0);
  TEST_CHECK(xw_state_parse_line("a\tbogus\t/p\t0\t-\t0\n", &e) != 0);
  TEST_CHECK(xw_state_parse_line("a\timage\t/p\t0\txx\t0\n", &e) != 0);
}

static void test_save_load_file(void) {
  TEST_BEGIN("save + load file round-trips multiple entries");
  char path[] = "/tmp/xw_state_test_XXXXXX";
  int fd = mkstemp(path);
  TEST_CHECK(fd >= 0);
  close(fd);
  unlink(path);

  XWStateEntry e[2];
  memset(e, 0, sizeof(e));
  snprintf(e[0].name, sizeof(e[0].name), "eDP");
  e[0].type = XW_BACKEND_IMAGE;
  snprintf(e[0].path, sizeof(e[0].path), "/a/b.png");
  e[0].rotation = 0;
  e[0].have_geom = 1;
  e[0].x = 0; e[0].y = 0; e[0].w = 1920; e[0].h = 1080;
  e[0].keep = 0;
  snprintf(e[1].name, sizeof(e[1].name), "Screen");
  e[1].type = XW_BACKEND_VIDEO;
  snprintf(e[1].path, sizeof(e[1].path), "/v/clip.mp4");
  e[1].rotation = 90;
  e[1].have_geom = 0;
  e[1].keep = 0;

  TEST_CHECK_EQ_INT(xw_state_save(path, e, 2), 0);

  XWStateEntry out[4];
  int count = 0;
  TEST_CHECK_EQ_INT(xw_state_load(path, out, 4, &count), 0);
  TEST_CHECK_EQ_INT(count, 2);
  TEST_CHECK_EQ_STR(out[0].name, "eDP");
  TEST_CHECK_EQ_STR(out[1].name, "Screen");
  TEST_CHECK_EQ_INT(out[1].rotation, 90);

  unlink(path);
  /* 目录里残留的 tmp 由 xw_state_save 用 pid 后缀, 不会残留 */
  char tmp[300];
  snprintf(tmp, sizeof(tmp), "%s.tmp.*", path);
  (void)tmp;
}

static void test_load_missing(void) {
  TEST_BEGIN("load of missing file yields zero entries");
  XWStateEntry out[4];
  int count = 99;
  TEST_CHECK_EQ_INT(xw_state_load("/tmp/xw_state_nonexistent_12345", out, 4, &count), 0);
  TEST_CHECK_EQ_INT(count, 0);
}

static void test_upsert(void) {
  TEST_BEGIN("upsert replaces by name and appends new");
  char path[] = "/tmp/xw_state_up_XXXXXX";
  int fd = mkstemp(path);
  TEST_CHECK(fd >= 0);
  close(fd);
  unlink(path);

  XWStateEntry e;
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "eDP");
  e.type = XW_BACKEND_IMAGE;
  snprintf(e.path, sizeof(e.path), "/a/one.png");
  e.have_geom = 0;
  TEST_CHECK_EQ_INT(xw_state_upsert(path, &e), 0);

  snprintf(e.path, sizeof(e.path), "/a/two.png");
  TEST_CHECK_EQ_INT(xw_state_upsert(path, &e), 0);

  XWStateEntry other;
  memset(&other, 0, sizeof(other));
  snprintf(other.name, sizeof(other.name), "HDMI");
  other.type = XW_BACKEND_VIDEO;
  snprintf(other.path, sizeof(other.path), "/v/c.mp4");
  other.have_geom = 0;
  TEST_CHECK_EQ_INT(xw_state_upsert(path, &other), 0);

  XWStateEntry out[4];
  int count = 0;
  TEST_CHECK_EQ_INT(xw_state_load(path, out, 4, &count), 0);
  TEST_CHECK_EQ_INT(count, 2);
  TEST_CHECK_EQ_STR(out[0].name, "eDP");
  TEST_CHECK_EQ_STR(out[0].path, "/a/two.png");
  TEST_CHECK_EQ_STR(out[1].name, "HDMI");

  unlink(path);
}

static void test_clear_file(void) {
  TEST_BEGIN("save with count 0 clears the file");
  char path[] = "/tmp/xw_state_clear_XXXXXX";
  int fd = mkstemp(path);
  TEST_CHECK(fd >= 0);
  close(fd);
  unlink(path);

  XWStateEntry e;
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "eDP");
  e.type = XW_BACKEND_IMAGE;
  snprintf(e.path, sizeof(e.path), "/a/b.png");
  e.have_geom = 0;
  TEST_CHECK_EQ_INT(xw_state_save(path, &e, 1), 0);

  TEST_CHECK_EQ_INT(xw_state_save(path, NULL, 0), 0);
  XWStateEntry out[4];
  int count = 99;
  TEST_CHECK_EQ_INT(xw_state_load(path, out, 4, &count), 0);
  TEST_CHECK_EQ_INT(count, 0);

  unlink(path);
}

int main(void) {
  TEST_SUITE("state");
  test_path_xdg();
  test_path_home_fallback();
  test_path_for_wayland();
  test_path_for_x11_matches_legacy();
  test_path_for_wayland_fallback();
  test_path_for_truncation();
  test_roundtrip_geom();
  test_roundtrip_output();
  test_roundtrip_escape();
  test_parse_bad();
  test_save_load_file();
  test_load_missing();
  test_upsert();
  test_clear_file();
  TEST_SUMMARY();
}
