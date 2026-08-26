#ifndef XW_APP_H
#define XW_APP_H

#include <X11/Xlib.h>

#include "backend.h"
#include "cli.h"
#include "xw_err.h"

typedef struct XWApp XWApp;

/* Create the daemon's app context: opens the shared display, creates no
 * windows yet. Returns NULL with err set on failure. */
XWApp *xw_app_create(XWError *err);

/* Apply a `set` command:
 *   - TARGET_OUTPUT: find the window for that output and swap its backend in
 *     place, or create it if absent.
 *   - TARGET_GEOM:   replace any geometry-driven window at that rect.
 *   - TARGET_ALL:    tear down all windows and create one per connected output.
 * Returns 0 on success, -1 on error. */
int xw_app_set(XWApp *app, const XWConfig *cfg, XWError *err);

/* Apply a `clear` command: remove the given output's window (TARGET_OUTPUT)
 * or all windows (TARGET_ALL). Returns 0. */
int xw_app_clear(XWApp *app, const XWConfig *cfg, XWError *err);

/* Remove all windows. Returns 0. */
int xw_app_clear_all(XWApp *app);

/* Run the event loop until stop is requested. */
int xw_app_run(XWApp *app);

/* Request the loop to stop at the next iteration. */
void xw_app_stop(XWApp *app);

/* Whether stop was requested. */
int xw_app_stopped(XWApp *app);

/* Number of windows. */
int xw_app_window_count(XWApp *app);

/* Access window i (0..count-1). */
XWWindow *xw_app_window(XWApp *app, int i);

/* The daemon's X display (for event polling). */
Display *xw_app_display(XWApp *app);

/* Route an X event to the owning window: pointer forwarding + backend
 * on_event hook. Returns 1 if consumed. */
int xw_app_handle_event(XWApp *app, XEvent *ev);

/* Poll the X DPMS power state (throttled internally, ~500ms) and pause or
 * resume rendering backends when the display enters/exits a powered-off
 * state. Rendering backends keep presenting frames while DPMS is off, which
 * wakes the display back up; pausing them keeps it off. Safe to call from
 * any event loop iteration. */
void xw_app_dpms_poll(XWApp *app);

/* ------------------------------------------------------------------ *
 *  Persistent state (per-name latest wallpaper)
 * ------------------------------------------------------------------ */

/* Snapshot current windows + retained last entries to the state file.
 * The daemon calls this after every set/clear. Returns 0 or -1. */
int xw_app_save_state(XWApp *app);

/* Persist the window `name` as a kept (keep=1) entry so a later `restore`
 * can bring it back. Does not touch the live window. Used by `clear --keep`
 * (screen full-wallpaper clearing) to keep the last wallpaper of the cleared
 * monitor/group. Returns 0 or -1. */
int xw_app_keep(XWApp *app, const char *name);

/* Persist every live window as a kept entry (for `clear --keep --all`).
 * Returns 0 or -1. */
int xw_app_keep_all(XWApp *app);

/* Rebuild every non-kept entry from the state file as a live window.
 * Called once at daemon startup to restore the previous session's
 * wallpapers. Failures on individual entries are ignored. Returns 0. */
int xw_app_restore_startup(XWApp *app);

/* Number of non-kept state entries that are not yet live windows.
 * >0 after a failed startup restore (e.g. video backend init failed
 * because the GPU/GL stack wasn't ready yet); the daemon retries
 * xw_app_restore_startup until this reaches 0. */
int xw_app_restore_pending(XWApp *app);

/* Startup self-heal: call every backend's retry() hook. Video backends
 * whose mpv dropped its video track (Vulkan swapchain failed on first boot
 * before the GPU was ready) reload the file; returns how many were retried. */
int xw_app_retry_backends(XWApp *app);

/* Rebuild every kept entry as a live window (the `restore` command:
 * brings back last wallpapers after `clear --keep` hid them). Returns 0. */
int xw_app_restore_kept(XWApp *app);

/* Current app instance (for signal handlers). NULL before create / after destroy. */
XWApp *xw_app_get(void);

/* Tear down windows and close the display. */
void xw_app_destroy(XWApp *app);

#endif /* XW_APP_H */
