#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "we_audio.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WE_FFT_MAX 4096

static int is_pow2(int n) { return n >= 256 && (n & (n - 1)) == 0; }

/* In-place radix-2 FFT on interleaved-free re/im arrays. */
static void fft(double *re, double *im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i];
      re[i] = re[j];
      re[j] = t;
      t = im[i];
      im[i] = im[j];
      im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double ang = -2.0 * M_PI / len;
    double wr = cos(ang), wi = sin(ang);
    for (int i = 0; i < n; i += len) {
      double cr = 1.0, ci = 0.0;
      for (int k = 0; k < len / 2; k++) {
        double ur = re[i + k], ui = im[i + k];
        double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr;
        im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr;
        im[i + k + len / 2] = ui - vi;
        double nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
}

static double we_gain(double band, double nbands) {
  return 2.0 - exp((1.0 - band / nbands) - 0.5);
}

static void channel_bins(const float *in, int n, float *bands) {
  static double re[WE_FFT_MAX], im[WE_FFT_MAX];
  for (int i = 0; i < n; i++) {
    re[i] = in[i];
    im[i] = 0.0;
  }
  fft(re, im, n);

  /* power of bins 0..n/2 (bin 0 is DC, read by band 0 upstream too) */
  static double pw[WE_FFT_MAX / 2 + 1];
  int half = n / 2;
  for (int k = 0; k <= half; k++)
    pw[k] = re[k] * re[k] + im[k] * im[k];

  /* linear band map straight from linux-wallpaperengine's
   * PulseAudioPlaybackRecorder: band b reads FFT bin 2b, level
   * 0.35*log10(power) with the (2-e^...) per-band gain, capped above
   * at 1.0. Upstream leaves quiet bands negative (renders as zero-height
   * in GLSL); here the same array also feeds JS audio listeners, where
   * negatives would poison page-side averaging, so clamp below at 0 to
   * keep the 0..1 contract. */
  for (int b = 0; b < 64; b++) {
    int k = b * 2;
    if (k > half)
      k = half;
    double v = 0.0;
    if (pw[k] > 0.0)
      v = 0.35 * log10(pw[k]) * we_gain((double)b, 63.0);
    if (v < 0.0)
      v = 0.0;
    if (v > 1.0)
      v = 1.0;
    bands[b] = (float)v;
  }
}

int xw_we_audio_map(const float *l, const float *r, int n, float out[128]) {
  if (!l || !r || !out || !is_pow2(n) || n > WE_FFT_MAX)
    return -1;
  channel_bins(l, n, out);
  channel_bins(r, n, out + 64);
  return 0;
}

/* Re-apply the destination resolution's gain: `v64` carries the 64-band
 * gain, so divide it out, then apply the 32/16-band curve. Exact unless
 * v64 hit the 1.0 cap (raw is then a lower bound). */
static float regained(float v64, int src, int dst, double ndst) {
  double raw = (double)v64 / we_gain((double)src, 63.0);
  double v = raw * we_gain((double)dst, ndst);
  if (v > 1.0)
    v = 1.0;
  return (float)v;
}

int xw_we_audio_spectrum(const float in[XW_WE_AUDIO_BINS], XWWeSpectrum *out) {
  int i;
  if (!out)
    return -1;
  memset(out, 0, sizeof(*out));
  if (!in)
    return -1;
  for (i = 0; i < 64; i++) {
    out->left64[i] = in[i];
    out->right64[i] = in[XW_WE_AUDIO_BINS / 2 + i];
  }
  for (i = 0; i < 32; i++) {
    out->left32[i] = regained(out->left64[2 * i + 1], 2 * i + 1, i, 31.0);
    out->right32[i] = regained(out->right64[2 * i + 1], 2 * i + 1, i, 31.0);
  }
  for (i = 0; i < 16; i++) {
    out->left16[i] = regained(out->left64[4 * i + 3], 4 * i + 3, i, 15.0);
    out->right16[i] = regained(out->right64[4 * i + 3], 4 * i + 3, i, 15.0);
  }
  return 0;
}

int xw_we_audio_format(const float in[128], char *out, size_t n) {
  if (!in || !out || n == 0)
    return -1;
  size_t off = 0;
  for (int i = 0; i < 128; i++) {
    int w = snprintf(out + off, n - off, "%s%.4f", i ? "," : "[", (double)in[i]);
    if (w < 0 || (size_t)w >= n - off)
      return -1;
    off += (size_t)w;
  }
  if (off + 2 > n)
    return -1;
  out[off++] = ']';
  out[off] = '\0';
  return 0;
}
