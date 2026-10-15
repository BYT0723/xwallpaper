CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -D_GNU_SOURCE
LDFLAGS ?=

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

# ---- Scene scripting (vendored QuickJS, MIT): daemon-only ----
# No system dependency: engine sources build straight in.
QJS_SRC := vendor/quickjs/quickjs.c vendor/quickjs/cutils.c \
           vendor/quickjs/libregexp.c vendor/quickjs/libunicode.c \
           vendor/quickjs/libbf.c
QJS_OBJ := $(QJS_SRC:.c=.o)
# vendor warnings are not ours; version macro comes from upstream's Makefile
$(QJS_OBJ): CFLAGS += -w -DCONFIG_VERSION='"2024-01-13"'
src/scene_script.o: CFLAGS += -Ivendor/quickjs

# daemon: owns all wallpaper windows, links the full backend stack.
DAEMON_SRC := src/dmain.c src/daemon.c src/state.c src/cli.c src/proto.c \
              src/x11.c src/app.c src/image.c src/video.c src/web.c \
              src/web_dir.c src/wl.c src/we_audio.c src/we_pulse.c \
              src/scene_pkg.c src/scene_tex.c src/scene_shader.c \
              src/scene_parse.c src/scene_core.c src/scene_gl.c \
              src/scene_mesh.c \
              src/scene_image.c src/scene_text_gl.c \
              src/scene_particle.c src/scene_particle_gl.c src/scene_text.c \
              src/scene_sound.c src/scene_glsl.c \
              src/scene_script.c src/scene.c
DAEMON_SRC += $(QJS_SRC)
DAEMON_OBJ := $(DAEMON_SRC:.c=.o)

# ---- Scene (WE scene wallpapers): daemon-only ----
# json-c parses project.json/scene.json; freetype rasterizes text objects;
# SDL2_mixer decodes sound objects (ogg loop). quickjs is vendored later.
# The thin CLI never sees these flags or libs (check-client-libs guards that).
SCENE_PKGS := json-c
HAVE_JSONC := $(shell pkg-config --exists $(SCENE_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_JSONC),1)
SCENE_CFLAGS := $(shell pkg-config --cflags $(SCENE_PKGS)) -DXW_HAVE_JSONC=1
SCENE_LIBS := $(shell pkg-config --libs $(SCENE_PKGS))
else
SCENE_CFLAGS :=
SCENE_LIBS :=
$(info xwallpaper: scene JSON parsing OFF (need: $(SCENE_PKGS) dev package))
endif

# ---- Scene GL rendering (desktop GL via GLX + GLEW): daemon-only ----
GL_PKGS := gl glew
HAVE_GL := $(shell pkg-config --exists $(GL_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_GL),1)
GL_CFLAGS := $(shell pkg-config --cflags $(GL_PKGS)) -DXW_HAVE_GL=1
GL_LIBS := $(shell pkg-config --libs $(GL_PKGS))
else
GL_CFLAGS :=
GL_LIBS :=
$(info xwallpaper: scene GL rendering OFF (need: $(GL_PKGS) dev packages))
endif

# ---- Scene text (FreeType glyph rasterization): daemon-only ----
FT_PKGS := freetype2
HAVE_FT := $(shell pkg-config --exists $(FT_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_FT),1)
FT_CFLAGS := $(shell pkg-config --cflags $(FT_PKGS)) -DXW_HAVE_FT=1
FT_LIBS := $(shell pkg-config --libs $(FT_PKGS))
else
FT_CFLAGS :=
FT_LIBS :=
$(info xwallpaper: scene text OFF (need: $(FT_PKGS) dev package))
endif

# ---- Scene sound (SDL2_mixer chunk playback): daemon-only ----
SND_PKGS := SDL2_mixer
HAVE_SND := $(shell pkg-config --exists $(SND_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_SND),1)
SND_CFLAGS := $(shell pkg-config --cflags $(SND_PKGS)) -DXW_HAVE_SDL_MIXER=1
SND_LIBS := $(shell pkg-config --libs $(SND_PKGS))
else
SND_CFLAGS :=
SND_LIBS :=
$(info xwallpaper: scene sound OFF (need: $(SND_PKGS) dev package))
endif

# ---- Scene textures (LZ4 mipmap decompression): daemon-only ----
LZ4_PKGS := liblz4
HAVE_LZ4 := $(shell pkg-config --exists $(LZ4_PKGS) && echo 1 || echo 0)
ifeq ($(HAVE_LZ4),1)
LZ4_CFLAGS := $(shell pkg-config --cflags $(LZ4_PKGS)) -DXW_HAVE_LZ4=1
LZ4_LIBS := $(shell pkg-config --libs $(LZ4_PKGS))
else
LZ4_CFLAGS :=
LZ4_LIBS :=
$(info xwallpaper: scene TEXV decode OFF (need: $(LZ4_PKGS) dev package))
endif

# ---- Scene shader pipeline (glslang + SPIRV-Cross): daemon-only ----
# Mirrors linux-wallpaperengine's GLSLContext::toGlsl: glslang parses the
# preprocessed GLSL, SPIRV-Cross re-emits it. Both are permissively licensed
# (BSD / Apache-2.0), unlike the GPL engine itself. Without them the renderer
# hands the preprocessed source straight to the driver.
GLSLANG_HDR := /usr/include/glslang/Include/glslang_c_interface.h
SPVC_HDR    := /usr/include/spirv_cross/spirv_cross_c.h
HAVE_GLSLANG := $(shell test -f $(GLSLANG_HDR) -a -f $(SPVC_HDR) && \
  pkg-config --exists spirv-cross-c-shared && echo 1 || echo 0)
ifeq ($(HAVE_GLSLANG),1)
GLSLANG_CFLAGS := -DXW_HAVE_GLSLANG=1
GLSLANG_LIBS   := -lglslang -lglslang-default-resource-limits \
                  $(shell pkg-config --libs spirv-cross-c-shared) -lstdc++
else
GLSLANG_CFLAGS :=
GLSLANG_LIBS   :=
$(info xwallpaper: scene shader pipeline OFF (need: glslang + spirv-cross))
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
$(DAEMON_OBJ): CFLAGS += $(WL_CFLAGS) $(PULSE_CFLAGS) $(SCENE_CFLAGS) $(GL_CFLAGS) $(FT_CFLAGS) $(SND_CFLAGS) $(LZ4_CFLAGS) $(GLSLANG_CFLAGS)

BINS := xwallpaper xwallpaperd

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(TEST_SRC:.c=)

INTEG_BIN := tests/integration/test_x11 tests/integration/test_image \
             tests/integration/test_video tests/integration/test_web \
             tests/integration/test_web_audio \
             tests/integration/test_scene \
             tests/integration/test_scene_gl \
             tests/integration/test_scene_particles \
             tests/integration/test_scene_text \
             tests/integration/test_scene_snd \
             tests/integration/test_scene_scripted \
             tests/integration/test_scene_props \
             tests/integration/test_app_geom tests/integration/test_app_all \
             tests/integration/test_fill tests/integration/test_restore

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
	$(CC) $(LDFLAGS) -o $@ $(DAEMON_OBJ) $(LDLIBS) $(WL_LIBS) $(PULSE_LIBS) $(SCENE_LIBS) $(GL_LIBS) $(FT_LIBS) $(SND_LIBS) $(LZ4_LIBS) $(GLSLANG_LIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# header deps (no auto-deps in this Makefile): without these, a struct
# layout change relinks stale objects against new ones (mixed ABI).
HEADER_DEPS := src/cli.h src/backend.h src/xw_err.h src/proto.h \
  src/client.h src/state.h src/x11.h src/app.h src/backends.h \
  src/wl.h src/daemon.h src/web_dir.h src/we_audio.h src/we_pulse.h \
  src/keybinds.h \
  src/scene_pkg.h src/scene_tex.h src/scene_shader.h src/scene_parse.h \
  src/scene_model.h src/scene_core.h src/scene_gl.h src/scene_image.h \
  src/scene_text_gl.h src/scene_particle_gl.h src/scene_glsl.h \
  src/scene_sound.h src/scene_script.h \
  src/scene.h $(wildcard src/proto/*-client-protocol.h)
$(CLI_OBJ) $(DAEMON_OBJ) $(TEST_BIN) $(INTEG_BIN) $(WL_INTEG_BIN): $(HEADER_DEPS)

# thin unit tests: only the client-side objects, no heavy libs
tests/test_cli tests/test_geom: tests/%: tests/%.c src/cli.o src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/cli.o src/web_dir.o

tests/test_client: tests/test_client.c src/cli.o src/proto.o src/client.o src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/cli.o src/proto.o src/client.o src/web_dir.o

tests/test_state: tests/test_state.c src/state.o src/cli.o src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/state.o src/cli.o src/web_dir.o

tests/test_web_dir: tests/test_web_dir.c src/web_dir.o
	$(CC) $(CFLAGS) -o $@ $< src/web_dir.o

tests/test_we_audio: tests/test_we_audio.c src/we_audio.o
	$(CC) $(CFLAGS) -o $@ $< src/we_audio.o -lm

tests/test_keybinds: tests/test_keybinds.c src/keybinds.o
	$(CC) $(CFLAGS) -o $@ $< src/keybinds.o


tests/test_scene_pkg: tests/test_scene_pkg.c src/scene_pkg.o
	$(CC) $(CFLAGS) -o $@ $< src/scene_pkg.o

tests/test_scene_tex: tests/test_scene_tex.c src/scene_tex.o
	$(CC) $(CFLAGS) $(LZ4_CFLAGS) -o $@ $< src/scene_tex.o $(LZ4_LIBS)

tests/test_scene_mesh: tests/test_scene_mesh.c src/scene_mesh.o
	$(CC) $(CFLAGS) -o $@ $< src/scene_mesh.o

tests/test_scene_shader: tests/test_scene_shader.c src/scene_shader.o
	$(CC) $(CFLAGS) -o $@ $< src/scene_shader.o

tests/test_scene_glsl: tests/test_scene_glsl.c src/scene_glsl.o
	$(CC) $(CFLAGS) $(GLSLANG_CFLAGS) -o $@ $< src/scene_glsl.o $(GLSLANG_LIBS)

tests/test_scene_parse: tests/test_scene_parse.c src/scene_parse.o src/scene_shader.o
	$(CC) $(CFLAGS) $(SCENE_CFLAGS) -o $@ $< src/scene_parse.o src/scene_shader.o $(SCENE_LIBS) -lm

tests/test_scene_core: tests/test_scene_core.c src/scene_core.o
	$(CC) $(CFLAGS) -o $@ $< src/scene_core.o -lm

tests/test_scene_particle: tests/test_scene_particle.c src/scene_particle.o
	$(CC) $(CFLAGS) -o $@ $< src/scene_particle.o -lm

tests/test_scene_text: tests/test_scene_text.c src/scene_text.o
	$(CC) $(CFLAGS) $(FT_CFLAGS) -o $@ $< src/scene_text.o $(FT_LIBS)

tests/test_scene_sound: tests/test_scene_sound.c src/scene_sound.o
	$(CC) $(CFLAGS) $(SND_CFLAGS) -o $@ $< src/scene_sound.o $(SND_LIBS)

tests/test_scene_script: tests/test_scene_script.c src/scene_script.o $(QJS_OBJ)
	$(CC) $(CFLAGS) -o $@ $< src/scene_script.o $(QJS_OBJ) -lm

# test_wl compiles wl.c sources directly (without XW_HAVE_WAYLAND, so only
# the pure-logic part builds): keeps the unit test free of wayland headers
# and avoids sharing src/wl.o flag state with the daemon build.
tests/test_wl: tests/test_wl.c src/wl.c src/cli.c src/web_dir.c
	$(CC) $(CFLAGS) -o $@ tests/test_wl.c src/wl.c src/cli.c src/web_dir.c

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t =="; ./$$t || exit 1; done

tests/integration/%: tests/integration/%.c $(filter-out src/dmain.o,$(DAEMON_OBJ))
	$(CC) $(CFLAGS) $(WL_CFLAGS) $(PULSE_CFLAGS) $(SCENE_CFLAGS) $(GL_CFLAGS) $(FT_CFLAGS) $(SND_CFLAGS) $(LZ4_CFLAGS) $(GLSLANG_CFLAGS) -o $@ $< $(filter-out src/dmain.o,$(DAEMON_OBJ)) $(LDLIBS) $(WL_LIBS) $(PULSE_LIBS) $(SCENE_LIBS) $(GL_LIBS) $(FT_LIBS) $(SND_LIBS) $(LZ4_LIBS) $(GLSLANG_LIBS) -lm

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
