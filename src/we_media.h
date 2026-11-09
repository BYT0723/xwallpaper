#ifndef XW_WE_MEDIA_H
#define XW_WE_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#include <glib.h>

/* WE media-integration snapshot. The bus layer owns the live instance
 * returned by xw_we_media_get(); formatters only read it. */
#define XW_WE_MEDIA_TEXT_MAX 512
#define XW_WE_MEDIA_URL_MAX 2048
#define XW_WE_MEDIA_ART_MAX (2 * 1024 * 1024)

typedef struct {
  char title[XW_WE_MEDIA_TEXT_MAX];
  char artist[XW_WE_MEDIA_TEXT_MAX];
  char album[XW_WE_MEDIA_TEXT_MAX];
  char content_type[32];
  char art_url[XW_WE_MEDIA_URL_MAX];
  char *thumbnail; /* data: URL or http(s) URL; NULL means no artwork */
  int state;       /* 0 playing, 1 paused, 2 stopped */
  double position; /* seconds */
  double duration; /* seconds, 0 when unknown */
  int enabled;     /* media integration available */
  /* Monotonic field versions; web.c uses them for per-view dedup. */
  uint64_t v_props, v_thumb, v_playback, v_timeline, v_status;
} XWMediaInfo;

enum {
  XW_WE_MEDIA_PLAYING = 0,
  XW_WE_MEDIA_PAUSED = 1,
  XW_WE_MEDIA_STOPPED = 2,
};

typedef struct {
  const char *name;
  int state;
  uint64_t last_active;
} XWMediaPick;

typedef enum {
  XW_WE_MEDIA_ART_NONE = 0,
  XW_WE_MEDIA_ART_FILE,
  XW_WE_MEDIA_ART_HTTP,
  XW_WE_MEDIA_ART_DATA,
  XW_WE_MEDIA_ART_OTHER,
} XWMediaArtKind;

/* JSON-escape into out (quotes, backslash, control chars). Returns 0, or -1
 * on NULL/too-small output. */
int xw_we_media_json_escape(const char *in, char *out, size_t n);

/* Build the object body for each WE listener callback. All return 0, or -1
 * on bad args/truncation. */
int xw_we_media_format_props(const XWMediaInfo *m, char *out, size_t n);
int xw_we_media_format_thumb(const XWMediaInfo *m, char *out, size_t n);
int xw_we_media_format_playback(const XWMediaInfo *m, char *out, size_t n);
int xw_we_media_format_timeline(const XWMediaInfo *m, char *out, size_t n);
int xw_we_media_format_status(const XWMediaInfo *m, char *out, size_t n);

/* Fill title/artist/album/content_type/art_url/duration from an MPRIS
 * Metadata a{sv}. Missing keys become empty/zero. Leaves state/position/
 * versions/enabled alone. Returns 0, or -1 on bad args. */
int xw_we_media_parse_metadata(GVariant *metadata, XWMediaInfo *m);

int xw_we_media_state_from_status(const char *s);
int xw_we_media_pick(const XWMediaPick *p, int n);
XWMediaArtKind xw_we_media_art_kind(const char *url);

/* Resolve MPRIS artUrl to something an <img> can load:
 * file:// or absolute path -> data:image/...;base64,... (2 MiB raw cap),
 * http(s)/data -> g_strdup(url). On success *out is NULL (no art) or a
 * heap string owned by the caller. Returns 0, or -1 on bad args/read error. */
int xw_we_media_resolve_art(const char *url, char **out);

/* Process-wide MPRIS reader (main thread only, one bus connection for all
 * web views). start() is idempotent; stop() releases everything. */
int xw_we_media_start(void);
void xw_we_media_stop(void);
void xw_we_media_tick(void);
const XWMediaInfo *xw_we_media_get(void);

#endif /* XW_WE_MEDIA_H */
