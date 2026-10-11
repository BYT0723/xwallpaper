#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "../src/we_audio.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define N 2048
static float L[N], R[N];

static void gen_sine(float *b, int n, double freq, double rate, double amp) {
  for (int i = 0; i < n; i++)
    b[i] = (float)(amp * sin(2.0 * M_PI * freq * i / rate));
}

static double ch_mean(const float *o, int from, int to) {
  double s = 0;
  for (int i = from; i < to; i++)
    s += o[i];
  return s / (to - from);
}

static void test_silence_is_zeros(void) {
  TEST_BEGIN("silence maps to all zeros");
  memset(L, 0, sizeof(L));
  memset(R, 0, sizeof(R));
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  for (int i = 0; i < 128; i++)
    TEST_CHECK(out[i] == 0.0f);
}

static void test_bass_sine_peaks_low_bins(void) {
  TEST_BEGIN("100Hz sine peaks in low bins, not high bins");
  gen_sine(L, N, 100.0, 48000.0, 0.9);
  gen_sine(R, N, 100.0, 48000.0, 0.9);
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  double low_l = ch_mean(out, 8, 16);
  double high_l = ch_mean(out, 48, 64);
  double low_r = ch_mean(out, 72, 80);
  double high_r = ch_mean(out, 112, 128);
  TEST_CHECK(low_l > 3.0 * high_l);
  TEST_CHECK(low_r > 3.0 * high_r);
  TEST_CHECK(low_l > 0.05);
}

static void test_stereo_separation(void) {
  TEST_BEGIN("left-only signal stays mostly left");
  gen_sine(L, N, 100.0, 48000.0, 0.9);
  memset(R, 0, sizeof(R));
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  TEST_CHECK(ch_mean(out, 8, 16) > 0.05);
  TEST_CHECK(ch_mean(out, 72, 80) == 0.0f);
}

static void test_fullscale_clamped(void) {
  TEST_BEGIN("full-scale sine clamps to <= 1.0");
  gen_sine(L, N, 100.0, 48000.0, 1.0);
  gen_sine(R, N, 8000.0, 48000.0, 1.0);
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  for (int i = 0; i < 128; i++)
    TEST_CHECK(out[i] >= 0.0f && out[i] <= 1.0f);
}

static void test_format_128(void) {
  TEST_BEGIN("format emits 128-number JSON array");
  float in[128];
  for (int i = 0; i < 128; i++)
    in[i] = i * 0.01f;
  char buf[2048];
  TEST_CHECK_EQ_INT(xw_we_audio_format(in, buf, sizeof(buf)), 0);
  TEST_CHECK(buf[0] == '[');
  TEST_CHECK(buf[strlen(buf) - 1] == ']');
  int commas = 0;
  for (const char *p = buf; *p; p++)
    if (*p == ',')
      commas++;
  TEST_CHECK_EQ_INT(commas, 127);
  char tiny[16];
  TEST_CHECK_EQ_INT(xw_we_audio_format(in, tiny, sizeof(tiny)), -1);
}

static void test_bad_args(void) {
  TEST_BEGIN("null args rejected");
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(NULL, R, N, out), -1);
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, 0, out), -1);
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, NULL), -1);
  char dummy[8];
  TEST_CHECK_EQ_INT(xw_we_audio_format(NULL, dummy, sizeof(dummy)), -1);
}

int main(void) {
  TEST_SUITE("we_audio");
  test_silence_is_zeros();
  test_bass_sine_peaks_low_bins();
  test_stereo_separation();
  test_fullscale_clamped();
  test_format_128();
  test_bad_args();
  TEST_SUMMARY();
}
