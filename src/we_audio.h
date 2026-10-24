#ifndef XW_WE_AUDIO_H
#define XW_WE_AUDIO_H

#include <stddef.h>

#define XW_WE_AUDIO_BINS 128
#define XW_WE_AUDIO_RATE 48000

/* Map time-domain stereo samples to 128 WE-style bins
 * (0-63 left, 64-127 right, bass first). n must be a power of two >= 256.
 * Level per band follows linux-wallpaperengine (0.35*log10(power) with the
 * per-band gain), clamped 0..1. Silence yields all zeros.
 * Returns 0 on success, -1 on bad args. */
int xw_we_audio_map(const float *l, const float *r, int n, float out[128]);

/* Format 128 bins as a JSON array "[v,v,...]" for JS injection.
 * Returns 0 on success, -1 on bad args or truncation. */
int xw_we_audio_format(const float in[128], char *out, size_t n);

/* The six arrays the engine's g_AudioSpectrum{N}{Left,Right} shader uniforms
 * read; the corpus effects (workshop/2084198056, workshop/2504730727) use all
 * six. */
typedef struct {
  float left16[16], right16[16];
  float left32[32], right32[32];
  float left64[64], right64[64];
} XWWeSpectrum;

/* Split the 128-bin layout (0-63 left, 64-127 right) into the six uniform
 * arrays. The 32- and 16-band arrays decimate the 64-band data the way the
 * engine does - PulseAudioPlaybackRecorder writes destination[band >> 1] and
 * destination[band >> 2] in a plain loop, so the last write of each group
 * wins and the surviving source band is the odd one:
 * 32[k] = 64[2k+1] and 16[k] = 64[4k+3].
 *
 * The engine also applies its own per-resolution magnitude curve (band/63,
 * band/31, band/15); these arrays carry the 64-band curve for all three, so
 * the 32- and 16-band levels are an approximation. `out` is zeroed first, so
 * a NULL input leaves a silent spectrum rather than garbage.
 * Returns 0, or -1 on bad args. */
int xw_we_audio_spectrum(const float in[XW_WE_AUDIO_BINS], XWWeSpectrum *out);

#endif /* XW_WE_AUDIO_H */
