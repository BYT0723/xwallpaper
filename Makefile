CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -D_GNU_SOURCE
LDFLAGS ?=

# Parallel by default: a from-scratch make compiles ~30 objects, so a
# single-threaded make leaves every other core idle. An explicit -jN
# (command line or environment) still wins.
ifeq ($(filter -j%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 2)
endif

# `make clean install` (any clean+build combo) races under -j: make runs
# command-line goals concurrently, so clean's rm can unlink freshly linked
# binaries while install stats them (ENOENT on a different binary each run;
# the retry then succeeds because everything rebuilds from scratch).
# Serialize the whole run when clean is combined with other goals: goals
# then execute in command-line order (clean first, then a genuine rebuild).
ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(words $(MAKECMDGOALS)),1)
.NOTPARALLEL:
endif
endif

# install locations (override: PREFIX= PREFIX=/usr, or DESTDIR=... for staging)
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=

PKGS := x11 xrandr xrender xext imlib2 mpv gtk+-3.0 webkit2gtk-4.1
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  := $(shell pkg-config --libs $(PKGS))

# thin client: interacts over the socket, must never link the heavy backend
# libs (webkit/mpv/imlib2) or every invocation pays their load time.
CLI_SRC := src/main.c src/cli.c src/proto.c src/client.c src/web_dir.c
CLI_OBJ := $(CLI_SRC:.c=.o)

# daemon: owns all wallpaper windows, links the full backend stack.
DAEMON_SRC := src/dmain.c src/daemon.c src/state.c src/cli.c src/proto.c \
              src/x11.c src/app.c src/image.c src/video.c src/web.c \
              src/web_dir.c src/web_http.c src/web_props.c src/props_store.c src/wl.c src/we_audio.c src/we_pulse.c \
              src/keybinds.c src/osd.c
DAEMON_OBJ := $(DAEMON_SRC:.c=.o)

# ---- Web user properties (json-c): daemon-only ----
# json-c persists web wallpaper user properties (sidecar files) and parses
# project.json entry points. The thin CLI never sees these flags or libs
# (check-client-libs guards that).
JSONC_PKGS := json-c
HAVE_JSONC := $(shell pkg-config --exists $(JSONC_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_JSONC),1)
JSONC_CFLAGS := $(shell pkg-config --cflags $(JSONC_PKGS))
JSONC_LIBS := $(shell pkg-config --libs $(JSONC_PKGS))
else
JSONC_CFLAGS :=
JSONC_LIBS :=
$(info xwallpaper: web user properties OFF (need: $(JSONC_PKGS) dev package))
endif

# ---- PulseAudio monitor capture (WE audio listener): daemon-only ----
# libpulse works via pipewire-pulse with no new daemon process.
# The thin CLI never sees these flags or libs (check-client-libs guards that).
PULSE_PKGS := libpulse
HAVE_PULSE := $(shell pkg-config --exists $(PULSE_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_PULSE),1)
PULSE_CFLAGS := $(shell pkg-config --cflags $(PULSE_PKGS)) -DXW_HAVE_PULSE=1
PULSE_LIBS := $(shell pkg-config --libs $(PULSE_PKGS))
else
PULSE_CFLAGS :=
PULSE_LIBS :=
$(info xwallpaper: PulseAudio capture OFF (need: $(PULSE_PKGS) dev package))
endif

WAYLAND_SCANNER ?= wayland-scanner
WLR_XML := src/proto/wlr-layer-shell-unstable-v1.xml
WL_GEN_H := src/proto/wlr-layer-shell-unstable-v1-client-protocol.h
WL_GEN_C := src/proto/wlr-layer-shell-unstable-v1-protocol.c
# layer-shell signatures reference xdg-shell types (xdg_popup_interface),
# so its bindings must be generated + linked as well.
XDG_XML := $(shell pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml
XDG_GEN_H := src/proto/xdg-shell-client-protocol.h
XDG_GEN_C := src/proto/xdg-shell-protocol.c
# output names + logical geometry come from xdg-output (not wl_output core).
XDGOUT_XML := $(shell pkg-config --variable=pkgdatadir wayland-protocols)/unstable/xdg-output/xdg-output-unstable-v1.xml
XDGOUT_GEN_H := src/proto/xdg-output-unstable-v1-client-protocol.h
XDGOUT_GEN_C := src/proto/xdg-output-unstable-v1-protocol.c

# ---- Wayland layer-shell support: auto-detected, daemon-only ----
# ON only when the wayland dev libs exist AND proto-wayland was generated.
# Otherwise the daemon builds X11-only and prints why. The thin CLI never
# sees these flags or libs (check-client-libs guards that).
# NOTE: this block must stay above the xwallpaperd/integration rules:
# make expands prerequisites at parse time, so appending to DAEMON_OBJ
# after those rules would update the link line but not the prereq list.
WL_PKGS := wayland-client wayland-egl egl
WL_PROTO_OBJ := $(WL_GEN_C:.c=.o) $(XDG_GEN_C:.c=.o) $(XDGOUT_GEN_C:.c=.o)
HAVE_WAYLAND := $(shell pkg-config --exists $(WL_PKGS) && echo 1 || echo 0)
HAVE_WL_PROTO := $(shell test -f $(WL_GEN_C) -a -f $(XDG_GEN_C) -a -f $(XDGOUT_GEN_C) && echo 1 || echo 0)
HAVE_GTK_LAYER := $(shell pkg-config --exists gtk-layer-shell-0 && echo 1 || echo 0)

ifeq ($(HAVE_WAYLAND)$(HAVE_WL_PROTO),11)
XW_WAYLAND := 1
else
XW_WAYLAND := 0
endif

ifeq ($(XW_WAYLAND),1)
WL_CFLAGS := $(shell pkg-config --cflags $(WL_PKGS)) -DXW_HAVE_WAYLAND=1
WL_LIBS := $(shell pkg-config --libs $(WL_PKGS))
ifeq ($(HAVE_GTK_LAYER),1)
WL_CFLAGS += $(shell pkg-config --cflags gtk-layer-shell-0) -DXW_HAVE_GTK_LAYER_SHELL=1
WL_LIBS += $(shell pkg-config --libs gtk-layer-shell-0)
endif
DAEMON_OBJ += $(WL_PROTO_OBJ)
$(info xwallpaper: Wayland layer-shell support ON$(if $(filter 0,$(HAVE_GTK_LAYER)), -- WITHOUT gtk-layer-shell AND web-on-wayland IS disabled))
else
WL_CFLAGS :=
WL_LIBS :=
$(info xwallpaper: Wayland layer-shell support OFF (need: $(WL_PKGS) libs + 'make proto-wayland'))
endif

# wayland flags reach daemon objects only (protocol obj included above).
$(DAEMON_OBJ): CFLAGS += $(WL_CFLAGS) $(PULSE_CFLAGS) $(JSONC_CFLAGS)

BINS := xwallpaper xwallpaperd

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(TEST_SRC:.c=)

INTEG_BIN := tests/integration/test_x11 tests/integration/test_image \
             tests/integration/test_video tests/integration/test_web \
             tests/integration/test_web_dpms \
             tests/integration/test_web_audio \
             tests/integration/test_app_geom tests/integration/test_app_all \
             tests/integration/test_fill tests/integration/test_restore \
             tests/integration/test_reconfigure

ifdef DEBUG
SANITIZE  := -fsanitize=address,undefined
CFLAGS    += -O0 -g3 $(SANITIZE) -fno-omit-frame-pointer
LDLIBS    += $(SANITIZE)
endif

.PHONY: all clean test test-xvfb test-wayland check-client-libs install uninstall proto-wayland

all: $(BINS)

xwallpaper: $(CLI_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(CLI_OBJ) $(SANITIZE)

xwallpaperd: $(DAEMON_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(DAEMON_OBJ) $(LDLIBS) $(WL_LIBS) $(PULSE_LIBS) $(JSONC_LIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# header deps (no auto-deps in this Makefile): without these, a struct
# layout change relinks stale objects against new ones (mixed ABI).
HEADER_DEPS := src/cli.h src/backend.h src/xw_err.h src/proto.h \
  src/client.h src/state.h src/x11.h src/app.h src/backends.h \
  src/wl.h src/daemon.h src/web_dir.h src/web_http.h src/web_props.h src/props_store.h src/web.h src/we_audio.h src/we_pulse.h \
  src/keybinds.h src/osd.h \
  $(wildcard src/proto/*-client-protocol.h)
$(CLI_OBJ) $(DAEMON_OBJ) \
  $(TEST_BIN) $(INTEG_BIN) $(WL_INTEG_BIN): $(HEADER_DEPS)

# thin unit tests: only the client-side objects, no heavy libs
tests/test_cli tests/test_geom: tests/%: tests/%.c src/cli.o src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/cli.o src/web_dir.o

tests/test_client: tests/test_client.c src/cli.o src/proto.o src/client.o src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/cli.o src/proto.o src/client.o src/web_dir.o

tests/test_state: tests/test_state.c src/state.o src/cli.o src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/state.o src/cli.o src/web_dir.o

tests/test_web_dir: tests/test_web_dir.c src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/web_dir.o

tests/test_web_http: tests/test_web_http.c src/web_http.o
	$(CC) $(CFLAGS) -o $@ $< src/web_http.o $(shell pkg-config --libs glib-2.0 gio-2.0)

tests/test_web_props: tests/test_web_props.c src/web_props.o
	$(CC) $(CFLAGS) $(JSONC_CFLAGS) -o $@ $< src/web_props.o $(JSONC_LIBS)

tests/test_props_store: tests/test_props_store.c src/props_store.o
	$(CC) $(CFLAGS) $(JSONC_CFLAGS) -o $@ $< src/props_store.o $(JSONC_LIBS) $(shell pkg-config --libs glib-2.0)

tests/test_we_audio: tests/test_we_audio.c src/we_audio.o
	$(CC) $(CFLAGS) -o $@ $< src/we_audio.o -lm

tests/test_keybinds: tests/test_keybinds.c src/keybinds.o
	$(CC) $(CFLAGS) -o $@ $< src/keybinds.o

tests/test_osd: tests/test_osd.c src/osd.o
	$(CC) $(CFLAGS) -o $@ $< src/osd.o

# test_wl compiles wl.c sources directly (without XW_HAVE_WAYLAND, so only
# the pure-logic part builds): keeps the unit test free of wayland headers
# and avoids sharing src/wl.o flag state with the daemon build.
tests/test_wl: tests/test_wl.c src/wl.c src/cli.c src/web_dir.c
	$(CC) $(CFLAGS) -o $@ tests/test_wl.c src/wl.c src/cli.c src/web_dir.c

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t =="; ./$$t || exit 1; done

tests/integration/%: tests/integration/%.c $(filter-out src/dmain.o,$(DAEMON_OBJ))
	$(CC) $(CFLAGS) $(WL_CFLAGS) $(PULSE_CFLAGS) $(JSONC_CFLAGS) -o $@ $< $(filter-out src/dmain.o,$(DAEMON_OBJ)) $(LDLIBS) $(WL_LIBS) $(PULSE_LIBS) $(JSONC_LIBS) -lm

test-xvfb: $(BINS) $(INTEG_BIN)
	@for t in $(INTEG_BIN); do \
	  echo "== $$t =="; \
	  xvfb-run -a -s "-screen 0 1920x1080x24" ./$$t || exit 1; \
	done

# wayland integration: needs a wlr-layer-shell compositor; the test itself
# prints SKIP + exits 0 when none is present (no wrapper needed).
WL_INTEG_BIN := tests/integration/test_wl_surface

test-wayland: $(BINS) $(WL_INTEG_BIN)
	@for t in $(WL_INTEG_BIN); do \
	  echo "== $$t =="; \
	  ./$$t || exit 1; \
	done

# explicit-call only: regenerates layer-shell client bindings from the
# vendored xml. Not part of the default build.
proto-wayland: $(WLR_XML)
	$(WAYLAND_SCANNER) client-header $< $(WL_GEN_H)
	$(WAYLAND_SCANNER) private-code $< $(WL_GEN_C)
	$(WAYLAND_SCANNER) client-header $(XDG_XML) $(XDG_GEN_H)
	$(WAYLAND_SCANNER) private-code $(XDG_XML) $(XDG_GEN_C)
	$(WAYLAND_SCANNER) client-header $(XDGOUT_XML) $(XDGOUT_GEN_H)
	$(WAYLAND_SCANNER) private-code $(XDGOUT_XML) $(XDGOUT_GEN_C)

check-client-libs: xwallpaper
	@! ldd ./xwallpaper | grep -qE 'webkit|mpv|Imlib2|javascriptcore|libpulse|libjson-c|libGL|GLEW|freetype|SDL2' \
	  && echo "OK: xwallpaper (cli) is free of heavy backend libs" \
	  || (echo "FAIL: cli links heavy backend libs"; exit 1)

install: $(BINS)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 xwallpaper $(DESTDIR)$(BINDIR)/xwallpaper
	install -m 755 xwallpaperd $(DESTDIR)$(BINDIR)/xwallpaperd

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/xwallpaper $(DESTDIR)$(BINDIR)/xwallpaperd

clean:
	rm -f $(CLI_OBJ) $(DAEMON_OBJ) $(BINS) $(TEST_BIN) $(INTEG_BIN)
