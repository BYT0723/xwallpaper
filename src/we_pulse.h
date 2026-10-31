#ifndef XW_WE_PULSE_H
#define XW_WE_PULSE_H

/* Process-wide monitor capture, refcounted. Thread-safe.
 * start() connects the default sink monitor in the background and
 * refcounts; stop() releases. Call start() only when !running() (one live
 * ref at a time); get() copies the newest 128 bins (zeros when unavailable)
 * and returns 0, or -1 on bad args.
 *
 * When the sink reports monitor.channel-volumes=true the monitor tap is
 * attenuated by the sink volume, so the captured chunk is scaled back up
 * (capped at +36 dB) before the spectrum is computed: the same audio must
 * drive the same visual response whatever output device and volume the
 * user picked. Sinks that tap before the volume are left untouched. */
int xw_we_pulse_start(void);
void xw_we_pulse_stop(void);
int xw_we_pulse_get(float out[128]);
int xw_we_pulse_running(void);

#endif /* XW_WE_PULSE_H */
