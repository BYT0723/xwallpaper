#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "image_cache.h"

/* Bound on distinct live images. Image windows are few, so this only stops a
 * bug from pinning an unbounded number; past it the image is still handed out
 * but not indexed (put() frees it either way). */
#define XW_IMAGE_CACHE_MAX 64

typedef struct {
  char *key; /* realpath, owned */
  struct timespec mtime;
  long long size;
  void *img;
  int refs;
} XWImageEntry;

struct XWImageCache {
  XWImageCacheOps ops;
  XWImageEntry **v;
  int n;
  int loads;
};

struct XWImageRef {
  XWImageCache *cache;
  XWImageEntry *e;
};

static int same_key(const XWImageEntry *e, const char *key,
                    const struct stat *st) {
  return strcmp(e->key, key) == 0 && e->mtime.tv_sec == st->st_mtim.tv_sec &&
         e->mtime.tv_nsec == st->st_mtim.tv_nsec &&
         e->size == (long long)st->st_size;
}

static XWImageEntry *find_entry(const XWImageCache *c, const char *key,
                                const struct stat *st) {
  for (int i = 0; i < c->n; i++)
    if (same_key(c->v[i], key, st))
      return c->v[i];
  return NULL;
}

static int resolve(const char *path, char **key, struct stat *st) {
  char *real = realpath(path, NULL);
  if (!real)
    return -1;
  if (stat(real, st) != 0 || !S_ISREG(st->st_mode)) {
    free(real);
    return -1;
  }
  *key = real;
  return 0;
}

XWImageCache *xw_image_cache_new(const XWImageCacheOps *ops) {
  XWImageCache *c;
  if (!ops || !ops->load || !ops->free)
    return NULL;
  c = calloc(1, sizeof(*c));
  if (!c)
    return NULL;
  c->ops = *ops;
  return c;
}

void xw_image_cache_free(XWImageCache *c) {
  if (!c)
    return;
  for (int i = 0; i < c->n; i++) {
    c->ops.free(c->ops.ud, c->v[i]->img);
    free(c->v[i]->key);
    free(c->v[i]);
  }
  free(c->v);
  free(c);
}

XWImageRef *xw_image_cache_get(XWImageCache *c, const char *path) {
  XWImageEntry *e;
  XWImageRef *r;
  char *key = NULL;
  struct stat st;

  if (!c || !path || !path[0])
    return NULL;
  if (resolve(path, &key, &st) != 0)
    return NULL;
  e = find_entry(c, key, &st);
  if (e) {
    e->refs++;
    free(key);
  } else {
    void *img = c->ops.load(c->ops.ud, key);
    c->loads++;
    if (!img) {
      free(key);
      return NULL;
    }
    e = calloc(1, sizeof(*e));
    if (!e) {
      c->ops.free(c->ops.ud, img);
      free(key);
      return NULL;
    }
    e->key = key;
    e->mtime = st.st_mtim;
    e->size = (long long)st.st_size;
    e->img = img;
    e->refs = 1;
    if (c->n < XW_IMAGE_CACHE_MAX) {
      XWImageEntry **grown =
          realloc(c->v, sizeof(*grown) * (size_t)(c->n + 1));
      if (grown) {
        c->v = grown;
        c->v[c->n++] = e;
      }
    }
  }
  r = malloc(sizeof(*r));
  if (!r) {
    if (--e->refs == 0) {
      for (int i = 0; i < c->n; i++)
        if (c->v[i] == e) {
          c->v[i] = c->v[--c->n];
          break;
        }
      c->ops.free(c->ops.ud, e->img);
      free(e->key);
      free(e);
    }
    return NULL;
  }
  r->cache = c;
  r->e = e;
  return r;
}

void xw_image_cache_put(XWImageCache *c, XWImageRef *r) {
  XWImageEntry *e;
  XWImageCache *cache;
  if (!r)
    return;
  cache = r->cache ? r->cache : c;
  e = r->e;
  if (--e->refs > 0) {
    free(r);
    return;
  }
  if (cache) {
    for (int i = 0; i < cache->n; i++)
      if (cache->v[i] == e) {
        cache->v[i] = cache->v[--cache->n];
        break;
      }
  }
  cache->ops.free(cache->ops.ud, e->img);
  free(e->key);
  free(e);
  free(r);
}

void *xw_image_ref_image(const XWImageRef *r) { return r ? r->e->img : NULL; }

int xw_image_cache_entries(const XWImageCache *c) { return c ? c->n : 0; }

int xw_image_cache_loads(const XWImageCache *c) { return c ? c->loads : 0; }

int xw_image_cache_refs(const XWImageCache *c, const char *path) {
  char *key = NULL;
  struct stat st;
  XWImageEntry *e;
  if (!c || !path || !path[0])
    return 0;
  if (resolve(path, &key, &st) != 0)
    return 0;
  e = find_entry(c, key, &st);
  free(key);
  return e ? e->refs : 0;
}
