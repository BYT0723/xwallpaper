#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto.h"

void xw_daemon_socket_path(char *out, size_t n) {
  const char *rt = getenv(XW_SOCKET_DIR_ENV);
  if (rt && rt[0])
    snprintf(out, n, "%s/%s/%s", rt, XW_SOCKET_SUBDIR, XW_SOCKET_NAME);
  else
    snprintf(out, n, "%s/%s/%s", "/tmp", XW_SOCKET_SUBDIR, XW_SOCKET_NAME);
}

static int append_field(char *msg, size_t n, size_t *off, const char *field) {
  size_t len = strlen(field);
  if (*off + len + 1 >= n) /* field + NUL, leave room for LF */
    return -1;
  memcpy(msg + *off, field, len);
  *off += len;
  msg[(*off)++] = '\0';
  return 0;
}

/* append a "key=value" field without a scratch buffer; fails on overflow
 * instead of truncating, so oversized values never silently corrupt the msg */
static int append_kv(char *msg, size_t n, size_t *off,
                     const char *key, const char *value) {
  size_t kl = strlen(key), vl = strlen(value);
  if (*off + kl + vl + 1 >= n)
    return -1;
  memcpy(msg + *off, key, kl);
  *off += kl;
  memcpy(msg + *off, value, vl);
  *off += vl;
  msg[(*off)++] = '\0';
  return 0;
}

static int finish(char *msg, size_t n, size_t *off) {
  if (*off + 1 >= n)
    return -1;
  msg[(*off)++] = '\n';
  msg[*off] = '\0';
  return (int)*off;
}

static const char *type_name(int kind) {
  switch (kind) {
  case XW_BACKEND_IMAGE: return "image";
  case XW_BACKEND_VIDEO: return "video";
  case XW_BACKEND_WEB:   return "web";
  case XW_BACKEND_SCENE: return "scene";
  default: return "unknown";
  }
}

static const char *scale_name(int mode) {
  switch (mode) {
  case XW_SCALE_STRETCH: return "stretch";
  case XW_SCALE_FILL:    return "fill";
  case XW_SCALE_TILE:    return "tile";
  default:               return "contain";
  }
}

static int encode_common(const char *verb, const XWConfig *cfg,
                         char *msg, size_t n) {
  size_t off = 0;
  if (append_field(msg, n, &off, verb) < 0)
    return -1;

  if (strcmp(verb, "clear") == 0) {
    const char *t = (cfg->target == XW_TARGET_OUTPUT) ? cfg->output : "all";
    if (append_field(msg, n, &off, t) < 0)
      return -1;
    if (cfg->keep) {
      if (append_field(msg, n, &off, "keep=1") < 0)
        return -1;
    }
    return finish(msg, n, &off);
  }

  /* set */
  const char *target;
  if (cfg->target == XW_TARGET_ALL)
    target = "all";
  else if (cfg->target == XW_TARGET_GEOM)
    target = "geom";
  else if (cfg->target == XW_TARGET_SPAN)
    target = "span";
  else
    target = cfg->output[0] ? cfg->output : "primary";
  if (append_field(msg, n, &off, target) < 0)
    return -1;
  if (append_field(msg, n, &off, type_name(cfg->backend_explicit < 0
        ? xw_cli_detect_backend(cfg->path) : cfg->backend_explicit)) < 0)
    return -1;
  if (append_field(msg, n, &off, cfg->path) < 0)
    return -1;

  char kv[128];
  snprintf(kv, sizeof(kv), "scale=%s", scale_name(cfg->scale_mode));
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  snprintf(kv, sizeof(kv), "mute=%d", cfg->mute);
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  if (cfg->rotate != 0) {
    snprintf(kv, sizeof(kv), "rotate=%d", cfg->rotate);
    if (append_field(msg, n, &off, kv) < 0)
      return -1;
  }
  if (cfg->keybinds[0]) {
    if (append_kv(msg, n, &off, "keybinds=", cfg->keybinds) < 0)
      return -1;
  }
  if (cfg->fps > 0.0) {
    snprintf(kv, sizeof(kv), "fps=%g", cfg->fps);
    if (append_field(msg, n, &off, kv) < 0)
      return -1;
  }
  if (cfg->audio_client_name[0]) {
    if (append_kv(msg, n, &off, "audio_client_name=", cfg->audio_client_name) < 0)
      return -1;
  }
  snprintf(kv, sizeof(kv), "volume=%.0f", cfg->volume);
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  snprintf(kv, sizeof(kv), "opacity=%.3f", cfg->opacity);
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  if (cfg->below)     { if (append_field(msg, n, &off, "below=1") < 0) return -1; }
  if (cfg->sticky)    { if (append_field(msg, n, &off, "sticky=1") < 0) return -1; }
  if (cfg->skip_taskbar) { if (append_field(msg, n, &off, "skip_taskbar=1") < 0) return -1; }
  if (cfg->skip_pager)   { if (append_field(msg, n, &off, "skip_pager=1") < 0) return -1; }
  if (cfg->no_focus)  { if (append_field(msg, n, &off, "no_focus=1") < 0) return -1; }
  if (cfg->have_geom) {
    char g[64];
    snprintf(g, sizeof(g), "geom=%dx%d+%d+%d",
             cfg->geom_w, cfg->geom_h, cfg->geom_x, cfg->geom_y);
    if (append_field(msg, n, &off, g) < 0)
      return -1;
  }
  if (cfg->have_span) {
    char g[64];
    snprintf(g, sizeof(g), "span=%dx%d+%d+%d",
             cfg->span_w, cfg->span_h, cfg->span_x, cfg->span_y);
    if (append_field(msg, n, &off, g) < 0)
      return -1;
  }
  if (cfg->name[0]) {
    if (append_kv(msg, n, &off, "name=", cfg->name) < 0)
      return -1;
  }
  return finish(msg, n, &off);
}

int xw_proto_encode_set(const XWConfig *cfg, char *msg, size_t n) {
  return encode_common("set", cfg, msg, n);
}

int xw_proto_encode_clear(const XWConfig *cfg, char *msg, size_t n) {
  return encode_common("clear", cfg, msg, n);
}

int xw_proto_encode_list(char *msg, size_t n) {
  return snprintf(msg, n, "list\n") < (int)n ? (int)strlen("list\n") : -1;
}

int xw_proto_encode_list_props(const XWConfig *cfg, char *msg, size_t n) {
  const char *t = (cfg->target == XW_TARGET_OUTPUT && cfg->output[0])
                      ? cfg->output
                      : "primary";
  size_t off = 0;
  if (append_field(msg, n, &off, "list-props") < 0)
    return -1;
  if (append_field(msg, n, &off, t) < 0)
    return -1;
  return finish(msg, n, &off);
}

int xw_proto_encode_set_prop(const XWConfig *cfg, char *msg, size_t n) {
  const char *t = (cfg->target == XW_TARGET_OUTPUT && cfg->output[0])
                      ? cfg->output
                      : "primary";
  size_t off = 0;
  if (append_field(msg, n, &off, "set-prop") < 0)
    return -1;
  if (append_field(msg, n, &off, t) < 0)
    return -1;
  if (append_field(msg, n, &off, cfg->prop) < 0)
    return -1;
  return finish(msg, n, &off);
}

int xw_proto_encode_control(const XWConfig *cfg, char *msg, size_t n) {
  const char *t;
  char kv[96];
  size_t off = 0;
  if (cfg->target == XW_TARGET_ALL)
    t = "all";
  else if (cfg->target == XW_TARGET_OUTPUT && cfg->output[0])
    t = cfg->output;
  else
    t = "primary";
  snprintf(kv, sizeof(kv), "%s=%s", cfg->ctl_name, cfg->ctl_value);
  if (append_field(msg, n, &off, "control") < 0)
    return -1;
  if (append_field(msg, n, &off, t) < 0)
    return -1;
  if (append_field(msg, n, &off, kv) < 0)
    return -1;
  return finish(msg, n, &off);
}

int xw_proto_encode_restore(char *msg, size_t n) {  return snprintf(msg, n, "restore\n") < (int)n ? (int)strlen("restore\n") : -1;
}

int xw_proto_encode_exit(char *msg, size_t n) {
  return snprintf(msg, n, "exit\n") < (int)n ? (int)strlen("exit\n") : -1;
}
