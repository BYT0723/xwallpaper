#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "we_media.h"

static void copy_utf8(char *dst, size_t n, const char *src) {
  size_t len;
  if (!dst || n == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  len = strlen(src);
  if (len >= n) {
    const char *end = src + (n - 1);
    while (end > src && ((unsigned char)*end & 0xc0) == 0x80)
      end--;
    len = (size_t)(end - src);
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

int xw_we_media_json_escape(const char *in, char *out, size_t n) {
  size_t j = 0;
  if (!out || n == 0)
    return -1;
  if (!in) {
    out[0] = '\0';
    return 0;
  }
  for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
    const char *rep = NULL;
    char buf[7];
    size_t k;
    switch (*p) {
    case '"': rep = "\\\""; break;
    case '\\': rep = "\\\\"; break;
    case '\b': rep = "\\b"; break;
    case '\f': rep = "\\f"; break;
    case '\n': rep = "\\n"; break;
    case '\r': rep = "\\r"; break;
    case '\t': rep = "\\t"; break;
    default:
      if (*p < 0x20) {
        snprintf(buf, sizeof(buf), "\\u%04x", *p);
        rep = buf;
      }
      break;
    }
    k = rep ? strlen(rep) : 1;
    if (j + k >= n) {
      out[j < n ? j : n - 1] = '\0';
      return -1;
    }
    if (rep)
      memcpy(out + j, rep, k);
    else
      out[j] = (char)*p;
    j += k;
  }
  out[j] = '\0';
  return 0;
}

int xw_we_media_format_props(const XWMediaInfo *m, char *out, size_t n) {
  char t[XW_WE_MEDIA_TEXT_MAX * 6 + 1];
  char a[XW_WE_MEDIA_TEXT_MAX * 6 + 1];
  char al[XW_WE_MEDIA_TEXT_MAX * 6 + 1];
  char ct[sizeof(m->content_type) * 6 + 1];
  int w;
  if (!m || !out || n == 0)
    return -1;
  if (xw_we_media_json_escape(m->title, t, sizeof(t)) != 0 ||
      xw_we_media_json_escape(m->artist, a, sizeof(a)) != 0 ||
      xw_we_media_json_escape(m->album, al, sizeof(al)) != 0 ||
      xw_we_media_json_escape(m->content_type, ct, sizeof(ct)) != 0)
    return -1;
  w = snprintf(out, n,
               "{\"title\":\"%s\",\"artist\":\"%s\",\"albumTitle\":\"%s\","
               "\"contentType\":\"%s\"}",
               t, a, al, ct);
  return w >= 0 && (size_t)w < n ? 0 : -1;
}

static int data_url_safe(const char *u) {
  if (!u)
    return 0;
  for (const unsigned char *p = (const unsigned char *)u; *p; p++)
    if (*p < 0x20 || *p == 0x7f || *p == '"' || *p == '\\')
      return 0;
  return 1;
}

int xw_we_media_format_thumb(const XWMediaInfo *m, char *out, size_t n) {
  const char *u;
  char *esc = NULL;
  const char *s;
  int w;
  if (!m || !out || n == 0)
    return -1;
  u = m->thumbnail ? m->thumbnail : "";
  s = u;
  /* Our generated data: URLs are base64-only; anything else is escaped. */
  if (strncmp(u, "data:", 5) != 0) {
    esc = g_malloc(strlen(u) * 6 + 1);
    if (!esc)
      return -1;
    if (xw_we_media_json_escape(u, esc, strlen(u) * 6 + 1) != 0) {
      g_free(esc);
      return -1;
    }
    s = esc;
  } else if (!data_url_safe(u)) {
    s = "";
  }
  w = snprintf(out, n,
               "{\"thumbnail\":\"%s\",\"primaryColor\":\"\","
               "\"textColor\":\"\"}",
               s);
  g_free(esc);
  return w >= 0 && (size_t)w < n ? 0 : -1;
}

int xw_we_media_format_playback(const XWMediaInfo *m, char *out, size_t n) {
  int w;
  if (!m || !out || n == 0)
    return -1;
  w = snprintf(out, n, "{\"state\":%d}", m->state);
  return w >= 0 && (size_t)w < n ? 0 : -1;
}

int xw_we_media_format_timeline(const XWMediaInfo *m, char *out, size_t n) {
  double pos, dur;
  int w;
  if (!m || !out || n == 0)
    return -1;
  pos = isfinite(m->position) && m->position > 0 ? m->position : 0.0;
  dur = isfinite(m->duration) && m->duration > 0 ? m->duration : 0.0;
  w = snprintf(out, n, "{\"position\":%.3f,\"duration\":%.3f}", pos, dur);
  return w >= 0 && (size_t)w < n ? 0 : -1;
}

int xw_we_media_format_status(const XWMediaInfo *m, char *out, size_t n) {
  int w;
  if (!m || !out || n == 0)
    return -1;
  w = snprintf(out, n, "{\"enabled\":%s}", m->enabled ? "true" : "false");
  return w >= 0 && (size_t)w < n ? 0 : -1;
}

static GVariant *lookup_value(GVariant *dict, const char *key) {
  GVariant *v;
  if (!dict || !key)
    return NULL;
  v = g_variant_lookup_value(dict, key, NULL);
  if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
    GVariant *inner = g_variant_get_variant(v);
    g_variant_unref(v);
    return inner;
  }
  return v;
}

static void lookup_string(GVariant *dict, const char *key, char *out,
                          size_t n) {
  GVariant *v;
  copy_utf8(out, n, "");
  v = lookup_value(dict, key);
  if (v) {
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING))
      copy_utf8(out, n, g_variant_get_string(v, NULL));
    g_variant_unref(v);
  }
}

static void lookup_artist(GVariant *dict, char *out, size_t n) {
  GVariant *v;
  copy_utf8(out, n, "");
  v = lookup_value(dict, "xesam:artist");
  if (!v)
    return;
  if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
    copy_utf8(out, n, g_variant_get_string(v, NULL));
  } else if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING_ARRAY)) {
    GString *s = g_string_new(NULL);
    gsize count = g_variant_n_children(v);
    for (gsize i = 0; i < count; i++) {
      GVariant *child = g_variant_get_child_value(v, i);
      const char *name = g_variant_get_string(child, NULL);
      if (name && name[0]) {
        if (s->len)
          g_string_append(s, ", ");
        g_string_append(s, name);
      }
      g_variant_unref(child);
    }
    copy_utf8(out, n, s->str);
    g_string_free(s, TRUE);
  }
  g_variant_unref(v);
}

static int variant_seconds(GVariant *v, double *out) {
  double raw;
  if (!v || !out)
    return -1;
  if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT64))
    raw = (double)g_variant_get_int64(v);
  else if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT64))
    raw = (double)g_variant_get_uint64(v);
  else if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT32))
    raw = (double)g_variant_get_int32(v);
  else if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32))
    raw = (double)g_variant_get_uint32(v);
  else if (g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE))
    raw = g_variant_get_double(v);
  else
    return -1;
  *out = raw / 1e6;
  return 0;
}

static void infer_content_type(const char *url, char *out, size_t n) {
  const char *dot;
  if (!url || !url[0]) {
    copy_utf8(out, n, "music");
    return;
  }
  dot = strrchr(url, '.');
  if (dot &&
      (!g_ascii_strcasecmp(dot, ".mp4") || !g_ascii_strcasecmp(dot, ".webm") ||
       !g_ascii_strcasecmp(dot, ".mkv") || !g_ascii_strcasecmp(dot, ".avi") ||
       !g_ascii_strcasecmp(dot, ".mov") || !g_ascii_strcasecmp(dot, ".m4v"))) {
    copy_utf8(out, n, "video");
  } else if (dot && (!g_ascii_strcasecmp(dot, ".png") ||
                     !g_ascii_strcasecmp(dot, ".jpg") ||
                     !g_ascii_strcasecmp(dot, ".jpeg") ||
                     !g_ascii_strcasecmp(dot, ".gif") ||
                     !g_ascii_strcasecmp(dot, ".webp"))) {
    copy_utf8(out, n, "image");
  } else {
    copy_utf8(out, n, "music");
  }
}

int xw_we_media_parse_metadata(GVariant *metadata, XWMediaInfo *m) {
  GVariant *v;
  char url[XW_WE_MEDIA_URL_MAX];
  if (!metadata || !m)
    return -1;
  if (!g_variant_is_of_type(metadata, G_VARIANT_TYPE("a{sv}")))
    return -1;
  m->title[0] = '\0';
  m->artist[0] = '\0';
  m->album[0] = '\0';
  m->art_url[0] = '\0';
  m->content_type[0] = '\0';
  m->duration = 0.0;

  lookup_string(metadata, "xesam:title", m->title, sizeof(m->title));
  lookup_artist(metadata, m->artist, sizeof(m->artist));
  lookup_string(metadata, "xesam:album", m->album, sizeof(m->album));
  lookup_string(metadata, "mpris:artUrl", m->art_url, sizeof(m->art_url));
  lookup_string(metadata, "xesam:url", url, sizeof(url));

  v = lookup_value(metadata, "mpris:length");
  if (v) {
    (void)variant_seconds(v, &m->duration);
    g_variant_unref(v);
  }
  infer_content_type(url, m->content_type, sizeof(m->content_type));
  return 0;
}

int xw_we_media_state_from_status(const char *s) {
  if (s && strcmp(s, "Playing") == 0)
    return XW_WE_MEDIA_PLAYING;
  if (s && strcmp(s, "Paused") == 0)
    return XW_WE_MEDIA_PAUSED;
  return XW_WE_MEDIA_STOPPED;
}

int xw_we_media_pick(const XWMediaPick *p, int n) {
  int best_play = -1, best_pause = -1;
  uint64_t play_at = 0, pause_at = 0;
  if (!p || n <= 0)
    return -1;
  for (int i = 0; i < n; i++) {
    if (!p[i].name || strstr(p[i].name, "playerctld"))
      continue;
    if (p[i].state == XW_WE_MEDIA_PLAYING) {
      if (best_play < 0 || p[i].last_active > play_at) {
        best_play = i;
        play_at = p[i].last_active;
      }
    } else if (p[i].state == XW_WE_MEDIA_PAUSED) {
      if (best_pause < 0 || p[i].last_active > pause_at) {
        best_pause = i;
        pause_at = p[i].last_active;
      }
    }
  }
  return best_play >= 0 ? best_play : best_pause;
}

XWMediaArtKind xw_we_media_art_kind(const char *url) {
  if (!url || !url[0])
    return XW_WE_MEDIA_ART_NONE;
  if (strncmp(url, "file://", 7) == 0 || url[0] == '/')
    return XW_WE_MEDIA_ART_FILE;
  if (g_str_has_prefix(url, "http://") || g_str_has_prefix(url, "https://"))
    return XW_WE_MEDIA_ART_HTTP;
  if (g_str_has_prefix(url, "data:"))
    return XW_WE_MEDIA_ART_DATA;
  return XW_WE_MEDIA_ART_OTHER;
}

static const char *sniff_mime(const unsigned char *data, gsize len) {
  if (len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
      data[3] == 'G' && data[4] == '\r' && data[5] == '\n' && data[6] == 0x1a &&
      data[7] == '\n')
    return "image/png";
  if (len >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
    return "image/jpeg";
  if (len >= 6 && (memcmp(data, "GIF87a", 6) == 0 ||
                   memcmp(data, "GIF89a", 6) == 0))
    return "image/gif";
  if (len >= 12 && memcmp(data, "RIFF", 4) == 0 &&
      memcmp(data + 8, "WEBP", 4) == 0)
    return "image/webp";
  return NULL;
}

int xw_we_media_resolve_art(const char *url, char **out) {
  XWMediaArtKind kind;
  char *path = NULL;
  GStatBuf st;
  gchar *data = NULL;
  gsize len = 0;
  const char *mime;
  char *b64;
  if (!out)
    return -1;
  *out = NULL;
  kind = xw_we_media_art_kind(url);
  if (kind == XW_WE_MEDIA_ART_NONE)
    return 0;
  if (kind == XW_WE_MEDIA_ART_HTTP || kind == XW_WE_MEDIA_ART_DATA) {
    *out = g_strdup(url);
    return *out ? 0 : -1;
  }
  if (kind != XW_WE_MEDIA_ART_FILE)
    return -1;
  if (strncmp(url, "file://", 7) == 0)
    path = g_filename_from_uri(url, NULL, NULL);
  else
    path = g_strdup(url);
  if (!path)
    return -1;
  if (g_stat(path, &st) != 0 || st.st_size <= 0 ||
      (guint64)st.st_size > XW_WE_MEDIA_ART_MAX) {
    g_free(path);
    return -1;
  }
  if (!g_file_get_contents(path, &data, &len, NULL)) {
    g_free(path);
    return -1;
  }
  g_free(path);
  if (len > XW_WE_MEDIA_ART_MAX) {
    g_free(data);
    return -1;
  }
  mime = sniff_mime((const unsigned char *)data, len);
  if (!mime) {
    g_free(data);
    return -1;
  }
  b64 = g_base64_encode((const guchar *)data, len);
  g_free(data);
  if (!b64)
    return -1;
  *out = g_strdup_printf("data:%s;base64,%s", mime, b64);
  g_free(b64);
  return *out ? 0 : -1;
}

/* ---- MPRIS bus layer ---- */

#define XW_MEDIA_MAX_PLAYERS 16
#define XW_MEDIA_RETRY_US (5 * G_USEC_PER_SEC)

typedef struct {
  char name[256];
  XWMediaInfo info;
  char *thumb;
  double base_position;
  int64_t base_time_us;
  double rate;
  uint64_t last_active;
  guint sub_props;
  guint sub_seeked;
} XWMediaPlayer;

static GDBusConnection *g_bus;
static guint g_sub_names;
static XWMediaPlayer g_players[XW_MEDIA_MAX_PLAYERS];
static int g_nplayers;
static XWMediaInfo g_snap;
static int64_t g_retry_us;
static int64_t g_last_timeline_us;
static char g_active_name[256];

static void refresh_active(int64_t now, int force);
static void get_all_props(const char *name);

static int64_t media_now(void) { return (int64_t)g_get_monotonic_time(); }

static XWMediaPlayer *find_player(const char *name) {
  if (!name || !name[0])
    return NULL;
  for (int i = 0; i < g_nplayers; i++)
    if (g_players[i].name[0] && strcmp(g_players[i].name, name) == 0)
      return &g_players[i];
  return NULL;
}

static void variant_to_string(GVariant *v, char *out, size_t n) {
  GVariant *inner = v;
  int owned = 0;
  copy_utf8(out, n, "");
  if (!v)
    return;
  if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
    inner = g_variant_get_variant(v);
    owned = 1;
  }
  if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING))
    copy_utf8(out, n, g_variant_get_string(inner, NULL));
  if (owned)
    g_variant_unref(inner);
}

static int variant_to_double(GVariant *v, double *out) {
  GVariant *inner = v;
  int owned = 0;
  int r = -1;
  if (!v || !out)
    return -1;
  if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
    inner = g_variant_get_variant(v);
    owned = 1;
  }
  if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_DOUBLE)) {
    *out = g_variant_get_double(inner);
    r = 0;
  } else if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_INT64)) {
    *out = (double)g_variant_get_int64(inner);
    r = 0;
  } else if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT64)) {
    *out = (double)g_variant_get_uint64(inner);
    r = 0;
  }
  if (owned)
    g_variant_unref(inner);
  return r;
}

static int variant_to_seconds(GVariant *v, double *out) {
  GVariant *inner = v;
  int owned = 0;
  int r;
  if (!v || !out)
    return -1;
  if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
    inner = g_variant_get_variant(v);
    owned = 1;
  }
  r = variant_seconds(inner, out);
  if (owned)
    g_variant_unref(inner);
  return r;
}

static double player_position(const XWMediaPlayer *p, int64_t now) {
  double pos;
  if (!p)
    return 0.0;
  pos = p->base_position;
  if (p->info.state == XW_WE_MEDIA_PLAYING) {
    double rate = p->rate > 0.0 && isfinite(p->rate) ? p->rate : 1.0;
    double dt = (double)(now - p->base_time_us) / 1e6;
    if (dt > 0.0)
      pos += dt * rate;
  }
  if (p->info.duration > 0.0 && pos > p->info.duration)
    pos = p->info.duration;
  if (!isfinite(pos) || pos < 0.0)
    pos = 0.0;
  return pos;
}

static void player_update_thumb(XWMediaPlayer *p) {
  char *thumb = NULL;
  if (!p)
    return;
  if (xw_we_media_resolve_art(p->info.art_url, &thumb) != 0)
    thumb = NULL;
  g_free(p->thumb);
  p->thumb = thumb;
}

static void player_clear_meta(XWMediaPlayer *p) {
  if (!p)
    return;
  p->info.title[0] = '\0';
  p->info.artist[0] = '\0';
  p->info.album[0] = '\0';
  p->info.art_url[0] = '\0';
  p->info.duration = 0.0;
  player_update_thumb(p);
}

static void refresh_active(int64_t now, int force) {
  XWMediaPick picks[XW_MEDIA_MAX_PLAYERS];
  int n = 0;
  int idx;
  XWMediaPlayer *p;
  int active_changed = 0;
  int enabled;
  int props_changed = 0;
  int state_changed = 0;
  double pos;
  const char *want;
  const char *have;

  for (int i = 0; i < g_nplayers; i++) {
    picks[n].name = g_players[i].name;
    picks[n].state = g_players[i].info.state;
    picks[n].last_active = g_players[i].last_active;
    n++;
  }
  idx = xw_we_media_pick(picks, n);
  p = idx >= 0 ? &g_players[idx] : NULL;

  if (!p) {
    if (g_active_name[0]) {
      g_active_name[0] = '\0';
      active_changed = 1;
    }
  } else if (strcmp(g_active_name, p->name) != 0) {
    snprintf(g_active_name, sizeof(g_active_name), "%s", p->name);
    active_changed = 1;
  }

  enabled = g_bus != NULL;
  if (g_snap.enabled != enabled) {
    g_snap.enabled = enabled;
    g_snap.v_status++;
  }
  if (force) {
    g_snap.v_props++;
    g_snap.v_thumb++;
    g_snap.v_playback++;
    g_snap.v_timeline++;
  }

  if (!p) {
    if (active_changed || force) {
      if (g_snap.title[0] || g_snap.artist[0] || g_snap.album[0] ||
          g_snap.art_url[0] || g_snap.content_type[0]) {
        g_snap.title[0] = '\0';
        g_snap.artist[0] = '\0';
        g_snap.album[0] = '\0';
        g_snap.art_url[0] = '\0';
        g_snap.content_type[0] = '\0';
        g_snap.v_props++;
      }
      if (g_snap.state != XW_WE_MEDIA_STOPPED) {
        g_snap.state = XW_WE_MEDIA_STOPPED;
        g_snap.v_playback++;
      }
      if (g_snap.position != 0.0 || g_snap.duration != 0.0) {
        g_snap.position = 0.0;
        g_snap.duration = 0.0;
        g_snap.v_timeline++;
      }
      if (g_snap.thumbnail) {
        g_free(g_snap.thumbnail);
        g_snap.thumbnail = NULL;
        g_snap.v_thumb++;
      }
    }
    return;
  }

  props_changed = active_changed ||
                  strcmp(g_snap.title, p->info.title) != 0 ||
                  strcmp(g_snap.artist, p->info.artist) != 0 ||
                  strcmp(g_snap.album, p->info.album) != 0 ||
                  strcmp(g_snap.art_url, p->info.art_url) != 0 ||
                  strcmp(g_snap.content_type, p->info.content_type) != 0;
  if (props_changed) {
    snprintf(g_snap.title, sizeof(g_snap.title), "%s", p->info.title);
    snprintf(g_snap.artist, sizeof(g_snap.artist), "%s", p->info.artist);
    snprintf(g_snap.album, sizeof(g_snap.album), "%s", p->info.album);
    snprintf(g_snap.art_url, sizeof(g_snap.art_url), "%s", p->info.art_url);
    snprintf(g_snap.content_type, sizeof(g_snap.content_type), "%s",
             p->info.content_type);
    g_snap.v_props++;
  }

  state_changed = active_changed || g_snap.state != p->info.state;
  if (state_changed) {
    g_snap.state = p->info.state;
    g_snap.v_playback++;
  }
  if (active_changed || g_snap.duration != p->info.duration) {
    g_snap.duration = p->info.duration;
    g_snap.v_timeline++;
  }

  pos = player_position(p, now);
  g_snap.position = pos;

  want = p->thumb ? p->thumb : "";
  have = g_snap.thumbnail ? g_snap.thumbnail : "";
  if (strcmp(want, have) != 0) {
    g_free(g_snap.thumbnail);
    g_snap.thumbnail = want[0] ? g_strdup(want) : NULL;
    g_snap.v_thumb++;
  }
  if (props_changed || state_changed || active_changed)
    g_snap.v_timeline++;
}

static void get_all_cb(GObject *source, GAsyncResult *res, gpointer ud) {
  char *name = (char *)ud;
  GError *err = NULL;
  GVariant *ret;
  GVariant *dict;
  GVariant *v;
  XWMediaPlayer *p;
  int64_t now = media_now();
  char status[32];
  double sec;

  ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
  if (!ret) {
    if (err)
      g_error_free(err);
    g_free(name);
    return;
  }
  p = find_player(name);
  if (!p) {
    g_variant_unref(ret);
    g_free(name);
    return;
  }
  dict = g_variant_get_child_value(ret, 0);
  if (!dict || !g_variant_is_of_type(dict, G_VARIANT_TYPE("a{sv}"))) {
    if (dict)
      g_variant_unref(dict);
    g_variant_unref(ret);
    g_free(name);
    return;
  }

  v = lookup_value(dict, "PlaybackStatus");
  if (v) {
    variant_to_string(v, status, sizeof(status));
    if (status[0])
      p->info.state = xw_we_media_state_from_status(status);
    g_variant_unref(v);
  }

  v = lookup_value(dict, "Metadata");
  if (v) {
    if (g_variant_is_of_type(v, G_VARIANT_TYPE("a{sv}"))) {
      xw_we_media_parse_metadata(v, &p->info);
      player_update_thumb(p);
    }
    g_variant_unref(v);
  } else {
    player_clear_meta(p);
  }

  v = lookup_value(dict, "Position");
  if (v) {
    if (variant_seconds(v, &sec) == 0)
      p->base_position = sec;
    g_variant_unref(v);
  }
  v = lookup_value(dict, "Rate");
  if (v) {
    double rate;
    if (variant_to_double(v, &rate) == 0 && rate > 0.0 && isfinite(rate))
      p->rate = rate;
    g_variant_unref(v);
  }
  p->base_time_us = now;
  p->info.position = p->base_position;
  if (p->info.state == XW_WE_MEDIA_PLAYING) {
    p->last_active = (uint64_t)now;
    g_last_timeline_us = now;
  }
  g_variant_unref(dict);
  g_variant_unref(ret);
  g_free(name);
  refresh_active(now, 0);
}

static void get_all_props(const char *name) {
  char *dup;
  if (!g_bus || !name || !name[0])
    return;
  dup = g_strdup(name);
  if (!dup)
    return;
  g_dbus_connection_call(
      g_bus, name, "/org/mpris/MediaPlayer2",
      "org.freedesktop.DBus.Properties", "GetAll",
      g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
      G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
      get_all_cb, dup);
}

static void props_changed_cb(GDBusConnection *conn, const gchar *sender_name,
                             const gchar *object_path,
                             const gchar *interface_name,
                             const gchar *signal_name, GVariant *params,
                             gpointer ud) {
  const char *name = (const char *)ud;
  XWMediaPlayer *p = find_player(name);
  const char *iface;
  GVariant *changed;
  GVariant *invalidated;
  int64_t now = media_now();
  (void)conn;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  if (!g_bus || !p || !params)
    return;
  g_variant_get(params, "(&s@a{sv}@as)", &iface, &changed, &invalidated);
  if (iface && strcmp(iface, "org.mpris.MediaPlayer2.Player") == 0) {
    GVariantIter iter;
    const char *key;
    GVariant *value;
    g_variant_iter_init(&iter, changed);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
      if (strcmp(key, "PlaybackStatus") == 0) {
        char status[32];
        int ns;
        variant_to_string(value, status, sizeof(status));
        ns = xw_we_media_state_from_status(status);
        if (ns != p->info.state) {
          double old = player_position(p, now);
          p->info.state = ns;
          p->base_position = ns == XW_WE_MEDIA_STOPPED ? 0.0 : old;
          p->base_time_us = now;
          if (ns == XW_WE_MEDIA_PLAYING) {
            p->last_active = (uint64_t)now;
            g_last_timeline_us = now;
          }
        }
      } else if (strcmp(key, "Metadata") == 0) {
        GVariant *meta = value;
        int owned = 0;
        if (g_variant_is_of_type(meta, G_VARIANT_TYPE_VARIANT)) {
          meta = g_variant_get_variant(value);
          owned = 1;
        }
        if (meta && g_variant_is_of_type(meta, G_VARIANT_TYPE("a{sv}"))) {
          xw_we_media_parse_metadata(meta, &p->info);
          player_update_thumb(p);
        }
        if (owned)
          g_variant_unref(meta);
      } else if (strcmp(key, "Rate") == 0) {
        double rate;
        if (variant_to_double(value, &rate) == 0 && rate > 0.0 &&
            isfinite(rate)) {
          double old = player_position(p, now);
          p->rate = rate;
          p->base_position = old;
          p->base_time_us = now;
        }
      } else if (strcmp(key, "Position") == 0) {
        double sec;
        if (variant_to_seconds(value, &sec) == 0) {
          p->base_position = sec;
          p->base_time_us = now;
        }
      }
      g_variant_unref(value);
    }
  }
  if (invalidated) {
    GVariantIter iter;
    const char *key;
    g_variant_iter_init(&iter, invalidated);
    while (g_variant_iter_next(&iter, "&s", &key)) {
      if (strcmp(key, "Metadata") == 0 || strcmp(key, "PlaybackStatus") == 0) {
        get_all_props(p->name);
        break;
      }
    }
  }
  g_variant_unref(changed);
  g_variant_unref(invalidated);
  refresh_active(now, 0);
}

static void seeked_cb(GDBusConnection *conn, const gchar *sender_name,
                      const gchar *object_path, const gchar *interface_name,
                      const gchar *signal_name, GVariant *params, gpointer ud) {
  const char *name = (const char *)ud;
  XWMediaPlayer *p = find_player(name);
  gint64 pos = 0;
  int64_t now = media_now();
  (void)conn;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  if (!g_bus || !p || !params)
    return;
  g_variant_get(params, "(x)", &pos);
  p->base_position = (double)pos / 1e6;
  p->base_time_us = now;
  p->info.position = p->base_position;
  if (strcmp(g_active_name, p->name) == 0) {
    g_snap.position = p->base_position;
    g_snap.v_timeline++;
  }
}

static void add_player(const char *name) {
  XWMediaPlayer *p;
  int64_t now = media_now();
  if (!g_bus || !name || !name[0] || find_player(name))
    return;
  if (!g_str_has_prefix(name, "org.mpris.MediaPlayer2.") ||
      strstr(name, "playerctld"))
    return;
  if (g_nplayers >= XW_MEDIA_MAX_PLAYERS)
    return;
  p = &g_players[g_nplayers++];
  memset(p, 0, sizeof(*p));
  snprintf(p->name, sizeof(p->name), "%s", name);
  p->info.state = XW_WE_MEDIA_STOPPED;
  p->rate = 1.0;
  p->base_time_us = now;
  p->sub_props = g_dbus_connection_signal_subscribe(
      g_bus, p->name, "org.freedesktop.DBus.Properties", "PropertiesChanged",
      "/org/mpris/MediaPlayer2", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
      props_changed_cb, g_strdup(p->name), g_free);
  p->sub_seeked = g_dbus_connection_signal_subscribe(
      g_bus, p->name, "org.mpris.MediaPlayer2.Player", "Seeked",
      "/org/mpris/MediaPlayer2", NULL, G_DBUS_SIGNAL_FLAGS_NONE, seeked_cb,
      g_strdup(p->name), g_free);
  get_all_props(p->name);
  refresh_active(now, 0);
}

static void remove_player(const char *name) {
  for (int i = 0; i < g_nplayers; i++) {
    XWMediaPlayer *p = &g_players[i];
    if (strcmp(p->name, name) != 0)
      continue;
    if (g_bus) {
      if (p->sub_props)
        g_dbus_connection_signal_unsubscribe(g_bus, p->sub_props);
      if (p->sub_seeked)
        g_dbus_connection_signal_unsubscribe(g_bus, p->sub_seeked);
    }
    g_free(p->thumb);
    memset(p, 0, sizeof(*p));
    if (i != g_nplayers - 1) {
      g_players[i] = g_players[g_nplayers - 1];
      memset(&g_players[g_nplayers - 1], 0, sizeof(g_players[0]));
    }
    g_nplayers--;
    return;
  }
}

static void name_owner_changed_cb(GDBusConnection *conn,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name, GVariant *params,
                                  gpointer ud) {
  const char *name, *old_owner, *new_owner;
  int64_t now = media_now();
  (void)conn;
  (void)sender_name;
  (void)object_path;
  (void)interface_name;
  (void)signal_name;
  (void)ud;
  if (!params)
    return;
  g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);
  if (!name || !g_str_has_prefix(name, "org.mpris.MediaPlayer2.") ||
      strstr(name, "playerctld"))
    return;
  if (new_owner && new_owner[0]) {
    if (find_player(name))
      get_all_props(name);
    else
      add_player(name);
  } else if (!new_owner || !new_owner[0])
    remove_player(name);
  refresh_active(now, 0);
}

static void list_names_cb(GObject *source, GAsyncResult *res, gpointer ud) {
  GError *err = NULL;
  GVariant *ret;
  GVariant *names;
  int64_t now = media_now();
  (void)ud;
  ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
  if (!ret) {
    if (err)
      g_error_free(err);
    return;
  }
  if (!g_bus) {
    g_variant_unref(ret);
    return;
  }
  names = g_variant_get_child_value(ret, 0);
  if (names && g_variant_is_of_type(names, G_VARIANT_TYPE_STRING_ARRAY)) {
    gsize count = g_variant_n_children(names);
    for (gsize i = 0; i < count; i++) {
      GVariant *child = g_variant_get_child_value(names, i);
      add_player(g_variant_get_string(child, NULL));
      g_variant_unref(child);
    }
  }
  if (names)
    g_variant_unref(names);
  g_variant_unref(ret);
  refresh_active(now, 0);
}

static void list_names(void) {
  if (!g_bus)
    return;
  g_dbus_connection_call(
      g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"),
      G_DBUS_CALL_FLAGS_NONE, 3000, NULL, list_names_cb, NULL);
}

int xw_we_media_start(void) {
  GError *err = NULL;
  int64_t now = media_now();
  if (g_bus)
    return 0;
  if (g_retry_us && now < g_retry_us)
    return -1;
  g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
  if (!g_bus) {
    if (err)
      g_error_free(err);
    g_retry_us = now + XW_MEDIA_RETRY_US;
    g_snap.enabled = 0;
    g_snap.v_status++;
    return -1;
  }
  g_retry_us = 0;
  g_sub_names = g_dbus_connection_signal_subscribe(
      g_bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
      "NameOwnerChanged", "/org/freedesktop/DBus", NULL,
      G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed_cb, NULL, NULL);
  g_snap.enabled = 1;
  g_snap.v_status++;
  g_last_timeline_us = now;
  refresh_active(now, 1);
  list_names();
  return 0;
}

void xw_we_media_stop(void) {
  if (g_bus && g_sub_names) {
    g_dbus_connection_signal_unsubscribe(g_bus, g_sub_names);
    g_sub_names = 0;
  }
  for (int i = 0; i < g_nplayers; i++) {
    XWMediaPlayer *p = &g_players[i];
    if (g_bus) {
      if (p->sub_props)
        g_dbus_connection_signal_unsubscribe(g_bus, p->sub_props);
      if (p->sub_seeked)
        g_dbus_connection_signal_unsubscribe(g_bus, p->sub_seeked);
    }
    g_free(p->thumb);
    memset(p, 0, sizeof(*p));
  }
  g_nplayers = 0;
  if (g_bus) {
    g_object_unref(g_bus);
    g_bus = NULL;
  }
  g_retry_us = 0;
  g_last_timeline_us = 0;
  g_active_name[0] = '\0';
  g_free(g_snap.thumbnail);
  g_snap.thumbnail = NULL;
  g_snap.title[0] = '\0';
  g_snap.artist[0] = '\0';
  g_snap.album[0] = '\0';
  g_snap.art_url[0] = '\0';
  g_snap.content_type[0] = '\0';
  g_snap.state = XW_WE_MEDIA_STOPPED;
  g_snap.position = 0.0;
  g_snap.duration = 0.0;
  if (g_snap.enabled) {
    g_snap.enabled = 0;
    g_snap.v_status++;
  }
}

void xw_we_media_tick(void) {
  XWMediaPlayer *p;
  int64_t now;
  if (!g_bus)
    return;
  now = media_now();
  p = find_player(g_active_name);
  if (!p || p->info.state != XW_WE_MEDIA_PLAYING)
    return;
  g_snap.position = player_position(p, now);
  if (g_last_timeline_us == 0)
    g_last_timeline_us = now;
  if (now - g_last_timeline_us >= G_USEC_PER_SEC) {
    g_last_timeline_us = now;
    g_snap.v_timeline++;
  }
}

const XWMediaInfo *xw_we_media_get(void) { return &g_snap; }
