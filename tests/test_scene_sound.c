#include "harness.h"
#include "../src/scene_sound.h"

static void test_mix_volume(void) {
  TEST_BEGIN("object x global maps to 0..128 clamped");
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(0.0f, 100.0), 0);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(1.0f, 100.0), 128);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(0.5f, 50.0), 32);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(1.0f, 0.0), 0);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(2.0f, 100.0), 128);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(-1.0f, 100.0), 0);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(1.0f, 200.0), 128);
  TEST_CHECK_EQ_INT(xw_sound_mix_volume(0.7f, 100.0), 90);
}

static void test_bad_inputs(void) {
  TEST_BEGIN("NULL args rejected without touching any device");
  XWError err = {0};
  struct XWSound *s = NULL;
  TEST_CHECK_EQ_INT(xw_sound_load(NULL, "/x", &err), -1);
  TEST_CHECK_EQ_INT(xw_sound_load(&s, NULL, &err), -1);
  TEST_CHECK(s == NULL);
  TEST_CHECK_EQ_INT(xw_sound_load(&s, "", &err), -1);
  TEST_CHECK_EQ_INT(xw_sound_play(NULL, 1.0f, 100.0, 1, &err), -1);
  TEST_CHECK_EQ_INT(xw_sound_set_volumes(NULL, 1.0f, 100.0), -1);
  TEST_CHECK_EQ_INT(xw_sound_pause(NULL, 1), -1);
  TEST_CHECK_EQ_INT(xw_sound_playing(NULL), 0);
  xw_sound_stop(NULL);
  xw_sound_free(NULL);
  xw_sound_sys_free();
}

int main(void) {
  TEST_SUITE("scene_sound");
  test_mix_volume();
  test_bad_inputs();
  TEST_SUMMARY();
}
