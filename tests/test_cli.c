#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "harness.h"
#include "../src/cli.h"

static XWConfig cfg;
static XWError err;

static void reset(void) {
  memset(&cfg, 0, sizeof(cfg));
  memset(&err, 0, sizeof(err));
}

static int parse(char **argv, int argc) {
  return xw_cli_parse(argc, argv, &cfg, &err);
}

static void test_detect_backend(void) {
  TEST_BEGIN("detects image by extension");
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("a.png"), XW_BACKEND_IMAGE);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("A.JPG"), XW_BACKEND_IMAGE);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("/x/y.jpeg"), XW_BACKEND_IMAGE);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("b.webp"), XW_BACKEND_IMAGE);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("c.bmp"), XW_BACKEND_IMAGE);
}

static void test_detect_video(void) {
  TEST_BEGIN("detects video by extension");
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("clip.mp4"), XW_BACKEND_VIDEO);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("x.webm"), XW_BACKEND_VIDEO);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("y.mkv"), XW_BACKEND_VIDEO);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("z.mov"), XW_BACKEND_VIDEO);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("anim.gif"), XW_BACKEND_VIDEO);
}

static void test_detect_web(void) {
  TEST_BEGIN("only svg detects as web; html needs a directory");
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("g.svg"), XW_BACKEND_WEB);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("page.html"), -1);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("page.htm"), -1);
}

static void test_detect_dir_and_url(void) {
  TEST_BEGIN("existing dir and http(s) url detect as web");
  char *t = strdup("/tmp/xw-cli-dir-XXXXXX");
  TEST_CHECK(t != NULL && mkdtemp(t) != NULL);
  if (t) {
    TEST_CHECK_EQ_INT(xw_cli_detect_backend(t), XW_BACKEND_WEB);
    rmdir(t);
    free(t);
  }
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("https://example.com/"), XW_BACKEND_WEB);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("http://localhost:8000/"), XW_BACKEND_WEB);
}

static void test_detect_unknown(void) {
  TEST_BEGIN("unknown extension returns -1");
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("file.xyz"), -1);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend("noext"), -1);
  TEST_CHECK_EQ_INT(xw_cli_detect_backend(""), -1);
}

static void test_set_default(void) {
  TEST_BEGIN("set with no target -> primary, auto backend");
  reset();
  char *argv[] = {"xwallpaper", "set", "wall.png"};
  TEST_CHECK_EQ_INT(parse(argv, 3), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_SET);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_PRIMARY);
  TEST_CHECK_EQ_INT(cfg.backend_explicit, -1);
  TEST_CHECK_EQ_STR(cfg.path, "wall.png");
  TEST_CHECK_EQ_INT(cfg.scale_mode, XW_SCALE_FILL);
  TEST_CHECK_EQ_INT(cfg.volume, 100);
}

static void test_set_output(void) {
  TEST_BEGIN("set -m DP-1 targets that output");
  reset();
  char *argv[] = {"xwallpaper", "set", "-m", "DP-1", "clip.mp4"};
  TEST_CHECK_EQ_INT(parse(argv, 5), 0);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_OUTPUT);
  TEST_CHECK_EQ_STR(cfg.output, "DP-1");
}

static void test_set_all(void) {
  TEST_BEGIN("set --all targets every output");
  reset();
  char *argv[] = {"xwallpaper", "set", "--all", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv, 4), 0);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_ALL);
}

static void test_set_explicit_backend(void) {
  TEST_BEGIN("explicit --video overrides extension");
  reset();
  char *argv[] = {"xwallpaper", "set", "--video", "file.bin"};
  TEST_CHECK_EQ_INT(parse(argv, 4), 0);
  TEST_CHECK_EQ_INT(cfg.backend_explicit, XW_BACKEND_VIDEO);
  TEST_CHECK_EQ_STR(cfg.path, "file.bin");
}

static void test_set_geometry(void) {
  TEST_BEGIN("set -g parses WxH+X+Y and requires --name");
  reset();
  /* -g without --name must error */
  char *argv_bad[] = {"xwallpaper", "set", "-g", "1920x1080+100+200", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv_bad, 5), -1);

  /* -g with --name parses */
  reset();
  char *argv[] = {"xwallpaper", "set", "-g", "1920x1080+100+200", "--name", "dual", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv, 7), 0);
  TEST_CHECK_EQ_INT(cfg.have_geom, 1);
  TEST_CHECK_EQ_INT(cfg.geom_w, 1920);
  TEST_CHECK_EQ_INT(cfg.geom_h, 1080);
  TEST_CHECK_EQ_INT(cfg.geom_x, 100);
  TEST_CHECK_EQ_INT(cfg.geom_y, 200);
  TEST_CHECK_EQ_STR(cfg.name, "dual");
}

static void test_set_video_opts(void) {
  TEST_BEGIN("set --video --mute --volume 40");
  reset();
  char *argv[] = {"xwallpaper", "set", "--video", "a.mp4", "--mute", "--volume", "40"};
  TEST_CHECK_EQ_INT(parse(argv, 7), 0);
  TEST_CHECK_EQ_INT(cfg.mute, 1);
  TEST_CHECK_EQ_INT((int)cfg.volume, 40);
}

static void test_window_flags(void) {
  TEST_BEGIN("window attribute flags parsed");
  reset();
  char *argv[] = {"xwallpaper", "set", "-o", "0.5", "-b", "-s", "-st", "-sp", "-nf", "-d", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv, 11), 0);
  TEST_CHECK_EQ_INT((int)(cfg.opacity * 100), 50);
  TEST_CHECK_EQ_INT(cfg.below, 1);
  TEST_CHECK_EQ_INT(cfg.sticky, 1);
  TEST_CHECK_EQ_INT(cfg.skip_taskbar, 1);
  TEST_CHECK_EQ_INT(cfg.skip_pager, 1);
  TEST_CHECK_EQ_INT(cfg.no_focus, 1);
  TEST_CHECK_EQ_INT(cfg.daemonize, 1);
}

static void test_clear(void) {
  TEST_BEGIN("clear targets all by default");
  reset();
  char *argv[] = {"xwallpaper", "clear"};
  TEST_CHECK_EQ_INT(parse(argv, 2), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_CLEAR);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_ALL);
}

static void test_clear_output(void) {
  TEST_BEGIN("clear -m DP-1 targets one output");
  reset();
  char *argv[] = {"xwallpaper", "clear", "-m", "DP-1"};
  TEST_CHECK_EQ_INT(parse(argv, 4), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_CLEAR);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_OUTPUT);
  TEST_CHECK_EQ_STR(cfg.output, "DP-1");
}

static void test_clear_keep(void) {
  TEST_BEGIN("clear --keep sets keep flag (retain last state)");
  reset();
  char *argv[] = {"xwallpaper", "clear", "-m", "HDMI-1", "--keep"};
  TEST_CHECK_EQ_INT(parse(argv, 5), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_CLEAR);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_OUTPUT);
  TEST_CHECK_EQ_STR(cfg.output, "HDMI-1");
  TEST_CHECK_EQ_INT(cfg.keep, 1);
}

static void test_restore(void) {
  TEST_BEGIN("restore selects restore command");
  reset();
  char *argv[] = {"xwallpaper", "restore"};
  TEST_CHECK_EQ_INT(parse(argv, 2), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_RESTORE);

  reset();
  char *argv2[] = {"xwallpaper", "restore", "extra"};
  TEST_CHECK_EQ_INT(parse(argv2, 3), -1);
}

static void test_list(void) {
  TEST_BEGIN("--list selects list command");
  reset();
  char *argv[] = {"xwallpaper", "--list"};
  TEST_CHECK_EQ_INT(parse(argv, 2), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_LIST);
}

static void test_state(void) {
  TEST_BEGIN("--state selects state command");
  reset();
  char *argv[] = {"xwallpaper", "--state"};
  TEST_CHECK_EQ_INT(parse(argv, 2), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_STATE);
  reset();
  char *argv2[] = {"xwallpaper", "--state", "extra"};
  TEST_CHECK(parse(argv2, 3) != 0);
}

static void test_exit(void) {
  TEST_BEGIN("exit selects exit command");
  reset();
  char *argv[] = {"xwallpaper", "exit"};
  TEST_CHECK_EQ_INT(parse(argv, 2), 0);
  TEST_CHECK_EQ_INT(cfg.command, XW_CMD_EXIT);
}

static void test_missing_path(void) {
  TEST_BEGIN("set without path errors");
  reset();
  char *argv[] = {"xwallpaper", "set"};
  TEST_CHECK_EQ_INT(parse(argv, 2), -1);
  TEST_CHECK(err.code != 0);
}

static void test_unknown_flag(void) {
  TEST_BEGIN("unknown flag errors");
  reset();
  char *argv[] = {"xwallpaper", "set", "--bogus", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv, 4), -1);
  TEST_CHECK(err.code != 0);
}

static void test_unknown_command(void) {
  TEST_BEGIN("unknown subcommand errors");
  reset();
  char *argv[] = {"xwallpaper", "frobnicate", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv, 3), -1);
}

static void test_scale_mode_flag(void) {
  TEST_BEGIN("--scale stretch sets image scale mode");
  reset();
  char *argv[] = {"xwallpaper", "set", "--scale", "stretch", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv, 5), 0);
  TEST_CHECK_EQ_INT(cfg.scale_mode, XW_SCALE_STRETCH);

  reset();
  char *argv2[] = {"xwallpaper", "set", "--scale", "tile", "a.png"};
  TEST_CHECK_EQ_INT(parse(argv2, 5), 0);
  TEST_CHECK_EQ_INT(cfg.scale_mode, XW_SCALE_TILE);
}

static void test_rotate_flag(void) {
  TEST_BEGIN("--rotate parses and validates 0/90/180/270");
  reset();
  char *argv[] = {"xwallpaper", "set", "--video", "--rotate", "90", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv, 6), 0);
  TEST_CHECK_EQ_INT(cfg.rotate, 90);

  reset();
  char *argv2[] = {"xwallpaper", "set", "--video", "--rotate", "270", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv2, 6), 0);
  TEST_CHECK_EQ_INT(cfg.rotate, 270);

  /* default is 0 */
  reset();
  char *argv3[] = {"xwallpaper", "set", "--video", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv3, 4), 0);
  TEST_CHECK_EQ_INT(cfg.rotate, 0);

  /* invalid angles must error */
  reset();
  char *argv4[] = {"xwallpaper", "set", "--video", "--rotate", "45", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv4, 6), -1);
  TEST_CHECK(err.code != 0);

  /* non-numeric rotate must error (not silently treated as 0) */
  reset();
  char *argv_bad[] = {"xwallpaper", "set", "--video", "--rotate", "abc", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv_bad, 6), -1);
  TEST_CHECK(err.code != 0);

  reset();
  char *argv5[] = {"xwallpaper", "set", "--video", "--rotate", "180", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv5, 6), 0);
  TEST_CHECK_EQ_INT(cfg.rotate, 180);
}

static void test_keybinds_flag(void) {
  TEST_BEGIN("--keybinds stores the config path");
  reset();
  char *argv[] = {"xwallpaper", "set", "--video", "--keybinds", "/tmp/keys.conf", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv, 6), 0);
  TEST_CHECK_EQ_STR(cfg.keybinds, "/tmp/keys.conf");

  /* default is empty */
  reset();
  char *argv2[] = {"xwallpaper", "set", "--video", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv2, 4), 0);
  TEST_CHECK_EQ_STR(cfg.keybinds, "");
}

static void test_audio_client_name_flag(void) {
  TEST_BEGIN("--audio-client-name parses; default is xwallpaper");
  reset();
  char *argv[] = {"xwallpaper", "set", "--video", "--audio-client-name", "mywall", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv, 7), 0);
  TEST_CHECK_EQ_STR(cfg.audio_client_name, "mywall");

  /* default is "xwallpaper" */
  reset();
  char *argv2[] = {"xwallpaper", "set", "--video", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv2, 4), 0);
  TEST_CHECK_EQ_STR(cfg.audio_client_name, "xwallpaper");

  /* missing value must error */
  reset();
  char *argv3[] = {"xwallpaper", "set", "--video", "--audio-client-name", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv3, 5), -1);
  TEST_CHECK(err.code != 0);
}

static void test_fps_flag(void) {
  TEST_BEGIN("--fps parses and validates a positive frame rate");
  reset();
  char *argv[] = {"xwallpaper", "set", "--video", "--fps", "30", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv, 6), 0);
  TEST_CHECK_EQ_INT((int)cfg.fps, 30);

  /* default is 0 (auto) */
  reset();
  char *argv2[] = {"xwallpaper", "set", "--video", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv2, 4), 0);
  TEST_CHECK_EQ_INT((int)cfg.fps, 0);

  /* non-numeric / non-positive must error */
  reset();
  char *argv3[] = {"xwallpaper", "set", "--video", "--fps", "abc", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv3, 6), -1);
  TEST_CHECK(err.code != 0);

  reset();
  char *argv4[] = {"xwallpaper", "set", "--video", "--fps", "-5", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv4, 6), -1);
  TEST_CHECK(err.code != 0);

  reset();
  char *argv5[] = {"xwallpaper", "set", "--video", "--fps", "0", "a.mp4"};
  TEST_CHECK_EQ_INT(parse(argv5, 6), -1);
  TEST_CHECK(err.code != 0);
}

static void test_set_span(void) {
  TEST_BEGIN("set --span parses layout rect");
  reset();
  char *argv[] = {"xwallpaper", "set", "--span", "3000x1080+0+0", "wide.png"};
  TEST_CHECK_EQ_INT(parse(argv, 5), 0);
  TEST_CHECK_EQ_INT(cfg.target, XW_TARGET_SPAN);
  TEST_CHECK_EQ_INT(cfg.have_span, 1);
  TEST_CHECK_EQ_INT(cfg.span_w, 3000);
  TEST_CHECK_EQ_INT(cfg.span_h, 1080);
  TEST_CHECK_EQ_INT(cfg.span_x, 0);
  TEST_CHECK_EQ_INT(cfg.span_y, 0);
}

static void test_set_span_bad_geom(void) {
  TEST_BEGIN("set --span rejects malformed geometry");
  reset();
  char *argv[] = {"xwallpaper", "set", "--span", "abc", "wide.png"};
  TEST_CHECK_EQ_INT(parse(argv, 5), -1);
  TEST_CHECK(err.code != 0);
}

static void test_set_span_excludes(void) {
  TEST_BEGIN("set --span conflicts with -g/-m/--all");
  reset();
  char *a1[] = {"xwallpaper", "set", "--span", "10x10+0+0",
                "-g", "10x10+0+0", "--name", "n", "w.png"};
  TEST_CHECK_EQ_INT(parse(a1, 9), -1);
  reset();
  char *a2[] = {"xwallpaper", "set", "--span", "10x10+0+0",
                "-m", "DP-1", "w.png"};
  TEST_CHECK_EQ_INT(parse(a2, 7), -1);
  reset();
  char *a3[] = {"xwallpaper", "set", "--span", "10x10+0+0", "--all", "w.png"};
  TEST_CHECK_EQ_INT(parse(a3, 6), -1);
}

static void test_set_span_image_only(void) {
  TEST_BEGIN("set --span rejects video/web backends");
  reset();
  char *a1[] = {"xwallpaper", "set", "--span", "10x10+0+0",
                "--video", "c.mp4"};
  TEST_CHECK_EQ_INT(parse(a1, 6), -1);
  reset();
  char *a2[] = {"xwallpaper", "set", "--span", "10x10+0+0", "c.mp4"};
  TEST_CHECK_EQ_INT(parse(a2, 5), -1);
  reset();
  char *a3[] = {"xwallpaper", "set", "--span", "10x10+0+0",
                "--web", "p.html"};
  TEST_CHECK_EQ_INT(parse(a3, 6), -1);
}

int main(void) {
  TEST_SUITE("cli");
  test_detect_backend();
  test_detect_video();
  test_detect_web();
  test_detect_dir_and_url();
  test_detect_unknown();
  test_set_default();
  test_set_output();
  test_set_all();
  test_set_explicit_backend();
  test_set_geometry();
  test_set_video_opts();
  test_window_flags();
  test_clear();
  test_clear_output();
  test_clear_keep();
  test_restore();
  test_list();
  test_state();
  test_exit();
  test_missing_path();
  test_unknown_flag();
  test_unknown_command();
  test_scale_mode_flag();
  test_rotate_flag();
  test_keybinds_flag();
  test_audio_client_name_flag();
  test_fps_flag();
  test_set_span();
  test_set_span_bad_geom();
  test_set_span_excludes();
  test_set_span_image_only();
  TEST_SUMMARY();
}
