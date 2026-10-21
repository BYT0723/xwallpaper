#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "../src/keybinds.h"

/* The user's real ~/.config/dwm/wallpaper.keys, verbatim (note the double
 * space after "seek" and the trailing newline). */
static const char *kRealFile =
    "l seek  5\n"
    "h seek -5\n"
    "k seek  30\n"
    "j seek -30\n"
    "9 add volume -2\n"
    "0 add volume 2\n"
    "- cycle mute\n"
    "= cycle pause\n"
    "r cycle_values video-rotate 90 180 270 0\n";

static void test_real_file(void) {
  XWBind b[XW_KEYBINDS_MAX];
  int n;
  TEST_BEGIN("the real wallpaper.keys parses into 9 binds");
  n = xw_keybinds_parse(kRealFile, b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 9);
  if (n != 9)
    return;
  TEST_CHECK_EQ_STR(b[0].key, "l");
  TEST_CHECK_EQ_INT(b[0].kind, XW_ACT_SEEK);
  TEST_CHECK(b[0].seek_secs > 4.99 && b[0].seek_secs < 5.01);
  TEST_CHECK_EQ_STR(b[1].key, "h");
  TEST_CHECK(b[1].seek_secs > -5.01 && b[1].seek_secs < -4.99);
  TEST_CHECK_EQ_STR(b[2].key, "k");
  TEST_CHECK(b[2].seek_secs > 29.99 && b[2].seek_secs < 30.01);
  TEST_CHECK_EQ_STR(b[3].key, "j");
  TEST_CHECK(b[3].seek_secs > -30.01 && b[3].seek_secs < -29.99);
  TEST_CHECK_EQ_STR(b[4].key, "9");
  TEST_CHECK_EQ_INT(b[4].kind, XW_ACT_VOLUME);
  TEST_CHECK_EQ_INT(b[4].volume_delta, -2);
  TEST_CHECK_EQ_STR(b[5].key, "0");
  TEST_CHECK_EQ_INT(b[5].volume_delta, 2);
  TEST_CHECK_EQ_STR(b[6].key, "-");
  TEST_CHECK_EQ_INT(b[6].kind, XW_ACT_MUTE);
  TEST_CHECK_EQ_STR(b[7].key, "=");
  TEST_CHECK_EQ_INT(b[7].kind, XW_ACT_PAUSE);
  TEST_CHECK_EQ_STR(b[8].key, "r");
  TEST_CHECK_EQ_INT(b[8].kind, XW_ACT_ROTATE);
  TEST_CHECK_EQ_INT(b[8].n_rotate, 4);
  TEST_CHECK_EQ_INT(b[8].rotate[0], 90);
  TEST_CHECK_EQ_INT(b[8].rotate[1], 180);
  TEST_CHECK_EQ_INT(b[8].rotate[2], 270);
  TEST_CHECK_EQ_INT(b[8].rotate[3], 0);
}

static void test_lines(void) {
  XWBind b[XW_KEYBINDS_MAX];
  int n;
  TEST_BEGIN("blank lines, comments, stray whitespace and CRLF tolerated");
  n = xw_keybinds_parse("\n"
                        "   \n"
                        "# a comment\n"
                        "   l   seek   5   \r\n"
                        "\t0\tadd\tvolume\t2\t\n"
                        "\n",
                        b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 2);
  if (n != 2)
    return;
  TEST_CHECK_EQ_STR(b[0].key, "l");
  TEST_CHECK_EQ_INT(b[0].kind, XW_ACT_SEEK);
  TEST_CHECK_EQ_STR(b[1].key, "0");
  TEST_CHECK_EQ_INT(b[1].volume_delta, 2);
}

static void test_bad_lines(void) {
  XWBind b[XW_KEYBINDS_MAX];
  int n;
  TEST_BEGIN("unknown actions and malformed lines are skipped, not fatal");
  n = xw_keybinds_parse("l wobble 5\n"
                        "o seek\n"
                        "p add volume zzz\n"
                        "r cycle_values otherprop 1 2\n"
                        "s\n"
                        "t seek 5\n",
                        b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 1); /* only the last line is a valid seek */
  if (n == 1) {
    TEST_CHECK_EQ_STR(b[0].key, "t");
    TEST_CHECK(b[0].seek_secs > 4.99 && b[0].seek_secs < 5.01);
  }
  TEST_CHECK_EQ_INT(xw_keybinds_parse(NULL, b, XW_KEYBINDS_MAX), -1);
  TEST_CHECK_EQ_INT(xw_keybinds_parse("l seek 5\n", NULL, XW_KEYBINDS_MAX), -1);
  TEST_CHECK_EQ_INT(xw_keybinds_parse("l seek 5\n", b, 0), -1);
}

static void test_max_entries(void) {
  XWBind b[3];
  int n;
  TEST_BEGIN("parsing stops at the caller's capacity");
  n = xw_keybinds_parse("a seek 1\nb seek 2\nc seek 3\nd seek 4\ne seek 5\n", b,
                        3);
  TEST_CHECK_EQ_INT(n, 3);
  if (n == 3) {
    TEST_CHECK_EQ_STR(b[0].key, "a");
    TEST_CHECK_EQ_STR(b[2].key, "c");
  }
}

static void test_long_line(void) {
  XWBind b[XW_KEYBINDS_MAX];
  char big[4096];
  int n;
  TEST_BEGIN("an over-long line is dropped, the rest still parse");
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  n = xw_keybinds_parse(big, b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 0);
  n = xw_keybinds_parse("l seek 5\n", b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 1);
}

static void test_find_and_norm(void) {
  XWBind b[XW_KEYBINDS_MAX];
  char key[64];
  int n;
  TEST_BEGIN("key lookup normalizes simple ASCII and keeps modifiers");
  n = xw_keybinds_parse(kRealFile, b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 9);
  TEST_CHECK(xw_keybinds_find(b, n, "l") == &b[0]);
  TEST_CHECK(xw_keybinds_find(b, n, "r") == &b[8]);
  TEST_CHECK(xw_keybinds_find(b, n, "z") == NULL);
  TEST_CHECK(xw_keybinds_find(NULL, 0, "l") == NULL);
  TEST_CHECK(xw_keybinds_find(b, n, NULL) == NULL);

  TEST_CHECK_EQ_INT(xw_keybinds_norm_key("L", key, sizeof(key)), 0);
  TEST_CHECK_EQ_STR(key, "l");
  TEST_CHECK_EQ_INT(xw_keybinds_norm_key("XF86AudioMute", key, sizeof(key)), 0);
  TEST_CHECK_EQ_STR(key, "XF86AudioMute");
  TEST_CHECK_EQ_INT(xw_keybinds_norm_key("Super+l", key, sizeof(key)), 0);
  TEST_CHECK_EQ_STR(key, "Super+l");
  TEST_CHECK_EQ_INT(xw_keybinds_norm_key("", key, sizeof(key)), -1);
  TEST_CHECK_EQ_INT(xw_keybinds_norm_key(NULL, key, sizeof(key)), -1);
}

static void test_audio_apply(void) {
  int out = -1;
  TEST_BEGIN("volume accepts relative and absolute values, clamped 0..100");
  TEST_CHECK_EQ_INT(xw_audio_apply(50, "+2", &out), 0);
  TEST_CHECK_EQ_INT(out, 52);
  TEST_CHECK_EQ_INT(xw_audio_apply(50, "-2", &out), 0);
  TEST_CHECK_EQ_INT(out, 48);
  TEST_CHECK_EQ_INT(xw_audio_apply(50, "50", &out), 0);
  TEST_CHECK_EQ_INT(out, 50);
  TEST_CHECK_EQ_INT(xw_audio_apply(99, "+5", &out), 0);
  TEST_CHECK_EQ_INT(out, 100);
  TEST_CHECK_EQ_INT(xw_audio_apply(1, "-5", &out), 0);
  TEST_CHECK_EQ_INT(out, 0);
  TEST_CHECK_EQ_INT(xw_audio_apply(150, "0", &out), 0);
  TEST_CHECK_EQ_INT(out, 0);
  TEST_CHECK_EQ_INT(xw_audio_apply(50, "abc", &out), -1);
  TEST_CHECK_EQ_INT(xw_audio_apply(50, "", &out), -1);
  TEST_CHECK_EQ_INT(xw_audio_apply(50, NULL, &out), -1);
  TEST_CHECK_EQ_INT(xw_audio_apply(50, "+2", NULL), -1);
}

static void test_ctl_toggle(void) {
  int out = -1;
  TEST_BEGIN("on/off/toggle resolves a boolean control");
  TEST_CHECK_EQ_INT(xw_ctl_toggle(0, "toggle", &out), 0);
  TEST_CHECK_EQ_INT(out, 1);
  TEST_CHECK_EQ_INT(xw_ctl_toggle(1, "toggle", &out), 0);
  TEST_CHECK_EQ_INT(out, 0);
  /* empty means the keybind case: toggle */
  TEST_CHECK_EQ_INT(xw_ctl_toggle(1, "", &out), 0);
  TEST_CHECK_EQ_INT(out, 0);
  TEST_CHECK_EQ_INT(xw_ctl_toggle(0, NULL, &out), 0);
  TEST_CHECK_EQ_INT(out, 1);
  TEST_CHECK_EQ_INT(xw_ctl_toggle(0, "on", &out), 0);
  TEST_CHECK_EQ_INT(out, 1);
  TEST_CHECK_EQ_INT(xw_ctl_toggle(1, "off", &out), 0);
  TEST_CHECK_EQ_INT(out, 0);
  TEST_CHECK_EQ_INT(xw_ctl_toggle(0, "bogus", &out), -1);
  TEST_CHECK_EQ_INT(xw_ctl_toggle(0, "toggle", NULL), -1);
}

static void test_values_cycle(void) {
  const int vals[4] = {90, 180, 270, 0};
  int out = -1;
  TEST_BEGIN("cycle_values advances and wraps, unknown current starts at 0");
  TEST_CHECK_EQ_INT(xw_values_cycle(vals, 4, 90, &out), 0);
  TEST_CHECK_EQ_INT(out, 180);
  TEST_CHECK_EQ_INT(xw_values_cycle(vals, 4, 270, &out), 0);
  TEST_CHECK_EQ_INT(out, 0);
  TEST_CHECK_EQ_INT(xw_values_cycle(vals, 4, 0, &out), 0);
  TEST_CHECK_EQ_INT(out, 90);
  TEST_CHECK_EQ_INT(xw_values_cycle(vals, 4, 45, &out), 0);
  TEST_CHECK_EQ_INT(out, 90);
  TEST_CHECK_EQ_INT(xw_values_cycle(vals, 0, 90, &out), -1);
  TEST_CHECK_EQ_INT(xw_values_cycle(NULL, 4, 90, &out), -1);
}

static void test_to_action(void) {
  XWBind b[XW_KEYBINDS_MAX];
  char name[32], value[64];
  int n;
  TEST_BEGIN("a bind maps onto the backend control name and argument");
  n = xw_keybinds_parse(kRealFile, b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 9);
  if (n != 9)
    return;
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[0], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(name, "seek");
  TEST_CHECK_EQ_STR(value, "5");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[3], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(value, "-30");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[4], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(name, "volume");
  TEST_CHECK_EQ_STR(value, "-2");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[5], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(value, "+2");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[6], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(name, "mute");
  TEST_CHECK_EQ_STR(value, "toggle");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[7], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(name, "pause");
  TEST_CHECK_EQ_STR(value, "toggle");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[8], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(name, "rotate");
  TEST_CHECK_EQ_STR(value, "cycle");
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(NULL, name, sizeof(name), value,
                                          sizeof(value)),
                    -1);
}

static void test_reload(void) {
  XWBind b[XW_KEYBINDS_MAX];
  char name[32], value[64];
  int n;
  TEST_BEGIN("Ctrl+r reload parses and maps to the reload action");
  n = xw_keybinds_parse("Ctrl+r reload\n", b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 1);
  if (n != 1)
    return;
  TEST_CHECK_EQ_STR(b[0].key, "Ctrl+r");
  TEST_CHECK_EQ_INT(b[0].kind, XW_ACT_RELOAD);
  TEST_CHECK_EQ_INT(xw_keybinds_to_action(&b[0], name, sizeof(name), value,
                                          sizeof(value)),
                    0);
  TEST_CHECK_EQ_STR(name, "reload");
  n = xw_keybinds_parse("r cycle_values video-rotate 90 180 270 0\n"
                        "Ctrl+r reload\n",
                        b, XW_KEYBINDS_MAX);
  TEST_CHECK_EQ_INT(n, 2);
  if (n == 2) {
    TEST_CHECK_EQ_STR(b[0].key, "r");
    TEST_CHECK_EQ_INT(b[0].kind, XW_ACT_ROTATE);
    TEST_CHECK_EQ_STR(b[1].key, "Ctrl+r");
    TEST_CHECK_EQ_INT(b[1].kind, XW_ACT_RELOAD);
    TEST_CHECK(xw_keybinds_find(b, n, "Ctrl+r") == &b[1]);
    TEST_CHECK(xw_keybinds_find(b, n, "r") == &b[0]);
  }
}

static void test_load_and_default_path(void) {
  static const char *path = "/tmp/xw_test_keys_load.keys";
  XWBind b[XW_KEYBINDS_MAX];
  char def[512];
  FILE *f;
  TEST_BEGIN("the key file loads from disk and has a default location");
  f = fopen(path, "w");
  TEST_CHECK(f != NULL);
  if (f) {
    fputs(kRealFile, f);
    fclose(f);
  }
  TEST_CHECK_EQ_INT(xw_keybinds_load(path, b, XW_KEYBINDS_MAX), 9);
  TEST_CHECK_EQ_INT(xw_keybinds_load("/tmp/xw-no-such-keys-file", b,
                                     XW_KEYBINDS_MAX),
                    -1);
  TEST_CHECK_EQ_INT(xw_keybinds_load(NULL, b, XW_KEYBINDS_MAX), -1);
  unlink(path);

  setenv("XDG_CONFIG_HOME", "/tmp/xw-cfg", 1);
  TEST_CHECK_EQ_INT(xw_keybinds_default_path(def, sizeof(def)), 0);
  TEST_CHECK_EQ_STR(def, "/tmp/xw-cfg/xwallpaper/keys");
  unsetenv("XDG_CONFIG_HOME");
  setenv("HOME", "/tmp/xw-home", 1);
  TEST_CHECK_EQ_INT(xw_keybinds_default_path(def, sizeof(def)), 0);
  TEST_CHECK_EQ_STR(def, "/tmp/xw-home/.config/xwallpaper/keys");
  TEST_CHECK_EQ_INT(xw_keybinds_default_path(NULL, 0), -1);
}

int main(void) {
  TEST_SUITE("keybinds");
  test_real_file();
  test_lines();
  test_bad_lines();
  test_max_entries();
  test_long_line();
  test_find_and_norm();
  test_audio_apply();
  test_ctl_toggle();
  test_values_cycle();
  test_to_action();
  test_reload();
  test_load_and_default_path();
  TEST_SUMMARY();
}
