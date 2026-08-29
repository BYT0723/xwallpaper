# AGENTS.md

## Architecture: two binaries (easy to break)

- `xwallpaper` = thin CLI (`src/main.c` + `cli.c` + `proto.c` + `client.c`), links **only libc**. It must NEVER link webkit2gtk/mpv/imlib2 — that cost is why it was split out. `make check-client-libs` enforces this; do not "fix" it away.
- `xwallpaperd` = daemon (`src/dmain.c` + everything else incl. `x11.c`/`app.c`/`image.c`/`video.c`/`web.c`/`state.c`), links all heavy backends, owns all wallpaper windows.
- The CLI does **not** spawn the daemon. The user starts `xwallpaperd` themselves; if absent the CLI errors `daemon not running, start xwallpaperd first`. Don't reintroduce auto-spawn.
- Backends (`image`/`video`/`web`), `x11`, `app`, `state` are daemon-only; changing them does not touch the CLI build.

## Build & test

```
make                 # xwallpaper + xwallpaperd
make test            # unit: tests/test_*.c (no X needed)
make test-xvfb       # integration: tests/integration/ (requires xvfb-run)
make check-client-libs  # regression: CLI must stay free of heavy libs
make DEBUG=1         # -O0 + -fsanitize=address,undefined
make install         # both binaries to $(BINDIR)
```

- No linter — `-Wall -Wextra` runs at compile time. Verify order: `make test` → `make test-xvfb` → `make check-client-libs`.
- Unit tests link **thin objects only** (see Makefile): `test_cli`/`test_geom` → `cli.o`; `test_client` → `cli.o proto.o client.o`; `test_state` → `state.o cli.o`. Integration tests link the daemon-side objects minus `dmain.o`. When adding a test, wire its link targets explicitly — the generic all-objects rule no longer exists.
- Integration needs X: run under `xvfb-run -a` (as the Makefile does) or a live session with `DISPLAY` set. `tests/data/` has fixtures (`red.png`, `bounce.mp4`, `page.html`).

## Conventions

- Protocol (socket path, `XW_MSG_MAX`/`XW_REPLY_MAX`, `xw_proto_encode_*`) lives in `src/proto.c/.h`, shared by CLI and daemon. It's line-based text; version skew is tolerated by "daemon rejects unknown commands". Keep it backward compatible.
- C11, names `xw_<module>_<action>` / types `XW*`; no comments unless they explain a non-obvious X11 trick.
- Manual smoke: `xwallpaperd &` first, then `xwallpaper set/--list/clear/exit`.
- `tasks/` is local planning docs and git-ignored (not committed). Feature specs go in `docs/specs/`, ideas in `docs/ideas/`.
