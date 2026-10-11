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

#endif /* XW_WEB_DIR_H */
