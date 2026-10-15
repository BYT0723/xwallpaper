#ifndef XW_KEYBINDS_H
#define XW_KEYBINDS_H

#include <stddef.h>

/* mpv input.conf subset used by wallpaper.key files. Pure logic: no X11,
 * GTK or mpv dependency, so it stays unit-testable without heavy libs. */

typedef enum {
  XW_ACT_NONE = 0,
  XW_ACT_SEEK,   /* seek <seconds>              */
  XW_ACT_VOLUME, /* add volume <delta>          */
  XW_ACT_MUTE,   /* cycle mute                  */
  XW_ACT_PAUSE,  /* cycle pause                 */
  XW_ACT_ROTATE  /* cycle_values video-rotate v */
} XWActKind;

#define XW_BIND_KEY_MAX 32
#define XW_BIND_ROTATE_MAX 8
#define XW_KEYBINDS_MAX 64
#define XW_KEYBINDS_LINE_MAX 512

typedef struct {
  char key[XW_BIND_KEY_MAX];
  XWActKind kind;
  double seek_secs;
  int volume_delta;
  int rotate[XW_BIND_ROTATE_MAX];
  int n_rotate;
} XWBind;

/* Parse text into binds. Malformed or unknown lines are skipped, never fatal.
 * Returns the number of binds written, or -1 on bad arguments. */
int xw_keybinds_parse(const char *text, XWBind *out, int max);

/* Look up a key by its normalised spelling; NULL when absent. */
const XWBind *xw_keybinds_find(const XWBind *binds, int n, const char *key);

/* Normalise an X11 keysym name or a config spelling to the key used in the
 * file: single ASCII letters lower-cased, common keysym names mapped to the
 * character they produce, everything else (XF86*, Ctrl+/Alt+/Super+ forms)
 * kept verbatim. Returns 0, or -1 on empty/NULL/too long. */
int xw_keybinds_norm_key(const char *in, char *out, size_t n);

/* Relative ("+2"/"-2") or absolute ("50") volume against the current value,
 * clamped to 0..100. Returns 0, or -1 for NULL/bad arguments or bad input. */
int xw_audio_apply(int cur, const char *value, int *out);

/* Next entry of a value ring: the item after cur, wrapping, or the first item
 * when cur is not in the list. Returns 0, or -1 on bad arguments. */
int xw_values_cycle(const int *vals, int n, int cur, int *out);

#endif /* XW_KEYBINDS_H */
