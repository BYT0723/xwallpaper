/* Integration test: scene sound objects with the SDL dummy audio driver.
 * Synthesizes a short WAV into a temp dir (no repo binaries), then loads,
 * plays, re-volumes, pauses, stops and frees it through the sound module.
 * Skips when no audio backend initializes. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../harness.h"
#include "../../src/scene_sound.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 0.2s 440Hz mono 16-bit 44100Hz WAV. */
static int write_tone(const char *path) {
  static const int rate = 44100, nsamp = 8820;
  FILE *f = fopen(path, "wb");
  uint8_t hdr[44];
  int32_t sz = nsamp * 2;
  if (!f)
    return -1;
  memset(hdr, 0, sizeof(hdr));
  memcpy(hdr, "RIFF", 4);
  memcpy(hdr + 4, &(int32_t){36 + sz}, 4);
  memcpy(hdr + 8, "WAVEfmt ", 8);
  memcpy(hdr + 16, &(int32_t){16}, 4);
  memcpy(hdr + 20, &(int16_t){1}, 2);
  memcpy(hdr + 22, &(int16_t){1}, 2);
  memcpy(hdr + 24, &rate, 4);
  memcpy(hdr + 28, &(int32_t){rate * 2}, 4);
  memcpy(hdr + 32, &(int16_t){2}, 2);
  memcpy(hdr + 34, &(int16_t){16}, 2);
  memcpy(hdr + 36, "data", 4);
  memcpy(hdr + 40, &sz, 4);
  if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
    fclose(f);
    return -1;
  }
  for (int i = 0; i < nsamp; i++) {
    int16_t v = (int16_t)(16000.0 * sin(2.0 * M_PI * 440.0 * i / rate));
    if (fwrite(&v, 1, 2, f) != 2) {
      fclose(f);
      return -1;
    }
  }
  fclose(f);
  return 0;
}

int main(void) {
  TEST_SUITE("scene sound (dummy driver)");
  TEST_BEGIN("load/play/volume/pause/stop lifecycle");

  setenv("SDL_AUDIODRIVER", "dummy", 1);

  char dir[] = "/tmp/xw-snd-XXXXXX";
  TEST_CHECK(mkdtemp(dir) != NULL);
  char wav[1024];
  snprintf(wav, sizeof(wav), "%s/tone.wav", dir);
  TEST_CHECK_EQ_INT(write_tone(wav), 0);

  XWError err = {0};
  if (xw_sound_sys_init(&err) != 0) {
    printf("  SKIP: no audio backend (%s)\n", err.msg);
    unlink(wav);
    rmdir(dir);
    TEST_SUMMARY();
  }

  struct XWSound *s = NULL;
  TEST_CHECK_EQ_INT(xw_sound_load(&s, wav, &err), 0);
  if (!s) {
    TEST_FAIL("load: %s", err.msg);
    xw_sound_sys_free();
    TEST_SUMMARY();
  }
  TEST_CHECK_EQ_INT(xw_sound_play(s, 0.7f, 100.0, 1, &err), 0);
  usleep(50000);
  TEST_CHECK_EQ_INT(xw_sound_playing(s), 1);
  TEST_CHECK_EQ_INT(xw_sound_set_volumes(s, 0.5f, 50.0), 0);
  TEST_CHECK_EQ_INT(xw_sound_pause(s, 1), 0);
  TEST_CHECK_EQ_INT(xw_sound_pause(s, 0), 0);
  TEST_CHECK_EQ_INT(xw_sound_playing(s), 1);
  xw_sound_stop(s);
  TEST_CHECK_EQ_INT(xw_sound_playing(s), 0);
  xw_sound_free(s);

  struct XWSound *bad = NULL;
  TEST_CHECK_EQ_INT(xw_sound_load(&bad, "/nonexistent/x.wav", &err), -1);
  TEST_CHECK(bad == NULL);

  xw_sound_sys_free();
  unlink(wav);
  rmdir(dir);
  TEST_SUMMARY();
}
