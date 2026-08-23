CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -D_GNU_SOURCE
LDFLAGS ?=

PKGS := x11 xrandr xrender imlib2 mpv gtk+-3.0 webkit2gtk-4.1
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  := $(shell pkg-config --libs $(PKGS))

SRC := src/main.c src/x11.c src/cli.c src/app.c \
       src/image.c src/video.c src/web.c \
       src/client.c src/daemon.c
OBJ := $(SRC:.c=.o)
BIN := xwallpaper

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(TEST_SRC:.c=)

ifdef DEBUG
CFLAGS  += -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
LDLIBS  += -fsanitize=address,undefined
endif

.PHONY: all clean test test-xvfb

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "== $$t =="; ./$$t || exit 1; done

tests/%: tests/%.c $(filter-out src/main.o,$(OBJ))
	$(CC) $(CFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJ)) $(LDLIBS)

test-xvfb: $(BIN) tests/integration/test_x11 tests/integration/test_image tests/integration/test_video tests/integration/test_web tests/integration/test_app_geom tests/integration/test_app_all tests/integration/test_fill
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_x11
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_image
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_video
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_web
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_app_geom
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_app_all
	xvfb-run -a -s "-screen 0 1920x1080x24" ./tests/integration/test_fill

tests/integration/%: tests/integration/%.c $(filter-out src/main.o,$(OBJ))
	$(CC) $(CFLAGS) -o $@ $< $(filter-out src/main.o,$(OBJ)) $(LDLIBS)

clean:
	rm -f $(OBJ) $(BIN) $(TEST_BIN)
	rm -f tests/integration/test_x11 tests/integration/test_image tests/integration/test_video tests/integration/test_web tests/integration/test_app_geom tests/integration/test_app_all tests/integration/test_fill
