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

/* Re-apply the current XRandR layout to live windows:
 *   - output windows that moved/resized follow the new rect; outputs that
 *     vanished are removed.
 *   - -g windows follow by name: output names track the new rect, "Screen"
 *     re-fits the full-screen bbox, "grp_*" stay put, stale names are removed.
 * New outputs never gain windows here. Saves state. Returns 0 or -1. */
int xw_app_reconfigure(XWApp *app, XWError *err);

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

/* Platform-independent snapshot of window i for --list/state/IPC.
 * String fields point into the window (valid until windows change).
 * Returns 0 on success, -1 when i is out of range. */
typedef struct XWWindowInfo {
  int type;             /* XW_BACKEND_* */
  const char *output;   /* output name, "" if geometry-only */
  const char *path;
  int rotation;
  int x, y;
  unsigned int w, h;
  /* backend flags (mirrors XWWlWindow/XWWindow; persisted to state) */
  int mute;
  double volume;
  double fps;
  int scale_mode;
  double opacity;
  int below, sticky, skip_taskbar, skip_pager, no_focus;
  const char *keybinds;
  const char *audio_client_name;
} XWWindowInfo;
int xw_app_window_info(XWApp *app, int i, XWWindowInfo *info);

/* The daemon's X display (for event polling). NULL on Wayland. */
Display *xw_app_display(XWApp *app);

/* ------------------------------------------------------------------ *
 *  Wayland (wlr-layer-shell) platform
 * ------------------------------------------------------------------ */

/* Whether the app runs on Wayland (1) or X11 (0). */
int xw_app_use_wayland(XWApp *app);

/* Wayland compositor fd for the daemon poll loop (-1 on X11). */
int xw_app_wayland_fd(XWApp *app);

/* Dispatch pending Wayland events (no-op on X11). */
void xw_app_wayland_dispatch(XWApp *app);

/* Access window i as a Wayland window (NULL on X11 / out of range). */
XWWlWindow *xw_app_wl_window(XWApp *app, int i);

/* Whether any live backend presents frames continuously (has a set_paused
 * hook, i.e. video). The daemon event loop must keep a periodic DPMS poll
 * while this is true so a screen-off pauses the renderer. */
int xw_app_dpms_watch_required(XWApp *app);

/* Whether any live backend wants a periodic frame tick (has a tick hook,
 * i.e. scene). The daemon loop clamps its poll timeout so animation is
 * never starved; the backend itself throttles to its fps. */
int xw_app_scene_tick_required(XWApp *app);

/* Call every live backend's tick hook once (NULL-safe). Call once per
 * main-loop iteration, after dpms_poll. */
void xw_app_tick_backends(XWApp *app);

/* Route an X event to the owning window: pointer forwarding + backend
 * on_event hook. Returns 1 if consumed. */
int xw_app_handle_event(XWApp *app, XEvent *ev);

/* List user properties of the window addressed by cfg (primary or -m):
 * writes "name\ttype\tvalue\n" lines. Returns the count, -1 on error
 * (unknown window, backend without properties). */
int xw_app_list_props(XWApp *app, const XWConfig *cfg, char *out, size_t n,
                      XWError *err);

/* Override a user property on the live window addressed by cfg. */
int xw_app_set_prop(XWApp *app, const XWConfig *cfg, const char *name,
                    const char *value, XWError *err);

/* Drop all user-property overrides on the window addressed by cfg. */
int xw_app_clear_props(XWApp *app, const XWConfig *cfg, XWError *err);

/* Runtime control of the live window addressed by cfg (volume/mute/pause/
 * seek/rotate). XW_TARGET_ALL applies to every window and succeeds when at
 * least one backend accepted the action. */
int xw_app_control(XWApp *app, const XWConfig *cfg, const char *name,
                   const char *value, XWError *err);

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
