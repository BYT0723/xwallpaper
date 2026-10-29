#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "../src/we_audio.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define N 1024
#define RATE 44100.0
static float L[N], R[N];

/* FFT bin m sits at m * RATE / N Hz; band b reads bin 2b. */
static double bin_freq(int m) { return m * RATE / N; }

static void gen_sine(float *b, int n, double freq, double rate, double amp) {
  for (int i = 0; i < n; i++)
    b[i] = (float)(amp * sin(2.0 * M_PI * freq * i / rate));
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

static void test_linear_band_peak(void) {
  int i;
  TEST_BEGIN("bin-centered sine lights exactly its upstream band");
  /* bin 4 -> band 2, amp 0.9: 0.35*log10((0.9*512)^2)*g(2) ~= 0.751 */
  gen_sine(L, N, bin_freq(4), RATE, 0.9);
  gen_sine(R, N, bin_freq(4), RATE, 0.9);
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  TEST_CHECK(out[2] > 0.70 && out[2] < 0.80);
  TEST_CHECK(out[64 + 2] > 0.70 && out[64 + 2] < 0.80);
  for (i = 0; i < 64; i++) {
    if (i == 2)
      continue;
    TEST_CHECK(out[i] < 0.05);
    TEST_CHECK(out[64 + i] < 0.05);
  }
}

static void test_stereo_separation(void) {
  TEST_BEGIN("left-only signal stays mostly left");
  gen_sine(L, N, bin_freq(4), RATE, 0.9);
  memset(R, 0, sizeof(R));
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  TEST_CHECK(out[2] > 0.5);
  for (int i = 64; i < 128; i++)
    TEST_CHECK(out[i] == 0.0f);
}

static void test_fullscale_clamped(void) {
  TEST_BEGIN("full-scale sine clamps to 0..1");
  gen_sine(L, N, bin_freq(4), RATE, 1.0);
  gen_sine(R, N, bin_freq(100), RATE, 1.0);
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  for (int i = 0; i < 128; i++)
    TEST_CHECK(out[i] >= 0.0f && out[i] <= 1.0f);
}

static void test_quiet_tones_hit_upstream_bands(void) {
  TEST_BEGIN("quiet tones land on their upstream bands, nowhere else");
  /* 4 low-level sines on exact FFT bins: no window leakage, so only the
   * four mapped bands may read high. Same signal shape the old log-scale
   * test used; the linear map keeps them visible at ~0.31..0.81. */
  int bins[] = {2, 10, 30, 60};
  int want[] = {1, 5, 15, 30};
  for (int i = 0; i < N; i++) {
    double s = 0;
    for (int k = 0; k < 4; k++)
      s += 0.03 * sin(2.0 * M_PI * bin_freq(bins[k]) * i / RATE);
    L[i] = R[i] = (float)s;
  }
  float out[128];
  TEST_CHECK_EQ_INT(xw_we_audio_map(L, R, N, out), 0);
  for (int k = 0; k < 4; k++) {
    TEST_CHECK(out[want[k]] > 0.25);
    TEST_CHECK(out[64 + want[k]] > 0.25);
  }
  int hot = 0;
  for (int i = 0; i < 64; i++)
    if (out[i] > 0.05)
      hot++;
  TEST_CHECK_EQ_INT(hot, 4);
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

static void test_spectrum_arrays(void) {
  XWWeSpectrum s;
  float in[128];
  int i;
  TEST_BEGIN("128 bins split into the six engine uniform arrays");
  /* the corpus shaders (workshop/2084198056 effects, audio_ring) read all six
   * g_AudioSpectrum{16,32,64}{Left,Right} arrays */
  memset(in, 0, sizeof(in));
  xw_we_audio_spectrum(in, &s);
  for (i = 0; i < 64; i++) {
    TEST_CHECK(s.left64[i] == 0.0f);
    TEST_CHECK(s.right64[i] == 0.0f);
  }
  for (i = 0; i < 32; i++) {
    TEST_CHECK(s.left32[i] == 0.0f);
    TEST_CHECK(s.right32[i] == 0.0f);
  }
  for (i = 0; i < 16; i++) {
    TEST_CHECK(s.left16[i] == 0.0f);
    TEST_CHECK(s.right16[i] == 0.0f);
  }
  /* bins 0-63 are the left channel and 64-127 the right, so a ramp in the
   * left half must not leak into the right arrays. The 64-band arrays are
   * a straight passthrough; the 32/16-band decimation (odd source bands
   * 2k+1 / 4k+3, each with its own gain curve) is pinned down by the
   * per-resolution test below. */
  for (i = 0; i < 128; i++)
    in[i] = (float)i;
  xw_we_audio_spectrum(in, &s);
  for (i = 0; i < 64; i++) {
    TEST_CHECK(s.left64[i] > (float)i - 0.001f && s.left64[i] < (float)i + 0.001f);
    TEST_CHECK(s.right64[i] > (float)(i + 64) - 0.001f &&
               s.right64[i] < (float)(i + 64) + 0.001f);
  }
}

static void test_spectrum_per_resolution_gains(void) {
  XWWeSpectrum s;
  float in[128];
  int i;
  TEST_BEGIN("32/16-band arrays apply their own upstream gain curves");
  /* flat 0.5 in: upstream computes each resolution with its own
   * (2-e^((1-band/N)-0.5)) curve (N = 63/31/15), so the decimated arrays
   * must not just copy the 64-band value. Independently computed:
   * 32[0] ~= 0.4656, 32[31] = 0.5, 16[0] ~= 0.4104, 16[15] = 0.5. */
  for (i = 0; i < 128; i++)
    in[i] = 0.5f;
  xw_we_audio_spectrum(in, &s);
  for (i = 0; i < 64; i++) {
    TEST_CHECK(s.left64[i] > 0.499f && s.left64[i] < 0.501f);
    TEST_CHECK(s.right64[i] > 0.499f && s.right64[i] < 0.501f);
  }
  TEST_CHECK(s.left32[0] > 0.462f && s.left32[0] < 0.469f);
  TEST_CHECK(s.right32[0] > 0.462f && s.right32[0] < 0.469f);
  TEST_CHECK(s.left32[31] > 0.499f && s.left32[31] < 0.501f);
  TEST_CHECK(s.left16[0] > 0.407f && s.left16[0] < 0.414f);
  TEST_CHECK(s.right16[0] > 0.407f && s.right16[0] < 0.414f);
  TEST_CHECK(s.left16[15] > 0.499f && s.left16[15] < 0.501f);

  /* decimation picks the odd source bands (last-write-wins upstream):
   * a spike in band 2k+1 surfaces in 32[k] only, 4k+3 in 16[k] only. */
  for (i = 0; i < 128; i++)
    in[i] = 0.0f;
  in[2 * 7 + 1] = 0.5f;      /* left band 15 -> left32[7] ~= 0.489 */
  in[64 + 4 * 5 + 3] = 0.5f; /* right band 23 -> right16[5] ~= 0.478 */
  xw_we_audio_spectrum(in, &s);
  TEST_CHECK(s.left32[7] > 0.48f && s.left32[7] < 0.50f);
  TEST_CHECK(s.right16[5] > 0.47f && s.right16[5] < 0.49f);
  /* left band 15 also feeds left16[3]; right band 23 also feeds right32[11] */
  TEST_CHECK(s.left16[3] > 0.0f);
  TEST_CHECK(s.right32[11] > 0.0f);
  for (i = 0; i < 32; i++) {
    if (i != 7)
      TEST_CHECK(s.left32[i] == 0.0f);
    if (i != 11)
      TEST_CHECK(s.right32[i] == 0.0f);
  }
  for (i = 0; i < 16; i++) {
    if (i != 3)
      TEST_CHECK(s.left16[i] == 0.0f);
    if (i != 5)
      TEST_CHECK(s.right16[i] == 0.0f);
  }
}

static void test_spectrum_bad_args(void) {
  XWWeSpectrum s;
  TEST_BEGIN("spectrum split rejects null args");
  float in[128];
  memset(in, 0, sizeof(in));
  TEST_CHECK_EQ_INT(xw_we_audio_spectrum(NULL, &s), -1);
  TEST_CHECK(s.left64[0] == 0.0f);
  TEST_CHECK(s.right16[0] == 0.0f);
  TEST_CHECK_EQ_INT(xw_we_audio_spectrum(in, NULL), -1);
}

int main(void) {
  TEST_SUITE("we_audio");
  test_silence_is_zeros();
  test_linear_band_peak();
  test_stereo_separation();
  test_fullscale_clamped();
  test_quiet_tones_hit_upstream_bands();
  test_format_128();
  test_bad_args();
  test_spectrum_arrays();
  test_spectrum_per_resolution_gains();
  test_spectrum_bad_args();
  TEST_SUMMARY();
}
