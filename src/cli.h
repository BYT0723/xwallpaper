#ifndef XW_CLI_H
#define XW_CLI_H

#include <stdio.h>

#include "xw_err.h"
#include "backend.h"

/* Subcommands */
enum {
  XW_CMD_NONE = 0,
  XW_CMD_SET,     /* set a wallpaper */
  XW_CMD_CLEAR,   /* remove wallpaper(s) */
  XW_CMD_LIST,    /* list running instances (--list) */
  XW_CMD_STATE,   /* dump window state as name/type/path/rotation TSV (--state) */
  XW_CMD_EXIT,    /* stop the daemon (exit) */
  XW_CMD_DAEMON,  /* run the daemon in the foreground (--daemon) */
  XW_CMD_RESTORE, /* restore last wallpapers kept by clear --keep (restore) */
};

/* Which windows a command targets */
enum {
  XW_TARGET_NONE = 0,
  XW_TARGET_PRIMARY, /* default: primary output */
  XW_TARGET_OUTPUT,  /* explicit -m <output> */
  XW_TARGET_ALL,     /* --all */
  XW_TARGET_GEOM,    /* -g <WxH+X+Y> overrides geometry */
  XW_TARGET_SPAN,    /* --span <WxH+X+Y>: layout-space rect split per output */
};

typedef struct XWConfig {
  int command;        /* XW_CMD_* */
  int target;         /* XW_TARGET_* */
  char output[64];    /* monitor name when target == OUTPUT */
  int have_geom;      /* -g given */
  int geom_x, geom_y;
  unsigned int geom_w, geom_h;
  char name[64];      /* -n/--name: name for -g windows (used for clear/--list) */
  int have_span;      /* --span given (Wayland image spanning) */
  int span_x, span_y;
  unsigned int span_w, span_h;

  /* backend opts */
  int backend_explicit; /* -1 = auto, else XW_BACKEND_* */
  char path[1024];
  int scale_mode;     /* XW_SCALE_* for image */
  int mute;           /* video */
  double volume;      /* video, 0..100, default 100 */
  int rotate;         /* video, 0/90/180/270, default 0 */
  char keybinds[1024]; /* video, mpv input-conf file path, empty = none */
  double fps;         /* video, target frame rate, 0 = auto */
  char audio_client_name[64]; /* audio API client name, default "xwallpaper" */
  double opacity;     /* 0..1, default 1.0 */
  int below;          /* -b */
  int sticky;         /* -s */
  int skip_taskbar;   /* -st */
  int skip_pager;     /* -sp */
  int no_focus;       /* -nf */
  int daemonize;      /* -d */
  int debug;          /* --debug */
  int keep;           /* clear --keep: remove windows but retain last state */
} XWConfig;

/* Parse argv (argc>=1, argv[0]=program). Returns 0 on success, -1 on error
 * (message written to err->msg; also prints usage hint). */
int xw_cli_parse(int argc, char **argv, XWConfig *cfg, XWError *err);

/* Parse an X11 geometry string "WxH+X+Y" / "WxH-X-Y" / "WxH".
 * Returns 1 on success, 0 on malformed input. Offsets default to 0. */
int xw_geom_parse(const char *s, int *x, int *y, unsigned int *w, unsigned int *h);

/* Detect backend kind from a file path by extension (lowercased).
 * Returns XW_BACKEND_* or -1 if unknown. */
int xw_cli_detect_backend(const char *path);

/* Print usage to the given stream. */
void xw_cli_usage(FILE *f);

#endif /* XW_CLI_H */
