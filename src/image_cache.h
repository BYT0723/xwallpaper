#ifndef XW_IMAGE_CACHE_H
#define XW_IMAGE_CACHE_H

/* Refcounted cache of decoded images, shared by every image window.
 * Key = real path + mtime + size: a window that shows the same file another
 * window already decoded reuses that image instead of decoding it again, and
 * a file rewritten on disk becomes a new entry while the old image stays
 * valid for whoever is still painting it.
 *
 * Decoding stays with the caller through the ops callbacks, so this module
 * needs neither imlib2 nor X and the thin unit test can count loads with a
 * fake loader. One instance per process; calls happen on the daemon main
 * thread only. */

typedef struct XWImageCache XWImageCache;
typedef struct XWImageRef XWImageRef;

typedef struct {
  void *(*load)(void *ud, const char *path); /* decoded image, NULL on error */
  void (*free)(void *ud, void *img);         /* release one decoded image */
  void *ud;
} XWImageCacheOps;

XWImageCache *xw_image_cache_new(const XWImageCacheOps *ops);
void xw_image_cache_free(XWImageCache *c);

/* Take one reference to path's decoded image; NULL when the file is missing
 * or the loader fails. Every non-NULL result must be passed to put. */
XWImageRef *xw_image_cache_get(XWImageCache *c, const char *path);
void xw_image_cache_put(XWImageCache *c, XWImageRef *r);
void *xw_image_ref_image(const XWImageRef *r);

/* Diagnostics: live entries, loader calls so far, refs held on path. */
int xw_image_cache_entries(const XWImageCache *c);
int xw_image_cache_loads(const XWImageCache *c);
int xw_image_cache_refs(const XWImageCache *c, const char *path);

#endif /* XW_IMAGE_CACHE_H */
