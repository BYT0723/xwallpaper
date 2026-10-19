#ifndef XW_WEB_PROPS_H
#define XW_WEB_PROPS_H

#include <stddef.h>

/* One Wallpaper Engine user property from project.json general.properties. */
typedef struct {
  char name[128];
  char type[32];
  char value[1024];
} XWWebProp;

/* Worst-case JS for one property: 127B name + 1024B value fully
 * \uXXXX-escaped (~7kB). Sizes every object_js/apply_js buffer. */
#define XW_WEB_PROP_JS_MAX 8192

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

/* Normalize a user-supplied value to WE form. color props accept the native
 * "r g b" triple (passed through) and #rrggbb/#rgb (converted); anything
 * else is rejected so bad input fails loudly instead of being silently
 * ignored by the page. Other types pass through untouched.
 * Returns 0 with the result in out, or -1 on bad input. */
int xw_web_props_normalize(const char *type, const char *value,
                            char *out, size_t m);

/* Build just the properties object literal: {"name":{"value":"..."},...}.
 * Used to sequence overrides after the page's own project.json fetch.
 * Returns 0, or -1 on bad input or a too-small buffer. */
int xw_web_props_object_js(const XWWebProp *props, int n,
                            char *out, size_t m);

#endif
