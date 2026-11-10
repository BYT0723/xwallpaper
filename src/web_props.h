#ifndef XW_WEB_PROPS_H
#define XW_WEB_PROPS_H

#include <stddef.h>

/* One Wallpaper Engine user property from project.json general.properties. */
#define XW_WEB_PROP_VALUE_MAX 1024
/* Sanity bound for a hostile 1 MiB project.json; real wallpapers stay in
 * the hundreds. The array itself grows to the property count. */
#define XW_WEB_PROPS_MAX 4096
typedef struct {
  char name[128];
  char type[32];
  char value[XW_WEB_PROP_VALUE_MAX];
} XWWebProp;

/* Worst-case JS for one property: 127B name + 1024B value fully
 * \uXXXX-escaped (~7kB). Single-property callers size their buffer with
 * this; multi-property callers use xw_web_props_object_js_len(). */
#define XW_WEB_PROP_JS_MAX 8192

/* Parse project.json text, extract general.properties entries. Group
 * headings (entries without a "type") are skipped. On success *out is a
 * malloc'd array (caller frees) and the count is returned; *out is NULL
 * when there are no properties. Returns -1 on bad input, allocation
 * failure, or more than XW_WEB_PROPS_MAX entries. */
int xw_web_props_parse(const char *json, size_t len, XWWebProp **out);

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

/* Required buffer size for xw_web_props_object_js(), including the trailing
 * NUL. Returns 0 for invalid input (NULL/n<=0 or size overflow). */
size_t xw_web_props_object_js_len(const XWWebProp *props, int n);

#endif
