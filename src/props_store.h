#ifndef XW_PROPS_STORE_H
#define XW_PROPS_STORE_H

#include <stddef.h>

/* Per-wallpaper user-property sidecar (disk persistence for --set-property).
 * Keyed by wallpaper identity (web root_dir absolute),
 * shared across X11/Wayland and across windows showing the same wallpaper.
 * Pure logic + file IO, no GTK/WebKit/X11; unit-testable. */

#define XW_STORED_PROP_NAME_MAX 128
#define XW_STORED_PROP_TYPE_MAX 32
#define XW_STORED_PROP_VALUE_MAX 1024
/* Sanity bound for a corrupt/hostile sidecar; real wallpapers stay well
 * below this (the largest known WE projects have a few hundred). */
#define XW_PROPS_STORE_MAX 4096

typedef struct {
  char name[XW_STORED_PROP_NAME_MAX];
  char type[XW_STORED_PROP_TYPE_MAX];
  char value[XW_STORED_PROP_VALUE_MAX];
} XWStoredProp;

/* Full sidecar path for a wallpaper id. Returns 0, -1 on bad input. */
int xw_props_store_path(const char *wallpaper_id, char *out, size_t n);

/* Atomically save n props (tmp+rename, 0600). n<=0 or props NULL deletes
 * the sidecar (clear). Returns 0, -1 on IO/format error. */
int xw_props_store_save(const char *wallpaper_id,
                        const XWStoredProp *props, int n);

/* Load sidecar into a malloc'd array in *out (caller frees). Returns 0 with
 * *n set, 1 when no sidecar exists, -1 on bad input, corrupt file, or more
 * than XW_PROPS_STORE_MAX entries (caller treats as absent). */
int xw_props_store_load(const char *wallpaper_id,
                        XWStoredProp **out, int *n);

/* Per-wallpaper localhost HTTP port sidecar (pins the loopback origin so
 * WebKit storage survives restarts). save is atomic (tmp+rename, 0600);
 * load returns the port, or -1 when absent/corrupt. */
int xw_props_port_save(const char *wallpaper_id, int port);
int xw_props_port_load(const char *wallpaper_id);

/* One-shot file:// -> http localStorage migration marker. done() is 1
 * once the migration ran for this wallpaper; set() records it. */
int xw_props_migrated_done(const char *wallpaper_id);
int xw_props_migrated_set(const char *wallpaper_id);

#endif /* XW_PROPS_STORE_H */
