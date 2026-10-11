#ifndef XW_WE_PULSE_H
#define XW_WE_PULSE_H

/* Process-wide monitor capture, refcounted. Thread-safe.
 * start() connects the default sink monitor in the background and
 * refcounts; stop() releases. Call start() only when !running() (one live
 * ref at a time); get() copies the newest 128 bins (zeros when unavailable)
 * and returns 0, or -1 on bad args. */
int xw_we_pulse_start(void);
void xw_we_pulse_stop(void);
int xw_we_pulse_get(float out[128]);
int xw_we_pulse_running(void);

#endif /* XW_WE_PULSE_H */
