# xwallpaper

## Problem Statement

How might we provide a single lightweight CLI that sets image / video / web wallpapers on bare X11 (no desktop environment, or a WM without wallpaper support), without forking external players?

## Recommended Direction

A pure-C, X11-native wallpaper **daemon + client**. A resident daemon owns one `_NET_WM_TYPE_DESKTOP` wrapper window per output (absorbing the proven window logic from the author's existing `xwinwrap`), onto which three interchangeable backends mount: `image` (imlib2), `video` (libmpv embedded), `web` (webkit2gtk via GtkPlug). The CLI client talks to the daemon over a UNIX socket (`set`/`clear`/`list`/`exit`), auto-spawning it on first use. `set` swaps a window's backend in place — no cross-process kill coordination. A unified poll loop integrates X events and the GTK main context (when the web backend is active); video self-renders via libmpv's `--wid`. Mouse events are forwarded to the root window so desktop items (conky, icons) stay interactive.

## Key Assumptions to Validate

- [x] libmpv renders correctly onto an override-redirect X window (validated via `--wid`)
- [x] GtkPlug can embed webkit2gtk into a foreign `_NET_WM_TYPE_DESKTOP` X window
- [x] GTK main context can be driven from a custom poll loop (not `gtk_main()`) without breaking webkit2gtk
- [ ] webkit2gtk memory footprint (~150MB) is acceptable for a wallpaper (validated on real X)
- [x] imlib2 covers all image needs (formats, scaling, tiling) without extra deps

## MVP Scope

- `xwallpaper set <path>` auto-detects backend by extension
- `xwallpaper set --image/--video/--web <path>` explicit backend
- `xwallpaper set --all | -m <output>` target every screen or one output (default primary), `-g <WxH+X+Y>` geometry override
- `xwallpaper clear`, `xwallpaper --list`, `xwallpaper exit`
- xwinwrap parity: `-o/-b/-s/-st/-sp/-nf` flags
- video: `--mute/--volume`, loop playback
- Resident daemon, single-process multi-window (one window per output, shared webkit engine); client auto-spawns it

## Not Doing (and Why)

- Wayland — go through XWayland for now
- GUI / tray / preview — CLI only
- Video filters / per-wallpaper advanced config — delegate to libmpv defaults
- EXIF rotation / color management for images — imlib2 out-of-the-box behavior only
- `--argb` transparency — not needed; wallpaper program is opaque
- Wallpaper change scheduling/randomization — not in MVP

## Open Questions

- Socket path under `$XDG_RUNTIME_DIR/xwallpaper/` — resolved in spec
- Whether `--argb` transparency is needed for web wallpaper overlays in MVP — resolved: no
- libmpv rendering on a window vs. offscreen FBO then composite — resolved: window via `--wid`
