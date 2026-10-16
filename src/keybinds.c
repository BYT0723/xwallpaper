#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keybinds.h"

/* X keysym names whose spelling differs from the character they produce.
 * Everything else is matched as written. */
static const struct {
  const char *name;
  const char *ch;
} kKeyNames[] = {
    {"minus", "-"},        {"equal", "="},        {"plus", "+"},
    {"comma", ","},        {"period", "."},       {"slash", "/"},
    {"backslash", "\\"},   {"semicolon", ";"},    {"apostrophe", "'"},
    {"grave", "`"},        {"bracketleft", "["},  {"bracketright", "]"},
    {"space", " "},        {"Return", "Return"},  {NULL, NULL},
};

static const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}

/* Copy the next whitespace-delimited token; returns the position after it.
 * Newlines terminate a token too: callers hand us a slice of the whole file,
 * so a token must not run into the following line. */
static const char *token(const char *p, char *out, size_t n) {
  size_t i = 0;
  p = skip_ws(p);
  while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
    if (i + 1 < n)
      out[i++] = *p;
    p++;
  }
  out[i] = '\0';
  return p;
}

static int parse_int(const char *s, int *out) {
  char *end = NULL;
  long v;
  if (!s || !s[0] || !out)
    return -1;
  v = strtol(s, &end, 10);
  if (!end || *end != '\0' || v < -100000 || v > 100000)
    return -1;
  *out = (int)v;
  return 0;
}

static int parse_double(const char *s, double *out) {
  char *end = NULL;
  double v;
  if (!s || !s[0] || !out)
    return -1;
  v = strtod(s, &end);
  if (!end || *end != '\0')
    return -1;
  *out = v;
  return 0;
}

/* One line into `b`; returns 0 when it produced a usable bind. */
static int parse_line(const char *line, size_t len, XWBind *b) {
  char tok[4][64];
  const char *p = line;
  char key[XW_BIND_KEY_MAX];
  int ntok = 0;

  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' ||
                     line[len - 1] == '\t'))
    len--;
  if (len == 0 || len > XW_KEYBINDS_LINE_MAX)
    return -1;

  memset(b, 0, sizeof(*b));
  p = token(p, key, sizeof(key));
  if (!key[0] || xw_keybinds_norm_key(key, b->key, sizeof(b->key)) != 0)
    return -1;

  while (ntok < 4) {
    p = token(p, tok[ntok], sizeof(tok[ntok]));
    if (!tok[ntok][0])
      break;
    ntok++;
  }

  if (ntok >= 2 && strcmp(tok[0], "seek") == 0) {
    if (parse_double(tok[1], &b->seek_secs) != 0)
      return -1;
    b->kind = XW_ACT_SEEK;
    return 0;
  }
  if (ntok >= 3 && strcmp(tok[0], "add") == 0 && strcmp(tok[1], "volume") == 0) {
    if (parse_int(tok[2], &b->volume_delta) != 0)
      return -1;
    b->kind = XW_ACT_VOLUME;
    return 0;
  }
  if (ntok >= 2 && strcmp(tok[0], "cycle") == 0 &&
      strcmp(tok[1], "mute") == 0) {
    b->kind = XW_ACT_MUTE;
    return 0;
  }
  if (ntok >= 2 && strcmp(tok[0], "cycle") == 0 &&
      strcmp(tok[1], "pause") == 0) {
    b->kind = XW_ACT_PAUSE;
    return 0;
  }
  if (ntok >= 3 && strcmp(tok[0], "cycle_values") == 0 &&
      strcmp(tok[1], "video-rotate") == 0) {
    /* the values live past the four-token window, so re-scan the rest */
    const char *q = line;
    char t[64];
    int got = 0;
    q = token(q, t, sizeof(t)); /* key */
    q = token(q, t, sizeof(t)); /* cycle_values */
    q = token(q, t, sizeof(t)); /* video-rotate */
    while (got < XW_BIND_ROTATE_MAX) {
      q = token(q, t, sizeof(t));
      if (!t[0])
        break;
      if (parse_int(t, &b->rotate[got]) != 0)
        return -1;
      got++;
    }
    if (got == 0)
      return -1;
    b->n_rotate = got;
    b->kind = XW_ACT_ROTATE;
    return 0;
  }
  return -1;
}

int xw_keybinds_parse(const char *text, XWBind *out, int max) {
  const char *p = text;
  int n = 0;
  if (!text || !out || max <= 0)
    return -1;
  while (*p && n < max) {
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    if (len <= XW_KEYBINDS_LINE_MAX) {
      const char *s = skip_ws(p);
      if (*s && *s != '#' && (size_t)(s - p) < len) {
        XWBind b;
        if (parse_line(s, len - (size_t)(s - p), &b) == 0)
          out[n++] = b;
      }
    }
    p = eol ? eol + 1 : NULL;
    if (!p)
      break;
  }
  return n;
}

const XWBind *xw_keybinds_find(const XWBind *binds, int n, const char *key) {
  int i;
  char norm[XW_BIND_KEY_MAX];
  if (!binds || n <= 0 || !key || !key[0])
    return NULL;
  if (xw_keybinds_norm_key(key, norm, sizeof(norm)) != 0)
    return NULL;
  for (i = 0; i < n; i++)
    if (strcmp(binds[i].key, norm) == 0)
      return &binds[i];
  return NULL;
}

int xw_keybinds_norm_key(const char *in, char *out, size_t n) {
  size_t i;
  if (!in || !in[0] || !out || n == 0)
    return -1;
  if (strlen(in) >= n)
    return -1;
  for (i = 0; kKeyNames[i].name; i++)
    if (strcmp(in, kKeyNames[i].name) == 0) {
      snprintf(out, n, "%s", kKeyNames[i].ch);
      return 0;
    }
  /* a modifier form ("Super+l") is kept, but its tail is still normalised */
  for (i = 0; in[i]; i++)
    if (in[i] == '+') {
      snprintf(out, n, "%s", in);
      if (in[i + 1] && !in[i + 2])
        out[i + 1] = (char)tolower((unsigned char)in[i + 1]);
      return 0;
    }
  if (in[1] == '\0') {
    out[0] = (char)tolower((unsigned char)in[0]);
    out[1] = '\0';
    return 0;
  }
  snprintf(out, n, "%s", in);
  return 0;
}

int xw_audio_apply(int cur, const char *value, int *out) {
  int v;
  if (!value || !value[0] || !out)
    return -1;
  if (value[0] == '+' || value[0] == '-') {
    if (parse_int(value, &v) != 0)
      return -1;
    v = cur + v;
  } else {
    if (parse_int(value, &v) != 0)
      return -1;
  }
  if (v < 0)
    v = 0;
  if (v > 100)
    v = 100;
  *out = v;
  return 0;
}

int xw_values_cycle(const int *vals, int n, int cur, int *out) {
  int i;
  if (!vals || n <= 0 || !out)
    return -1;
  for (i = 0; i < n; i++)
    if (vals[i] == cur) {
      *out = vals[(i + 1) % n];
      return 0;
    }
  *out = vals[0];
  return 0;
}

int xw_ctl_toggle(int cur, const char *value, int *out) {
  if (!out)
    return -1;
  if (!value || !value[0] || strcmp(value, "toggle") == 0)
    *out = !cur;
  else if (strcmp(value, "on") == 0)
    *out = 1;
  else if (strcmp(value, "off") == 0)
    *out = 0;
  else
    return -1;
  return 0;
}

int xw_keybinds_load(const char *path, XWBind *out, int max) {
  FILE *f;
  long sz;
  char *text;
  int n;
  size_t got;
  if (!path || !path[0] || !out || max <= 0)
    return -1;
  f = fopen(path, "rb");
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > 64 * 1024) {
    fclose(f);
    return -1;
  }
  text = malloc((size_t)sz + 1);
  if (!text) {
    fclose(f);
    return -1;
  }
  got = fread(text, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) {
    free(text);
    return -1;
  }
  text[sz] = '\0';
  n = xw_keybinds_parse(text, out, max);
  free(text);
  return n;
}

int xw_keybinds_default_path(char *out, size_t n) {
  const char *xdg, *home;
  if (!out || n == 0)
    return -1;
  xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    snprintf(out, n, "%s/xwallpaper/keys", xdg);
    return 0;
  }
  home = getenv("HOME");
  if (!home || !home[0])
    return -1;
  snprintf(out, n, "%s/.config/xwallpaper/keys", home);
  return 0;
}

int xw_keybinds_to_action(const XWBind *b, char *name, size_t n, char *value,
                          size_t m) {
  if (!b || !name || !value || n == 0 || m == 0)
    return -1;
  name[0] = '\0';
  value[0] = '\0';
  switch (b->kind) {
  case XW_ACT_VOLUME:
    snprintf(name, n, "volume");
    snprintf(value, m, "%+d", b->volume_delta);
    break;
  case XW_ACT_MUTE:
    snprintf(name, n, "mute");
    snprintf(value, m, "toggle");
    break;
  case XW_ACT_PAUSE:
    snprintf(name, n, "pause");
    snprintf(value, m, "toggle");
    break;
  case XW_ACT_SEEK:
    snprintf(name, n, "seek");
    snprintf(value, m, "%g", b->seek_secs);
    break;
  case XW_ACT_ROTATE:
    /* the engine's rotate action takes a ring; ours cycles the same default
     * the shipped key files name (90 180 270 0) */
    snprintf(name, n, "rotate");
    snprintf(value, m, "cycle");
    break;
  default:
    return -1;
  }
  return 0;
}
