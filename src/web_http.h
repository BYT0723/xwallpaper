#ifndef XW_WEB_HTTP_H
#define XW_WEB_HTTP_H

#include <stddef.h>

#include "xw_err.h"

/* Localhost HTTP server for web dir wallpapers (daemon-only).
 * Unity WebGL exports fetch their .wasm/.data and need real HTTP response
 * headers (Content-Type: application/wasm, Content-Length); file:// gives
 * empty headers and Unity aborts. Each wallpaper owns one server bound to
 * 127.0.0.1 on an ephemeral port, rooted at its wallpaper dir. No new build
 * dependency: GSocketService comes with the GTK stack we already link. */
typedef struct XWWebHttp XWWebHttp;

/* Start serving root (must be an existing dir). preferred_port > 0 asks
 * for that loopback port back (stable origin across restarts); a taken
 * port falls back to ephemeral instead of failing. Returns NULL on
 * failure. Only 127.0.0.1 is ever bound. */
XWWebHttp *xw_web_http_start(const char *root, int preferred_port,
                             XWError *err);
int xw_web_http_port(const XWWebHttp *h);
void xw_web_http_stop(XWWebHttp *h);

/* Pure helpers (no sockets, unit-testable). */
const char *xw_web_http_mime(const char *path);
int xw_web_http_resolve(const char *root, const char *target, char *out,
                        size_t n);
int xw_web_http_parse_range(const char *hdr, long long size, long long *a,
                            long long *b);

/* Expose one absolute file path (outside the wallpaper root, e.g. a WE
 * "file" user property) as /_xw/file/<id> on this server. An http-origin
 * page cannot touch file:// or absolute-path values at all, so the daemon
 * rewrites such property values to the returned alias URL before delivery.
 * Only existing regular files are accepted. Ids are stable per path and
 * live with the server. Returns the id, or -1. Thread-safe. */
#define XW_HTTP_ALIAS_MAX 64
int xw_web_http_alias(XWWebHttp *h, const char *abs_path);

#endif /* XW_WEB_HTTP_H */
