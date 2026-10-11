#ifndef XW_WE_AUDIO_H
#define XW_WE_AUDIO_H

#include <stddef.h>

#define XW_WE_AUDIO_BINS 128
#define XW_WE_AUDIO_RATE 48000

/* Map time-domain stereo samples to 128 WE-style bins
 * (0-63 left, 64-127 right, bass first). n must be a power of two >= 256.
 * Silence yields all zeros. Returns 0 on success, -1 on bad args. */
int xw_we_audio_map(const float *l, const float *r, int n, float out[128]);

/* Format 128 bins as a JSON array "[v,v,...]" for JS injection.
 * Returns 0 on success, -1 on bad args or truncation. */
int xw_we_audio_format(const float in[128], char *out, size_t n);

#endif /* XW_WE_AUDIO_H */
