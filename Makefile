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

SRC := src/main.c src/x11.c src/cli.c src/app.c \
       src/image.c src/video.c src/web.c \
       src/client.c src/daemon.c src/state.c
OBJ := $(SRC:.c=.o)
BIN := xwallpaper

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(TEST_SRC:.c=)

INTEG_BIN := tests/integration/test_x11 tests/integration/test_image \
             tests/integration/test_video tests/integration/test_web \
             tests/integration/test_app_geom tests/integration/test_app_all \
             tests/integration/test_fill tests/integration/test_restore

ifdef DEBUG
CFLAGS  += -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
LDLIBS  += -fsanitize=address,undefined
endif

.PHONY: all clean test test-xvfb install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t =="; ./$$t || exit 1; done

tests/%: tests/%.c $(filter-out src/main.o,$(OBJ))
	$(CC) $(CFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJ)) $(LDLIBS)

test-xvfb: $(BIN) $(INTEG_BIN)
	@for t in $(INTEG_BIN); do \
	  echo "== $$t =="; \
	  xvfb-run -a -s "-screen 0 1920x1080x24" ./$$t || exit 1; \
	done

tests/integration/%: tests/integration/%.c $(filter-out src/main.o,$(OBJ))
	$(CC) $(CFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJ)) $(LDLIBS)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(OBJ) $(BIN) $(TEST_BIN) $(INTEG_BIN)
