#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cli.h"
#include "keybinds.h" /* XW_VOLUME_HINT (header-only, no extra link) */
#include "web_dir.h"

/* Parse an X11 geometry string "WxH+X+Y" or "WxH". Returns 1 on success.
 * Offset defaults to 0,0. Negative offsets ("-X-Y") are accepted. */
int xw_geom_parse(const char *s, int *x, int *y, unsigned int *w, unsigned int *h) {
  unsigned int ww, hh;
  int xi = 0, yi = 0;
  char *end;

  if (!s || !*s)
    return 0;

  ww = (unsigned int)strtoul(s, &end, 10);
  if (end == s || ww == 0)
    return 0;
  if (*end != 'x' && *end != 'X')
    return 0;
  end++;
  if (*end == '\0')
    return 0;
  hh = (unsigned int)strtoul(end, &end, 10);
  if (hh == 0)
    return 0;

  if (*end == '\0') {
    /* no offset */
  } else if (*end == '+' || *end == '-') {
    xi = (int)strtol(end, &end, 10);
    if (*end == '\0') {
      /* x only, y = 0 */
    } else if (*end == '+' || *end == '-') {
      yi = (int)strtol(end, &end, 10);
      if (*end != '\0')
        return 0;
    } else {
      return 0;
    }
  } else {
    return 0;
  }

  *w = ww;
  *h = hh;
  *x = xi;
  *y = yi;
  return 1;
}

static int is_image_ext(const char *ext) {
  static const char *imgs[] = {"png", "jpg", "jpeg", "webp", "bmp"};
  for (size_t i = 0; i < sizeof(imgs) / sizeof(imgs[0]); i++)
    if (strcmp(ext, imgs[i]) == 0)
      return 1;
  return 0;
}

static int is_video_ext(const char *ext) {
  static const char *vids[] = {"mp4", "webm", "mkv", "mov", "gif", "avi", "m4v"};
  for (size_t i = 0; i < sizeof(vids) / sizeof(vids[0]); i++)
    if (strcmp(ext, vids[i]) == 0)
      return 1;
  return 0;
}

static int is_web_ext(const char *ext) {
  /* single html pages are no longer set directly (wrap them in a dir
   * with index.html); svg stays as a self-contained image-like format */
  return strcmp(ext, "svg") == 0;
}

int xw_cli_detect_backend(const char *path) {
  if (!path || !*path)
    return -1;
  if (xw_web_is_http_url(path))
    return XW_BACKEND_WEB;

  /* an existing directory is unambiguously a web wallpaper (checked
   * before extension so "foo.png/" dirs aren't mistaken for images),
   * unless it holds a scene marker (scene.pkg / scene.json). Marker
   * presence is stat-only so the thin CLI never reads package content. */
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
    char marker[1152];
    snprintf(marker, sizeof(marker), "%s/scene.pkg", path);
    if (stat(marker, &st) == 0)
      return XW_BACKEND_SCENE;
    snprintf(marker, sizeof(marker), "%s/scene.json", path);
    if (stat(marker, &st) == 0)
      return XW_BACKEND_SCENE;
    return XW_BACKEND_WEB;
  }

  const char *dot = strrchr(path, '.');
  if (!dot || dot == path || dot[1] == '\0')
    return -1;

  char ext[16];
  size_t len = 0;
  for (const char *p = dot + 1; *p && len < sizeof(ext) - 1; p++) {
    ext[len++] = (char)tolower((unsigned char)*p);
  }
  ext[len] = '\0';

  if (is_image_ext(ext))
    return XW_BACKEND_IMAGE;
  if (is_video_ext(ext))
    return XW_BACKEND_VIDEO;
  if (is_web_ext(ext))
    return XW_BACKEND_WEB;
  return -1;
}

void xw_cli_usage(FILE *f) {
  fprintf(f,
    "Usage:\n"
    "  xwallpaper set [--image|--video|--web|--scene] [target] [opts] <path>\n"
    "  xwallpaper clear [-m <name|output>] [--keep]\n"
    "  xwallpaper restore\n"
    "  xwallpaper --list\n"
    "  xwallpaper --state\n"
    "  xwallpaper --list-properties [-m <output>]\n"
    "  xwallpaper --set-property <name=value> [-m <output>]\n"
    "  xwallpaper --clear-properties [-m <output>]\n"
    "  xwallpaper volume [-m <output>|--all] <+N|-N|N>\n"
    "  xwallpaper mute [-m <output>|--all] [on|off|toggle]\n"
    "  xwallpaper exit\n"
    "  xwallpaper --daemon   (debug: run the daemon in the foreground)\n"
    "\n"
    "Target:\n"
    "  (default)     primary output\n"
    "  -m <output>   xrandr output name (e.g. DP-1, HDMI-1)\n"
    "  --all         every output\n"
    "  -g WxH+X+Y --name <n>   explicit geometry window, named (required)\n"
  "  --span WxH+X+Y          span one image across outputs (Wayland, image only)\n"
    "\n"
    "Options:\n"
    "  --image --video --web --scene   force backend (default: by file extension)\n"
    "  --scale <mode>          image/video: fill(default)|contain|stretch|tile\n"
    "  --rotate <0|90|180|270> video: static rotation (e.g. portrait clips)\n"
    "  --keybinds <file>       video: mpv input.conf-style keybindings\n"
    "  --fps <number>          video: target frame rate (keeps speed, drops frames)\n"
    "  --mute --volume <0-100> video\n"
    "  --audio-client-name <name>   audio API client name (default: xwallpaper)\n"
    "  -o <0.0-1.0>            window opacity\n"
    "  -b -s -st -sp -nf       below, sticky, skip-taskbar, skip-pager, no-focus\n"
    "  -d                      daemonize (fork to background)\n"
    "  --debug                 verbose diagnostics\n"
    "\n"
    "Examples:\n"
    "  xwallpaper set ~/wall.png\n"
    "  xwallpaper set --all --video ~/clip.mp4 --mute\n"
    "  xwallpaper set -m HDMI-1 --web ~/page.html\n"
    "  xwallpaper set -g 3840x2160+0+0 --name dual --video ~/wide.mp4\n"
    "  xwallpaper clear -m dual\n"
    "  xwallpaper clear --keep -m eDP   # hide, keep last for `restore`\n"
    "  xwallpaper restore               # bring kept last wallpapers back\n");
}

/* volume accepts a relative step ("+2"/"-2") or an absolute level 0..100 */
static int parse_ctl_volume(const char *s, char *out, size_t n) {
  const char *p = s;
  long v;
  if (!s || !s[0] || !out || n == 0)
    return -1;
  if (*p == '+' || *p == '-')
    p++;
  if (!*p)
    return -1;
  for (; *p; p++)
    if (*p < '0' || *p > '9')
      return -1;
  v = strtol(s, NULL, 10);
  if (s[0] != '+' && s[0] != '-' && (v < 0 || v > 100))
    return -1;
  snprintf(out, n, "%s", s);
  return 0;
}

int xw_cli_parse(int argc, char **argv, XWConfig *cfg, XWError *err) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->backend_explicit = -1;
  cfg->scale_mode = XW_SCALE_FILL;
  cfg->volume = 100.0;
  cfg->opacity = 1.0;
  snprintf(cfg->audio_client_name, sizeof(cfg->audio_client_name), "xwallpaper");
  cfg->command = XW_CMD_NONE;

  if (argc < 2) {
    XW_ERROR_SET(err, 1, "no command given");
    return -1;
  }

  int i = 1;
  const char *cmd = argv[i++];

  if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
    xw_cli_usage(stdout);
    exit(0);
  }

  if (strcmp(cmd, "--list") == 0 || strcmp(cmd, "--state") == 0) {
    cfg->command = (strcmp(cmd, "--list") == 0) ? XW_CMD_LIST : XW_CMD_STATE;
    if (i < argc) {
      XW_ERROR_SET(err, 1, "unexpected argument after %s", cmd);
      return -1;
    }
    return 0;
  }

  if (strcmp(cmd, "--list-properties") == 0 ||
      strcmp(cmd, "--set-property") == 0 ||
      strcmp(cmd, "--clear-properties") == 0) {
    cfg->command = (strcmp(cmd, "--list-properties") == 0) ? XW_CMD_LIST_PROPS
                 : (strcmp(cmd, "--clear-properties") == 0) ? XW_CMD_CLEAR_PROPS
                                                           : XW_CMD_SET_PROP;
    cfg->target = XW_TARGET_PRIMARY;
    for (; i < argc; i++) {
      const char *a = argv[i];
      if (strcmp(a, "-m") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-m requires an output name");
          return -1;
        }
        snprintf(cfg->output, sizeof(cfg->output), "%s", argv[i]);
        cfg->target = XW_TARGET_OUTPUT;
      } else if (cfg->command == XW_CMD_SET_PROP && cfg->prop[0] == '\0' &&
                 strchr(a, '=')) {
        snprintf(cfg->prop, sizeof(cfg->prop), "%s", a);
      } else {
        XW_ERROR_SET(err, 1, "unexpected argument '%s' (%s takes only "
                     "[-m <output>]%s)", a, cmd,
                     cfg->command == XW_CMD_SET_PROP ? " name=value" : "");
        return -1;
      }
    }
    if (cfg->command == XW_CMD_SET_PROP && cfg->prop[0] == '\0') {
      XW_ERROR_SET(err, 1, "--set-property requires name=value");
      return -1;
    }
    return 0;
  }

  if (strcmp(cmd, "volume") == 0 || strcmp(cmd, "mute") == 0) {
    int is_volume = (strcmp(cmd, "volume") == 0);
    cfg->command = XW_CMD_CONTROL;
    cfg->target = XW_TARGET_PRIMARY;
    snprintf(cfg->ctl_name, sizeof(cfg->ctl_name), "%s", cmd);
    for (; i < argc; i++) {
      const char *a = argv[i];
      if (strcmp(a, "-m") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-m requires an output name");
          return -1;
        }
        snprintf(cfg->output, sizeof(cfg->output), "%s", argv[i]);
        cfg->target = XW_TARGET_OUTPUT;
      } else if (strcmp(a, "--all") == 0) {
        cfg->target = XW_TARGET_ALL;
      } else if (is_volume && cfg->ctl_value[0] == '\0') {
        if (parse_ctl_volume(a, cfg->ctl_value, sizeof(cfg->ctl_value)) != 0) {
          XW_ERROR_SET(err, 1, XW_VOLUME_HINT);
          return -1;
        }
      } else if (!is_volume && cfg->ctl_value[0] == '\0' &&
                 (strcmp(a, "on") == 0 || strcmp(a, "off") == 0 ||
                  strcmp(a, "toggle") == 0)) {
        snprintf(cfg->ctl_value, sizeof(cfg->ctl_value), "%s", a);
      } else {
        XW_ERROR_SET(err, 1,
                     "unexpected argument '%s' (%s takes [-m <output>|--all]%s)",
                     a, cmd, is_volume ? " <value>" : " [on|off|toggle]");
        return -1;
      }
    }
    if (is_volume && cfg->ctl_value[0] == '\0') {
      XW_ERROR_SET(err, 1, XW_VOLUME_HINT);
      return -1;
    }
    /* bare `mute` is the keybind case: toggle whatever it currently is */
    if (!is_volume && cfg->ctl_value[0] == '\0')
      snprintf(cfg->ctl_value, sizeof(cfg->ctl_value), "toggle");
    return 0;
  }

  if (strcmp(cmd, "--daemon") == 0) {
    cfg->command = XW_CMD_DAEMON;
    if (i < argc) {
      XW_ERROR_SET(err, 1, "unexpected argument after --daemon");
      return -1;
    }
    return 0;
  }

  if (strcmp(cmd, "exit") == 0) {
    cfg->command = XW_CMD_EXIT;
    if (i < argc) {
      XW_ERROR_SET(err, 1, "unexpected argument after exit");
      return -1;
    }
    return 0;
  }

  if (strcmp(cmd, "restore") == 0) {
    cfg->command = XW_CMD_RESTORE;
    if (i < argc) {
      XW_ERROR_SET(err, 1, "unexpected argument after restore");
      return -1;
    }
    return 0;
  }

  if (strcmp(cmd, "set") == 0) {
    cfg->command = XW_CMD_SET;
    cfg->target = XW_TARGET_PRIMARY;
  } else if (strcmp(cmd, "clear") == 0) {
    cfg->command = XW_CMD_CLEAR;
    cfg->target = XW_TARGET_ALL;
  } else {
    XW_ERROR_SET(err, 1, "unknown command '%s'", cmd);
    return -1;
  }

  for (; i < argc; i++) {
    const char *a = argv[i];

    if (cfg->command == XW_CMD_CLEAR) {
      /* clear accepts --all or -m <name/output>, and --keep */
      if (strcmp(a, "--all") == 0) {
        cfg->target = XW_TARGET_ALL;
      } else if (strcmp(a, "--keep") == 0) {
        cfg->keep = 1;
      } else if (strcmp(a, "-m") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-m requires an output name");
          return -1;
        }
        snprintf(cfg->output, sizeof(cfg->output), "%s", argv[i]);
        cfg->target = XW_TARGET_OUTPUT;
      } else {
        XW_ERROR_SET(err, 1, "clear: unexpected argument '%s'", a);
        return -1;
      }
      continue;
    }

    /* --flag form */
    if (strncmp(a, "--", 2) == 0) {
      if (strcmp(a, "--image") == 0) {
        cfg->backend_explicit = XW_BACKEND_IMAGE;
      } else if (strcmp(a, "--video") == 0) {
        cfg->backend_explicit = XW_BACKEND_VIDEO;
      } else if (strcmp(a, "--web") == 0) {
        cfg->backend_explicit = XW_BACKEND_WEB;
      } else if (strcmp(a, "--scene") == 0) {
        cfg->backend_explicit = XW_BACKEND_SCENE;
      } else if (strcmp(a, "--all") == 0) {
        if (cfg->have_span) {
          XW_ERROR_SET(err, 1, "--span cannot be combined with --all");
          return -1;
        }
        cfg->target = XW_TARGET_ALL;
      } else if (strcmp(a, "--name") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--name requires a value");
          return -1;
        }
        snprintf(cfg->name, sizeof(cfg->name), "%s", argv[i]);
      } else if (strcmp(a, "--mute") == 0) {
        cfg->mute = 1;
      } else if (strcmp(a, "--volume") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--volume requires a value");
          return -1;
        }
        cfg->volume = atof(argv[i]);
        if (cfg->volume < 0 || cfg->volume > 100) {
          XW_ERROR_SET(err, 1, "--volume must be 0..100");
          return -1;
        }
      } else if (strcmp(a, "--scale") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--scale requires a mode");
          return -1;
        }
        const char *m = argv[i];
        if (strcmp(m, "contain") == 0)
          cfg->scale_mode = XW_SCALE_CONTAIN;
        else if (strcmp(m, "stretch") == 0)
          cfg->scale_mode = XW_SCALE_STRETCH;
        else if (strcmp(m, "fill") == 0)
          cfg->scale_mode = XW_SCALE_FILL;
        else if (strcmp(m, "tile") == 0)
          cfg->scale_mode = XW_SCALE_TILE;
        else {
          XW_ERROR_SET(err, 1, "--scale: unknown mode '%s'", m);
          return -1;
        }
      } else if (strcmp(a, "--rotate") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--rotate requires a value");
          return -1;
        }
        char *end;
        long r = strtol(argv[i], &end, 10);
        if (*argv[i] == '\0' || *end != '\0') {
          XW_ERROR_SET(err, 1, "--rotate must be a number");
          return -1;
        }
        cfg->rotate = (int)r;
        if (cfg->rotate != 0 && cfg->rotate != 90 &&
            cfg->rotate != 180 && cfg->rotate != 270) {
          XW_ERROR_SET(err, 1, "--rotate must be 0, 90, 180 or 270");
          return -1;
        }
      } else if (strcmp(a, "--keybinds") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--keybinds requires a file path");
          return -1;
        }
        snprintf(cfg->keybinds, sizeof(cfg->keybinds), "%s", argv[i]);
      } else if (strcmp(a, "--audio-client-name") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--audio-client-name requires a value");
          return -1;
        }
        snprintf(cfg->audio_client_name, sizeof(cfg->audio_client_name), "%s", argv[i]);
      } else if (strcmp(a, "--fps") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--fps requires a value");
          return -1;
        }
        char *end;
        double f = strtod(argv[i], &end);
        if (*argv[i] == '\0' || *end != '\0' || f <= 0.0) {
          XW_ERROR_SET(err, 1, "--fps must be a positive number");
          return -1;
        }
        cfg->fps = f;
      } else if (strcmp(a, "--debug") == 0) {
        cfg->debug = 1;
      } else if (strcmp(a, "--span") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "--span requires a geometry string");
          return -1;
        }
        if (cfg->have_geom || cfg->output[0] ||
            cfg->target == XW_TARGET_ALL) {
          XW_ERROR_SET(err, 1, "--span cannot be combined with -g/-m/--all");
          return -1;
        }
        if (!xw_geom_parse(argv[i], &cfg->span_x, &cfg->span_y,
                           &cfg->span_w, &cfg->span_h)) {
          XW_ERROR_SET(err, 1, "invalid geometry '%s' (want WxH[+X+Y])", argv[i]);
          return -1;
        }
        cfg->have_span = 1;
        cfg->target = XW_TARGET_SPAN;
      } else {
        XW_ERROR_SET(err, 1, "unknown option '%s'", a);
        return -1;
      }
      continue;
    }

    /* -x short flags */
    if (a[0] == '-' && a[1] != '\0') {
      if (strcmp(a, "-m") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-m requires an output name");
          return -1;
        }
        if (cfg->have_span) {
          XW_ERROR_SET(err, 1, "--span cannot be combined with -m");
          return -1;
        }
        snprintf(cfg->output, sizeof(cfg->output), "%s", argv[i]);
        cfg->target = XW_TARGET_OUTPUT;
      } else if (strcmp(a, "-g") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-g requires a geometry string");
          return -1;
        }
        if (cfg->have_span) {
          XW_ERROR_SET(err, 1, "--span cannot be combined with -g");
          return -1;
        }
        if (!xw_geom_parse(argv[i], &cfg->geom_x, &cfg->geom_y,
                           &cfg->geom_w, &cfg->geom_h)) {
          XW_ERROR_SET(err, 1, "invalid geometry '%s' (want WxH[+X+Y])", argv[i]);
          return -1;
        }
        cfg->have_geom = 1;
        cfg->target = XW_TARGET_GEOM;
      } else if (strcmp(a, "-n") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-n requires a value");
          return -1;
        }
        snprintf(cfg->name, sizeof(cfg->name), "%s", argv[i]);
      } else if (strcmp(a, "-o") == 0) {
        if (++i >= argc) {
          XW_ERROR_SET(err, 1, "-o requires a value");
          return -1;
        }
        cfg->opacity = atof(argv[i]);
        if (cfg->opacity < 0 || cfg->opacity > 1) {
          XW_ERROR_SET(err, 1, "-o must be 0..1");
          return -1;
        }
      } else if (strcmp(a, "-b") == 0) {
        cfg->below = 1;
      } else if (strcmp(a, "-s") == 0) {
        cfg->sticky = 1;
      } else if (strcmp(a, "-st") == 0) {
        cfg->skip_taskbar = 1;
      } else if (strcmp(a, "-sp") == 0) {
        cfg->skip_pager = 1;
      } else if (strcmp(a, "-nf") == 0) {
        cfg->no_focus = 1;
      } else if (strcmp(a, "-d") == 0) {
        cfg->daemonize = 1;
      } else {
        XW_ERROR_SET(err, 1, "unknown option '%s'", a);
        return -1;
      }
      continue;
    }

    /* positional: the wallpaper path */
    if (cfg->path[0] != '\0') {
      XW_ERROR_SET(err, 1, "unexpected extra argument '%s'", a);
      return -1;
    }
    snprintf(cfg->path, sizeof(cfg->path), "%s", a);
  }

  if (cfg->command == XW_CMD_SET && cfg->path[0] == '\0') {
    XW_ERROR_SET(err, 1, "set requires a wallpaper path");
    return -1;
  }

  if (cfg->command == XW_CMD_SET && cfg->have_geom && cfg->name[0] == '\0') {
    XW_ERROR_SET(err, 1, "-g requires --name <name> so the window can be "
                 "addressed by clear/--list");
    return -1;
  }

  if (cfg->command == XW_CMD_SET && cfg->target == XW_TARGET_SPAN) {
    int kind = cfg->backend_explicit >= 0
                   ? cfg->backend_explicit
                   : xw_cli_detect_backend(cfg->path);
    if (kind != XW_BACKEND_IMAGE) {
      XW_ERROR_SET(err, 1, "--span only supports image wallpapers");
      return -1;
    }
  }

  return 0;
}
