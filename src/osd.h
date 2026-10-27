#ifndef XW_OSD_H
#define XW_OSD_H

#include <stddef.h>

#define XW_OSD_MS 1500
#define XW_OSD_MAX 64
/* Unified look: bottom-right, clear of bottom bars, white sans on a dark
 * translucent box, ~20px text, fading after XW_OSD_MS. Renderers cannot be
 * pixel identical, so this is the tier every backend matches: mpv osd
 * alignment/margins, web toast CSS and the scene GL raster all use it.
 * mpv pins its box to #A6000000 (the same 65% as the web/scene toast)
 * with the outline off, so all three boxes blend identically; mpv text
 * stays single-line because background-box padding follows the line
 * height. */
#define XW_OSD_TEXT_PX 20
#define XW_OSD_MARGIN_X_PX 16
#define XW_OSD_MARGIN_BOTTOM_PX 48

/* Format the one-line OSD text for a successful control action.
 * name is one of volume/mute/pause/seek/rotate/reload; value is the raw
 * control argument (seek seconds); cur is the resolved state (volume 0..100,
 * mute/pause want flag, rotate degrees). Returns 0 with out written, or -1
 * for unknown actions or bad arguments (caller shows no OSD). */
int xw_osd_format(const char *name, const char *value, int cur, char *out,
                   size_t n);

#endif /* XW_OSD_H */
