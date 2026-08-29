# xwallpaper

> Lightweight X11 wallpaper daemon for image / video / web wallpapers — libmpv, webkit2gtk, and imlib2 backends with a long-running resident daemon and a thin CLI.

`xwallpaper` sets **image, video, and live web wallpapers** on bare X11 (i3, dwm, openbox, bspwm, …). It replaces the classic `xwinwrap + external player` workflow with embedded backends — no forked children, one desktop-type wrapper window per wallpaper, managed by a resident daemon you control with a thin CLI. Two binaries: `xwallpaperd` (the daemon, links the heavy backends) and `xwallpaper` (the CLI, links only libc so every invocation starts in <1ms).

```sh
xwallpaperd &                          # start the daemon once (stays resident)
xwallpaper set ~/wall.png              # image wallpaper (primary output)
xwallpaper set --video ~/clip.mp4 --mute # looping muted video wallpaper
xwallpaper set --web ~/page.html       # live web wallpaper
xwallpaper set -m HDMI-1 ~/portrait.mp4 --rotate 90   # per-output target
xwallpaper --list                      # what's running
xwallpaper clear                       # remove wallpapers (daemon stays)
xwallpaper exit                        # shut the daemon down
```

## Why

The previous setup — `xwinwrap` spawning a separate player per wallpaper — had two problems this project solves:

- **No fork+exec.** `mpv`, `webkit2gtk`, and `imlib2` are linked directly. One process owns every wallpaper window, so replacing a wallpaper swaps it in place with no cross-process kill/race.
- **Resident daemon.** A long-running `xwallpaperd` daemon owns all windows; the thin CLI client talks to it over a UNIX socket. `clear` keeps the daemon alive so the next `set` is instant. The CLI never spawns the daemon — start `xwallpaperd` yourself (from your session startup, a systemd user service, or a script).

Desktop icons and conky stay clickable: pointer events on the wallpaper window are forwarded to the root window.

## Features

| | |
|---|---|
| **Image** | imlib2 load/scale/tile — `fill`, `contain`, `stretch`, `tile` |
| **Video** | libmpv embedded (hardware decode, loop, mute/volume, static rotation, forced frame rate) |
| **Web** | webkit2gtk live page — one shared engine across screens |
| **Targets** | primary output, `-m <output>` by xrandr name, `--all`, or an explicit `-g WxH+X+Y --name` rectangle |
| **Video extras** | `--rotate <0\|90\|180\|270>`, `--keybinds <input.conf>`, `--fps <n>`, `--mute --volume` |
| **Window opts** | opacity, below, sticky, skip-taskbar, skip-pager, no-focus |
| **Desktop layer** | `_NET_WM_WINDOW_TYPE_DESKTOP` attach, sticky `_NET_WM_DESKTOP`, override-redirect |

## Install

Dependencies per distro (X11, `libx11`/`libxrandr`/`libxrender` and a C toolchain are assumed present):

**Arch**

```sh
sudo pacman -S --needed base-devel imlib2 mpv gtk3 webkit2gtk-4.1
```

> `libmpv` is provided by the `mpv` package on Arch (there is no separate package).

**Debian / Ubuntu**

```sh
sudo apt install build-essential pkg-config \
  libimlib2-dev libmpv-dev libgtk-3-dev libwebkit2gtk-4.1-dev
```

> `libmpv-dev` is a standalone package and does not pull in the `mpv` player binary.
> If the build reports missing X11 headers, also install `libx11-dev libxrandr-dev libxrender-dev`.

Optional, for `make test-xvfb`: `xorg-server-xvfb` (Arch) / `xvfb` (Debian/Ubuntu).

Build and install:

```sh
make            # produces ./xwallpaper
make test       # unit tests (no X required)
make test-xvfb  # integration tests under Xvfb (if available)
sudo make install            # PREFIX=/usr/local by default
sudo make install PREFIX=/usr DESTDIR=...  # override locations
```

## Usage

Start the daemon once (it stays resident until `xwallpaper exit`):

```sh
xwallpaperd &
```

Then control it from the CLI:

```
xwallpaper set [--image|--video|--web] [target] [opts] <path>
xwallpaper clear [-m <name|output>]
xwallpaper --list
xwallpaper exit
```

If the daemon is not running, the CLI errors with `daemon not running, start xwallpaperd first`. The backend is chosen by file extension unless forced with `--image`/`--video`/`--web`.

Target selection:

- `(default)` — primary output
- `-m <output>` — one xrandr output by name (`DP-1`, `HDMI-1`, …)
- `--all` — every output
- `-g WxH+X+Y --name <n>` — an explicit rectangle window, name required

Video options:

- `--rotate <0|90|180|270>` — static rotation, e.g. portrait clips on landscape screens
- `--keybinds <file>` — an mpv `input.conf`-style file for runtime control (pause, cycle rotation, …)
- `--fps <number>` — re-sample playback to this frame rate (drops frames; keeps speed; lower = less CPU/GPU)
- `--mute` / `--volume <0-100>` — audio control

See `xwallpaper --help` for the full list.

## How it works

- **Two binaries, thin CLI.** `xwallpaperd` owns all wallpaper windows and links the heavy backends; `xwallpaper` (the CLI) links only libc and talks to the daemon over a UNIX socket at `$XDG_RUNTIME_DIR/xwallpaper.sock`. The daemon must be started separately; the CLI never spawns it, so every CLI invocation is instant.
- **Desktop wrapper window.** Each wallpaper is an `override_redirect` window attached under the `_NET_WM_WINDOW_TYPE_DESKTOP` layer, lowered, made sticky and below — it behaves as a proper desktop backdrop.
- **Single event loop.** `app` runs one `poll()` over the X connection fd and the GTK main-context fds; mpv self-renders via `--wid` on its own thread.
- **Backend interface.** `image`/`video`/`web` implement a common `XWBackend` (`init/start/stop/destroy`); the app swaps backends per window in place.

```
xwallpaper (cli) ──→ client ──→ daemon (xwallpaperd) ──→ app ──→ x11
                                     ↑
                 image / video / web ─┘
```

## Testing

```sh
make test               # unit: cli parsing, geometry, protocol encode/decode
make test-xvfb          # integration: X11 window/desktop-layer, backend lifecycle under Xvfb
make check-client-libs  # regression: CLI must not link the heavy backend libs
```

Pure-logic modules are tested without an X server; integration tests run under `xvfb-run` (skipped when absent). No external test framework — plain C assert harness.

## License

MIT — see [LICENSE](LICENSE).

## Related

- [xwinwrap](https://github.com/ikoinoba/xwinwrap) — the original wrapper this project replaces.
