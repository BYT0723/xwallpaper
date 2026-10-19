#ifndef XW_WEB_PROPS_H
#define XW_WEB_PROPS_H

#include <stddef.h>

/* One Wallpaper Engine user property from project.json general.properties. */
typedef struct {
  char name[128];
  char type[32];
  char value[1024];
} XWWebProp;

/* Parse project.json text, extract general.properties entries.
 * Returns the number stored in out (cap limited), or -1 on bad input
 * or when more entries exist than cap. */
int xw_web_props_parse(const char *json, size_t len,
                        XWWebProp *out, int cap);

/* Build a JS statement calling
 * window.wallpaperPropertyListener.applyUserProperties({name:{value:"..."}})
 * for n props. Values are JSON-escaped. Returns 0, or -1 when n <= 0,
 * props/out is NULL, or the buffer is too small. */
int xw_web_props_apply_js(const XWWebProp *props, int n,
                           char *out, size_t m);

#endif
