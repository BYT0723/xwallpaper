#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cli.h"
#include "state.h"

static const char *type_name(int kind) {
  switch (kind) {
  case XW_BACKEND_IMAGE: return "image";
  case XW_BACKEND_VIDEO: return "video";
  case XW_BACKEND_WEB:   return "web";
  default: return "unknown";
  }
}

static int type_from_name(const char *n) {
  if (strcmp(n, "image") == 0) return XW_BACKEND_IMAGE;
  if (strcmp(n, "video") == 0) return XW_BACKEND_VIDEO;
  if (strcmp(n, "web") == 0)   return XW_BACKEND_WEB;
  return -1;
}

/* 转义字段中的 '\t' '\n' '\r' '\\', 返回写入的字节数 (不含 NUL)。
 * cap 需能容纳最坏情况 (2x 输入 + 1)。截断时返回 -1。 */
static int escape_field(char *out, size_t cap, const char *s) {
  size_t o = 0;
  for (const char *p = s; *p; p++) {
    const char *esc = NULL;
    switch (*p) {
    case '\t': esc = "\\t"; break;
    case '\n': esc = "\\n"; break;
    case '\r': esc = "\\r"; break;
    case '\\': esc = "\\\\"; break;
    default: break;
    }
    if (esc) {
      if (o + 2 >= cap)
        return -1;
      out[o++] = esc[0];
      out[o++] = esc[1];
    } else {
      if (o + 1 >= cap)
        return -1;
      out[o++] = *p;
    }
  }
  out[o] = '\0';
  return (int)o;
}

/* 反转义上述转义序列。超长截断时返回 -1 (半截字段不可信, 拒绝该行)。 */
static int unescape_field(char *out, size_t cap, const char *s) {
  size_t o = 0;
  for (const char *p = s; *p; p++) {
    if (*p == '\\' && p[1]) {
      p++;
      char c;
      switch (*p) {
      case 't': c = '\t'; break;
      case 'n': c = '\n'; break;
      case 'r': c = '\r'; break;
      case '\\': c = '\\'; break;
      default: c = *p; /* 未知转义保留原字符 */
      }
      if (o + 1 >= cap)
        return -1;
      out[o++] = c;
    } else {
      if (o + 1 >= cap)
        return -1;
      out[o++] = *p;
    }
  }
  out[o] = '\0';
  return 0;
}

int xw_state_path_for(int use_wayland, char *out, size_t n) {
  const char *file = use_wayland ? "state-wayland" : "state";
  int r;
  const char *xdg = getenv("XDG_STATE_HOME");
  if (xdg && xdg[0]) {
    r = snprintf(out, n, "%s/xwallpaper/%s", xdg, file);
  } else {
    const char *home = getenv("HOME");
    if (!home || !home[0])
      home = ".";
    r = snprintf(out, n, "%s/.local/state/xwallpaper/%s", home, file);
  }
  return r < (int)n ? 0 : -1;
}

int xw_state_path(char *out, size_t n) {
  return xw_state_path_for(0, out, n);
}

int xw_state_effective_mute(const XWStateEntry *e) {
  return e->mute < 0 ? (e->type == XW_BACKEND_VIDEO ? 1 : 0) : e->mute;
}

int xw_state_format_line(const XWStateEntry *e, char *out, size_t n) {
  /* 最坏情况 2x 转义后 + NUL */
  char name[2 * XW_STATE_NAME_LEN + 1];
  char path[2 * XW_STATE_PATH_LEN + 1];
  char kb[2 * XW_STATE_KEYBINDS_LEN + 1];
  char ac[2 * XW_STATE_NAME_LEN + 1];
  if (escape_field(name, sizeof(name), e->name) < 0 ||
      escape_field(path, sizeof(path), e->path) < 0 ||
      escape_field(kb, sizeof(kb), e->keybinds) < 0 ||
      escape_field(ac, sizeof(ac), e->audio_client_name) < 0)
    return -1;
  /* 落盘永远写有效 0/1, 不泄漏内存哨兵 -1 */
  int mute = xw_state_effective_mute(e);
  int r;
  if (e->have_geom)
    r = snprintf(out, n, "%s\t%s\t%s\t%d\t%ux%u+%d+%d\t%d\t%d\t%g\t%g\t%d\t%g\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
                 name, type_name(e->type), path, e->rotation,
                 e->w, e->h, e->x, e->y, e->keep, mute, e->volume, e->fps,
                 e->scale_mode, e->opacity, e->below, e->sticky,
                 e->skip_taskbar, e->skip_pager, e->no_focus, kb, ac);
  else
    r = snprintf(out, n, "%s\t%s\t%s\t%d\t-\t%d\t%d\t%g\t%g\t%d\t%g\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
                 name, type_name(e->type), path, e->rotation, e->keep, mute,
                 e->volume, e->fps, e->scale_mode, e->opacity, e->below,
                 e->sticky, e->skip_taskbar, e->skip_pager, e->no_focus, kb, ac);
  return (r < 0 || r >= (int)n) ? -1 : r;
}

/* 按 TAB 分割一行 (原地改写, 返回各字段指针 + 数量)。 */
static int split_tabs(char *line, char **f, int max) {
  int n = 0;
  char *p = line;
  while (n < max) {
    f[n++] = p;
    char *tab = strchr(p, '\t');
    if (!tab)
      break;
    *tab = '\0';
    p = tab + 1;
  }
  return n;
}

int xw_state_parse_line(const char *line, XWStateEntry *e) {
  /* 一行最长: 2x(name+path) 转义 + 其它字段 */
  char buf[XW_STATE_LINE_MAX];
  if (strlen(line) >= sizeof(buf))
    return -1;
  strcpy(buf, line);
  /* 去掉结尾换行 */
  size_t bl = strlen(buf);
  if (bl > 0 && (buf[bl - 1] == '\n' || buf[bl - 1] == '\r'))
    buf[bl - 1] = '\0';

  char *f[18];
  int nf = split_tabs(buf, f, 18);
  if (nf < 5)
    return -1;

  memset(e, 0, sizeof(*e));
  if (unescape_field(e->name, sizeof(e->name), f[0]) != 0)
    return -1;
  int type = type_from_name(f[1]);
  if (type < 0)
    return -1;
  e->type = type;
  if (unescape_field(e->path, sizeof(e->path), f[2]) != 0)
    return -1;
  e->rotation = atoi(f[3]);

  if (strcmp(f[4], "-") == 0) {
    e->have_geom = 0;
  } else {
    if (!xw_geom_parse(f[4], &e->x, &e->y, &e->w, &e->h))
      return -1;
    e->have_geom = 1;
  }

  e->keep = (nf >= 6) ? atoi(f[5]) : 0;

  /* backend flags (第 7 列起)。旧 6 字段行缺失: mute 置哨兵 -1,
   * volume/scale/opacity/hints 取默认 (memset 已给 0, 故只补非 0 默认)。 */
  e->mute = -1;
  e->volume = 100.0;
  e->scale_mode = XW_SCALE_FILL;
  e->opacity = 1.0;
  if (nf >= 7)
    e->mute = atoi(f[6]);
  if (nf >= 8)
    e->volume = atof(f[7]);
  if (nf >= 9)
    e->fps = atof(f[8]);
  if (nf >= 10)
    e->scale_mode = atoi(f[9]);
  if (nf >= 11)
    e->opacity = atof(f[10]);
  if (nf >= 12)
    e->below = atoi(f[11]);
  if (nf >= 13)
    e->sticky = atoi(f[12]);
  if (nf >= 14)
    e->skip_taskbar = atoi(f[13]);
  if (nf >= 15)
    e->skip_pager = atoi(f[14]);
  if (nf >= 16)
    e->no_focus = atoi(f[15]);
  if (nf >= 17 && unescape_field(e->keybinds, sizeof(e->keybinds), f[16]) != 0)
    return -1;
  if (nf >= 18 &&
      unescape_field(e->audio_client_name, sizeof(e->audio_client_name), f[17]) != 0)
    return -1;
  return 0;
}

static int ensure_dir_for(const char *path) {
  char dir[1024];
  snprintf(dir, sizeof(dir), "%s", path);
  char *slash = strrchr(dir, '/');
  if (!slash)
    return 0;
  *slash = '\0';
  if (mkdir(dir, 0700) != 0 && errno != EEXIST)
    return -1;
  return 0;
}

int xw_state_save(const char *path, const XWStateEntry *entries, int count) {
  if (ensure_dir_for(path) != 0)
    return -1;

  char tmp[1100];
  snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
  FILE *f = fopen(tmp, "w");
  if (!f)
    return -1;
  for (int i = 0; i < count; i++) {
    char line[XW_STATE_LINE_MAX];
    if (xw_state_format_line(&entries[i], line, sizeof(line)) < 0) {
      fclose(f);
      unlink(tmp);
      return -1;
    }
    if (fputs(line, f) == EOF) {
      fclose(f);
      unlink(tmp);
      return -1;
    }
  }
  if (fclose(f) != 0) {
    unlink(tmp);
    return -1;
  }
  if (rename(tmp, path) != 0) {
    unlink(tmp);
    return -1;
  }
  return 0;
}

int xw_state_load(const char *path, XWStateEntry *entries, int max, int *count) {
  *count = 0;
  FILE *f = fopen(path, "r");
  if (!f)
    return errno == ENOENT ? 0 : -1;

  char line[XW_STATE_LINE_MAX];
  while (fgets(line, sizeof(line), f) && *count < max) {
    XWStateEntry e;
    if (xw_state_parse_line(line, &e) == 0)
      entries[(*count)++] = e;
  }
  fclose(f);
  return 0;
}

int xw_state_upsert(const char *path, const XWStateEntry *e) {
  XWStateEntry entries[XW_STATE_MAX_ENTRIES];
  int count = 0;
  if (xw_state_load(path, entries, XW_STATE_MAX_ENTRIES, &count) != 0)
    return -1;

  int idx = -1;
  for (int i = 0; i < count; i++) {
    if (strcmp(entries[i].name, e->name) == 0) {
      idx = i;
      break;
    }
  }
  if (idx >= 0) {
    entries[idx] = *e;
  } else {
    if (count >= XW_STATE_MAX_ENTRIES)
      return -1;
    entries[count++] = *e;
  }
  return xw_state_save(path, entries, count);
}
