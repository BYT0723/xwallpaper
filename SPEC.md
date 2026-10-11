# Spec: xwallpaper

## Objective

A lightweight CLI tool that sets **image / video / web** wallpapers on bare X11. It replaces the author's previous `xwinwrap + external player` workflow with embedded backends — no forked children, one desktop-type wrapper window per wallpaper. Two binaries: `xwallpaperd` (resident daemon owning all windows) and a thin `xwallpaper` CLI that links only libc so every invocation starts in <1ms.

**User stories:**
- As an X11/WM user (i3, openbox, bspwm, …) I can `xwallpaper set photo.png` and get an image wallpaper.
- I can `xwallpaper set clip.mp4 --mute` and get a looping video wallpaper (libmpv).
- I can `xwallpaper set index.html` and get a live web wallpaper (webkit2gtk).
- I can target one output (`-m DP-1`) or an exact rectangle (`-g 1920x1080+0+0`).
- I can `xwallpaper clear` to remove running wallpapers and `xwallpaper --list` to see them.
- Desktop icons / conky stay clickable because pointer events are forwarded to the root window.

## Tech Stack

- C11, Xlib + XRender + XRandR, imlib2, libmpv, webkit2gtk-4.1 + GTK3
- Build: `make` + `pkg-config` (matching xwinwrap's build approach)
- Test: plain C assert-based harness (no external framework)

## Commands

```
Build:      make                 # xwallpaper (thin CLI) + xwallpaperd (daemon)
Run tests:  make test
Check deps: make check-client-libs   # CLI must not link webkit/mpv/imlib2
Clean:      make clean
Debug build: make DEBUG=1
Manual smoke:
  ./xwallpaperd &                    # start daemon once (stays resident)
  ./xwallpaper set tests/data/pixel.png
  ./xwallpaper set tests/data/bounce.mp4 --mute
  ./xwallpaper set tests/data/page.html
  ./xwallpaper --list
  ./xwallpaper clear
```

## Project Structure

```
src/
  main.c         → cli entry (xwallpaper): parse → encode → connect → print
  dmain.c        → daemon entry (xwallpaperd): xw_daemon_main
  daemon.c/.h    → socket server, command dispatch, daemon lifecycle
  proto.c/.h     → protocol encode + socket path, shared by cli and daemon
  client.c/.h    → socket connect/send/print (no daemon spawning)
  x11.c/.h       → XWWindow, window creation, atoms, desktop-layer, monitor, geometry, mouse forward
  cli.c/.h       → arg parse → XWConfig (pure, no X11)
  app.c/.h       → event loop (poll), backend lifecycle, XWBackend interface
  image.c        → imlib2 backend
  video.c        → libmpv backend
  web.c          → webkit2gtk backend (GtkPlug)
  backend.h      → XWBackend struct + XWWindow struct forward decl
tests/
  test_cli.c     → parsing, backend detection (links cli.o only)
  test_geom.c    → XParseGeometry-based monitor/geometry resolution (links cli.o only)
  test_client.c  → protocol encode/decode (tmp socket, fake daemon) (links cli/proto/client)
  harness.h      → assert macros + main runner
Makefile
CAPABILITY.md
docs/ideas/xwallpaper.md
```

## Code Style

```c
/* x11.c — one public struct, owned by x11 module */
XWWindow *xw_x11_create(XWConfig *cfg, XWError *err);
void      xw_x11_destroy(XWWindow *w);

/* backend.h — app codes only against this */
typedef struct XWBackend XWBackend;
struct XWBackend {
  const char *name;
  int  (*init)(XWWindow *win, XWBackendOpt *opt, XWError *err);
  int  (*start)(XWBackend *b);
  void (*stop)(XWBackend *b);
  void (*destroy)(XWBackend *b);
};
```

- Naming: `xw_<module>_<action>`; types `XW*`; module prefixes `xw_`.
- C11, no comments unless they explain a non-obvious X11 trick.
- Errors: `XWError` struct (`{int code; char msg[256];}`), functions return `int` (0 = ok).
- No global state except the single `Display *` owned by `x11` (kept module-static).

## Testing Strategy

- **Unit (no X required):** `cli`, `geom`, `client/protocol` — assert-based tests in `tests/`, built and run by `make test`.
- **Integration (Xvfb):** `make test-xvfb` runs `tests/integration/` against `xvfb-run`: window creation, desktop-layer attach, monitor enumeration, backend init without crash. Skipped if `xvfb-run` absent.
- **Manual (real X):** the smoke commands above on a live session.
- Coverage expectation: pure-logic modules ~90%; X11/backend init exercised by smoke + Xvfb (crash-level), full visual behavior is manual.

## Boundaries

- **Always:** run `make test` before landing; validate CLI input; free every X/backend resource on destroy; forward pointer events when overriding the desktop; keep the CLI free of the heavy backend libs (`make check-client-libs`).
- **Ask first:** adding a build dependency; changing the event-loop model; switching libmpv render from window to FBO; adding Wayland support; reintroducing daemon spawning from the CLI.
- **Never:** commit secrets; open a second X connection in a backend (must reuse `x11`'s); fork+exec external players; link webkit2gtk/mpv/imlib2 into the `xwallpaper` CLI binary (that cost is exactly why it was split out).

## Key design decisions (carried from xwinwrap + ideation)

1. **Desktop wrapper window**: `override_redirect` + attach under the `_NET_WM_WINDOW_TYPE_DESKTOP` layer (find via `__SWM_VROOT` fallback to root scan), `XLowerWindow`. Set `_NET_WM_WINDOW_TYPE_DESKTOP`, `_NET_WM_STATE_BELOW`, `_NET_WM_DESKTOP=0xFFFFFFFF` (sticky), skip taskbar/pager, `_NET_WM_WINDOW_OPACITY` when `-o`.
2. **Pointer forwarding**: Enter/Leave/Motion on the wallpaper window are re-sent to the root window so conky/desktop icons stay interactive.
3. **Video**: libmpv with `--wid=<win>` set via `mpv_set_property("wid", ...)`. mpv creates its own GL rendering context and renders on an internal thread — no `mpv_render_context` plumbing needed. We control loop/volume/mute via mpv commands and stay out of the render path. Video backend also needs a window with a GL-capable visual (default visual is fine, opaque wallpaper so no ARGB needed).
4. **Web**: webkit2gtk + GtkPlug embedding the foreign window XID. GDK opens its own X connection; `app` drives the GTK `GMainContext` from its own poll loop via `g_main_context_{prepare,query,check,dispatch}` (documented GLib pattern for embedding GLib in a foreign loop) — no `gtk_main()`. Each output window gets its own GtkPlug+WebView sharing one `WebKitWebContext`.
5. **Event loop** (`app`): single `poll()` over: X connection fd (from `x11`), GTK context fds (web). Video needs no loop integration (mpv self-renders). Backends register fd sources through `app`'s loop API.
6. **Two binaries, thin CLI + resident daemon**: a long-running `xwallpaperd` daemon owns all wallpaper windows (one per output, or arbitrary via `-g`). A thin CLI (`xwallpaper ...`) links only libc and talks to the daemon over a UNIX socket (`$XDG_RUNTIME_DIR/xwallpaper.sock`), so every invocation starts in <1ms. The CLI does **not** spawn the daemon — the user starts `xwallpaperd` (session startup, systemd user service, or a script); if it is not running the CLI errors with `daemon not running, start xwallpaperd first`.

   - **Command flow**: `set -m DP-1 <path>`, `set --all <path>`, `set -g ...` all become one socket message; the daemon resolves geometry and swaps the target window's backend in place — no cross-process kill coordination, so no replace race.
   - **Daemon lifecycle**: stays resident across `clear` (so later `set` is instant). `xwallpaper exit` tears it down explicitly. `clear`/`--list` connect only.
   - **Protocol**: line-based text over the socket (`set <output|all|geom...> <type> <path> [opts]`, `clear`, `list`, `exit`); daemon replies `ok` or `error <msg>`; `list` replies one line per window.
   - **WebKitWebContext is process-wide** — one engine shared by all webviews (saves ~150MB/screen vs multi-process).
   - Per-window state (mpv handle, webview, backend opt) lives in an `XWWindow` array owned by the daemon's `app`; backends are per-window instances.

## Success Criteria

- `xwallpaper set <png>` shows a correct image wallpaper on a live X session (scaled to monitor rect, desktop layer).
- `xwallpaper set <mp4> --mute` loops a muted video smoothly (no tearing, mouse still forwarded).
- `xwallpaper set <html>` renders the page as wallpaper and GTK loop coexists with X loop (no deadlock, page is live).
- `xwallpaper set --web <dir>` serves the directory (must contain `index.html`) as `file://` with file-access switches on for http-like subresource loading (no external server, no TCP port); plain local files are rejected with a hint. Media (`<video>`) uses the native file path (custom URI schemes are not supported by the WebKit media stack).
- `-m`/`-g` place the window on the right output/rectangle; unknown `-m` errors and lists monitors.
- `xwallpaper clear` removes wallpapers but the daemon stays alive; `xwallpaper exit` terminates it; `--list` shows windows.
- Replacing a monitor's wallpaper (`set -m DP-1` twice) swaps it in place without duplicate windows or stale processes.
- `make test` green; `make test-xvfb` green where xvfb available.
- No forks of external players anywhere in the codebase.

## Open Questions

- Exact geometry/resize semantics when `-g` and window manager constraints interact (i3 gaps etc.) — resolve during `x11` implementation.
- Whether `-i <output>` single-window addressing overlaps with `-m`; decide the canonical spelling during `cli` implementation (`-m` targets one window, `--all` targets all; `clear` applies to targeted set).
