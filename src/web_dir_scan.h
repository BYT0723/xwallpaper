#ifndef XW_WEB_DIR_SCAN_H
#define XW_WEB_DIR_SCAN_H

/* Pure directory enumeration for WE "directory" user properties.
 * Recurses into subdirectories, never follows symlinks, and filters by the
 * property's fileType (image / video). Only the daemon needs this; the CLI
 * must not link it. */

/* 1 when path's extension belongs to kind ("video" = webm/ogg/ogv, anything
 * else = the WE image set). NULL/unknown extensions are rejected. */
int xw_web_dir_match(const char *path, const char *kind);

/* Collect up to max matching regular files under dir into a malloc'd,
 * sorted array of malloc'd absolute paths. Returns the count, or -1 on bad
 * arguments or when dir cannot be opened. Caller frees each entry + array. */
int xw_web_dir_scan(const char *dir, const char *kind, char ***out, int max);

#endif /* XW_WEB_DIR_SCAN_H */
