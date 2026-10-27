#include <stdio.h>

#include "harness.h"
#include "../src/osd.h"

static void test_volume(void) {
  char out[128];
  TEST_BEGIN("volume formats as Volume: N%");
  TEST_CHECK(xw_osd_format("volume", NULL, 62, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Volume: 62%");
  TEST_CHECK(xw_osd_format("volume", "+2", 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Volume: 0%");
  TEST_CHECK(xw_osd_format("volume", NULL, 100, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Volume: 100%");
  TEST_CHECK(xw_osd_format("volume", NULL, -5, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Volume: 0%");
  TEST_CHECK(xw_osd_format("volume", NULL, 120, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Volume: 100%");
}

static void test_mute_pause(void) {
  char out[128];
  TEST_BEGIN("mute formats as Mute: on/off");
  TEST_CHECK(xw_osd_format("mute", "toggle", 1, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Mute: on");
  TEST_CHECK(xw_osd_format("mute", "toggle", 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Mute: off");
  TEST_BEGIN("pause formats as Paused/Playing");
  TEST_CHECK(xw_osd_format("pause", "toggle", 1, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Paused");
  TEST_CHECK(xw_osd_format("pause", "toggle", 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Playing");
}

static void test_seek_rotate_reload(void) {
  char out[128];
  TEST_BEGIN("seek keeps sign and appends s");
  TEST_CHECK(xw_osd_format("seek", "5", 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Seek +5s");
  TEST_CHECK(xw_osd_format("seek", "-5", 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Seek -5s");
  TEST_CHECK(xw_osd_format("seek", "+30", 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Seek +30s");
  TEST_BEGIN("rotate formats degrees");
  TEST_CHECK(xw_osd_format("rotate", "cycle", 90, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Rotate: 90");
  TEST_BEGIN("reload formats as Reloaded");
  TEST_CHECK(xw_osd_format("reload", NULL, 0, out, sizeof(out)) == 0);
  TEST_CHECK_EQ_STR(out, "Reloaded");
}

static void test_bad_input(void) {
  char out[128];
  TEST_BEGIN("unknown actions and bad args return -1");
  TEST_CHECK(xw_osd_format("zoom", NULL, 0, out, sizeof(out)) != 0);
  TEST_CHECK(xw_osd_format("seek", NULL, 0, out, sizeof(out)) != 0);
  TEST_CHECK(xw_osd_format("seek", "", 0, out, sizeof(out)) != 0);
  TEST_CHECK(xw_osd_format("seek", "abc", 0, out, sizeof(out)) != 0);
  TEST_CHECK(xw_osd_format(NULL, NULL, 0, out, sizeof(out)) != 0);
  TEST_CHECK(xw_osd_format("volume", NULL, 0, NULL, 0) != 0);
}

int main(void) {
  TEST_SUITE("osd");
  test_volume();
  test_mute_pause();
  test_seek_rotate_reload();
  test_bad_input();
  TEST_SUMMARY();
}
