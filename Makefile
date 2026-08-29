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
CLI_SRC := src/main.c src/cli.c src/proto.c src/client.c
CLI_OBJ := $(CLI_SRC:.c=.o)

# daemon: owns all wallpaper windows, links the full backend stack.
DAEMON_SRC := src/dmain.c src/daemon.c src/state.c src/cli.c src/proto.c \
              src/x11.c src/app.c src/image.c src/video.c src/web.c
DAEMON_OBJ := $(DAEMON_SRC:.c=.o)

BINS := xwallpaper xwallpaperd

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(TEST_SRC:.c=)

INTEG_BIN := tests/integration/test_x11 tests/integration/test_image \
             tests/integration/test_video tests/integration/test_web \
             tests/integration/test_app_geom tests/integration/test_app_all \
             tests/integration/test_fill tests/integration/test_restore

ifdef DEBUG
SANITIZE  := -fsanitize=address,undefined
CFLAGS    += -O0 -g3 $(SANITIZE) -fno-omit-frame-pointer
LDLIBS    += $(SANITIZE)
endif

.PHONY: all clean test test-xvfb check-client-libs install uninstall

all: $(BINS)

xwallpaper: $(CLI_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(CLI_OBJ) $(SANITIZE)

xwallpaperd: $(DAEMON_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(DAEMON_OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# thin unit tests: only the client-side objects, no heavy libs
tests/test_cli tests/test_geom: tests/%: tests/%.c src/cli.o
	$(CC) $(CFLAGS) -o $@ $< src/cli.o

tests/test_client: tests/test_client.c src/cli.o src/proto.o src/client.o
	$(CC) $(CFLAGS) -o $@ $< src/cli.o src/proto.o src/client.o

tests/test_state: tests/test_state.c src/state.o src/cli.o
	$(CC) $(CFLAGS) -o $@ $< src/state.o src/cli.o

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t =="; ./$$t || exit 1; done

tests/integration/%: tests/integration/%.c $(filter-out src/dmain.o,$(DAEMON_OBJ))
	$(CC) $(CFLAGS) -o $@ $< $(filter-out src/dmain.o,$(DAEMON_OBJ)) $(LDLIBS)

test-xvfb: $(BINS) $(INTEG_BIN)
	@for t in $(INTEG_BIN); do \
	  echo "== $$t =="; \
	  xvfb-run -a -s "-screen 0 1920x1080x24" ./$$t || exit 1; \
	done

check-client-libs: xwallpaper
	@! ldd ./xwallpaper | grep -qE 'webkit|mpv|Imlib2|javascriptcore' \
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
