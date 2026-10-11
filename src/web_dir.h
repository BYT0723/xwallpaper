#ifndef XW_WEB_DIR_H
#define XW_WEB_DIR_H

#include <stddef.h>

/* 1 = http(s) URL, 0 = not. NULL-safe. */
int xw_web_is_http_url(const char *path);

/* Resolve a `set --web` input.
 * Returns 1 for http(s) URLs (root/entry untouched),
 * 0 for a local dir containing index.html (root = abs dir, entry = "index.html"),
 * -1 for anything else (local files, missing dirs, dirs without index.html). */
int xw_web_resolve(const char *in, char *root, size_t rn, char *entry, size_t en);

/* MIME type by filename extension (lowercased). NULL-safe, never returns NULL. */
const char *xw_web_mime_for(const char *filename);

/* Map a URI-scheme request path (e.g. "/sub/app.js", "/") to a relative path.
 * Returns 0 on success, -1 on traversal/invalid input. NULL-safe. */
int xw_web_req_to_rel(const char *req_path, char *rel, size_t n);

#endif /* XW_WEB_DIR_H */
