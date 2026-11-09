#ifndef XW_WEB_H
#define XW_WEB_H

#include "we_media.h"

/* WE audio fan-out registry (web.c owns it, main thread only).
 * Pages opt in via the document-start shim's wallpaperRegisterAudioListener;
 * the daemon tick calls push() at ~20Hz and want() for refcounting. */
void xw_web_we_push(const float bins[128]);
int xw_web_we_want(void);
/* JS evaluations issued by push() (monotonic). Tests assert redundant
 * spectra are skipped. */
int xw_web_we_eval_count(void);

/* ~20Hz fan-out tick (50ms internally throttled, monotonic clock).
 * Starts Pulse capture on first want, stops it when nobody wants audio,
 * pushes the newest bins (zeros when unavailable). Call once per main-loop
 * iteration from the GTK/main thread; never from the capture thread. */
void xw_web_we_tick(void);

/* WE media fan-out (web.c owns per-view want/seen bits; we_media owns the
 * shared MPRIS snapshot). Call the tick once per main-loop iteration. */
int xw_web_media_want(void);
void xw_web_media_push(const XWMediaInfo *snap);
void xw_web_media_tick(void);
int xw_web_media_eval_count(void);

/* Test hook: drop the per-wallpaper property override cache. */
void xw_web_cache_clear_for_tests(void);

#endif /* XW_WEB_H */

