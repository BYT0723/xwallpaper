#ifndef XW_WEB_H
#define XW_WEB_H

/* WE audio fan-out registry (web.c owns it, main thread only).
 * Pages opt in via the document-start shim's wallpaperRegisterAudioListener;
 * the daemon tick calls push() at ~20Hz and want() for refcounting. */
void xw_web_we_push(const float bins[128]);
int xw_web_we_want(void);

/* ~20Hz fan-out tick (50ms internally throttled, monotonic clock).
 * Starts Pulse capture on first want, stops it when nobody wants audio,
 * pushes the newest bins (zeros when unavailable). Call once per main-loop
 * iteration from the GTK/main thread; never from the capture thread. */
void xw_web_we_tick(void);

#endif /* XW_WEB_H */
