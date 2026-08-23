# Capability Map: xwallpaper

| Module id | Responsibility | Depends on |
|---|---|---|
| `cli` | Arg parsing (`set/clear/list/exit`, `-m/-g/-o/-b/-s/...`), backend detection by extension | — |
| `client` | Connect to daemon socket; auto-spawn daemon (fork+setsid) if absent; encode command, print reply | `cli` |
| `daemon` | UNIX socket server, command dispatch (set/clear/list/exit), window swap logic, daemon lifecycle (resident across clear) | `client`, `app` |
| `x11` | Desktop wrapper window creation (`_NET_WM_TYPE_DESKTOP`, `_NET_WM_STATE_BELOW`, `_NET_WM_DESKTOP`, sticky/skip, override-redirect, `_NET_WM_WINDOW_OPACITY`), desktop-layer discovery (`__SWM_VROOT`, `_NET_WM_WINDOW_TYPE_DESKTOP`), xrandr monitor resolution, geometry application, mouse-event forwarding to root, input shape | — |
| `image` | imlib2 image load/scale/tile, render onto window via XRender/drawable | `x11` |
| `video` | libmpv embedded (init, load, loop, mute/volume via `wid`), `--wid` window attach | `x11` |
| `web` | webkit2gtk page render, GtkPlug embed into foreign X window, GTK main context integration | `x11`, `gtk` |
| `app` | Backend abstraction (`XWBackend` interface: `init/start/stop/destroy`), unified event loop (poll X fd + GTK ctx), lifecycle, backend switching per window, cleanup | `cli`, `x11`, `image`, `video`, `web` |

```
Dependency direction (no cycles):
  cli ──→ client ──→ daemon ──→ app ──→ x11
                                   ↑
        image ─────────────────────┘
        video ─────────────────────┘
        web   ─────────────────────┘
```

## Build order

```
cli → client → x11 → image → video → web → app → daemon → main
```

## Boundary contracts

- `image/video/web` each implement `XWBackend` from `app`; `app` codes only against the interface.
- `x11` is the sole owner of the X connection and `XWWindow` struct (Display, Window id, monitor rect, visual). Backends receive an `XWWindow*` and must not open their own display.
- `video` owns its libmpv handle; mpv self-renders via `--wid`, no render-context plumbing.
- `web` owns its GTK/WebKit state; it registers its GMainContext fds into `app`'s poll loop (no `gtk_main()`).
- `client` is pure socket I/O; it never opens an X display. Only the daemon touches X11.
- `daemon` never parses argv — it receives structured commands on the socket and dispatches to `app`. The text protocol is defined in `daemon.h` and is the only contract between `client` and `daemon`.
