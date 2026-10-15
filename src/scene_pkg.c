#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "scene_pkg.h"

/* Real Wallpaper Engine PKGV layout (scene.pkg), reverse-engineered from
 * workshop files (e.g. 2178468632, 25MB, 41 entries):
 *   u32 ignored | "PKGV" | 4-char version ("0008") | u32 file count
 *   per entry: u32 name len | name | u32 data offset | u32 data size
 * Data offsets are relative to the end of the file table. Bounds-checked
 * throughout; anything off fails closed. */
typedef struct {
  char *name;
  uint32_t off;
  uint32_t size;
} XWEntry;

struct XWPkg {
  unsigned char *buf;
  size_t len;
  size_t base;
  XWEntry *entries;
  int count;
};

static uint32_t rd32le(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

int xw_pkg_open(const char *path, XWPkg **out) {
  FILE *f;
  long sz;
  unsigned char *buf;
  uint32_t n, i;
  size_t pos;
  XWEntry *ents;
  XWPkg *p;
  if (!path || !out)
    return -1;
  *out = NULL;
  f = fopen(path, "rb");
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  sz = ftell(f);
  if (sz < 16) {
    fclose(f);
    return -1;
  }
  rewind(f);
  buf = malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return -1;
  }
  fclose(f);
  if (memcmp(buf + 4, "PKGV", 4) != 0) {
    free(buf);
    return -1;
  }
  n = rd32le(buf + 12);
  if (n == 0 || n > 100000) {
    free(buf);
    return -1;
  }
  ents = calloc(n, sizeof(XWEntry));
  if (!ents) {
    free(buf);
    return -1;
  }
  pos = 16;
  for (i = 0; i < n; i++) {
    uint32_t nl, off, size;
    char *nm;
    if (pos + 4 > (size_t)sz)
      goto fail;
    nl = rd32le(buf + pos);
    if (nl == 0 || nl > 1024 || pos + 4 + nl + 8 > (size_t)sz)
      goto fail;
    nm = malloc(nl + 1);
    if (!nm)
      goto fail;
    memcpy(nm, buf + pos + 4, nl);
    nm[nl] = '\0';
    off = rd32le(buf + pos + 4 + nl);
    size = rd32le(buf + pos + 4 + nl + 4);
    ents[i].name = nm;
    ents[i].off = off;
    ents[i].size = size;
    pos += 4 + nl + 8;
  }
  for (i = 0; i < n; i++) {
    if ((size_t)ents[i].off + ents[i].size > (size_t)sz - pos)
      goto fail;
  }
  p = malloc(sizeof(XWPkg));
  if (!p)
    goto fail;
  p->buf = buf;
  p->len = (size_t)sz;
  p->base = pos;
  p->entries = ents;
  p->count = (int)n;
  *out = p;
  return 0;
fail:
  for (uint32_t j = 0; j < n; j++) free(ents[j].name);
  free(ents);
  free(buf);
  return -1;
}

void xw_pkg_close(XWPkg *p) {
  int i;
  if (!p)
    return;
  for (i = 0; i < p->count; i++) free(p->entries[i].name);
  free(p->entries);
  free(p->buf);
  free(p);
}

int xw_pkg_count(const XWPkg *p) {
  if (!p)
    return 0;
  return p->count;
}

int xw_pkg_name(const XWPkg *p, int idx, char *buf, size_t n) {
  size_t need;
  if (!p || !buf || n == 0)
    return -1;
  if (idx < 0 || idx >= p->count)
    return -1;
  need = strlen(p->entries[idx].name) + 1;
  if (need > n)
    return -1;
  memcpy(buf, p->entries[idx].name, need);
  return 0;
}

int xw_pkg_find(const XWPkg *p, const char *name) {
  int i;
  if (!p || !name)
    return -1;
  for (i = 0; i < p->count; i++)
    if (strcmp(p->entries[i].name, name) == 0)
      return i;
  return -1;
}

int xw_pkg_read(const XWPkg *p, int idx, unsigned char **out, size_t *len) {
  unsigned char *b;
  size_t size, off;
  if (!p || idx < 0 || idx >= p->count)
    return -1;
  if (!out || !len)
    return -1;
  size = p->entries[idx].size;
  off = p->base + p->entries[idx].off;
  b = malloc(size ? size : 1);
  if (!b)
    return -1;
  memcpy(b, p->buf + off, size);
  *out = b;
  *len = size;
  return 0;
}

static int mkdir_parents(char *path) {
  /* mkdir every parent level of path (path itself is the file) */
  for (char *s = path + 1; *s; s++) {
    char save;
    if (*s != '/')
      continue;
    save = *s;
    *s = '\0';
    if (mkdir(path, 0755) != 0) {
      struct stat st;
      if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        *s = save;
        return -1;
      }
    }
    *s = save;
  }
  return 0;
}

int xw_pkg_extract_all(const XWPkg *p, const char *destdir) {
  int i;
  char path[2048];
  if (!p || !destdir || !destdir[0])
    return -1;
  for (i = 0; i < p->count; i++) {
    FILE *f;
    size_t off, size;
    int r;
    if (strstr(p->entries[i].name, "..") != NULL)
      return -1;
    r = snprintf(path, sizeof(path), "%s/%s", destdir,
                 p->entries[i].name);
    if (r < 0 || r >= (int)sizeof(path))
      return -1;
    if (mkdir_parents(path) != 0)
      return -1;
    off = p->base + p->entries[i].off;
    size = p->entries[i].size;
    f = fopen(path, "wb");
    if (!f)
      return -1;
    if (size > 0 && fwrite(p->buf + off, 1, size, f) != size) {
      fclose(f);
      return -1;
    }
    if (fclose(f) != 0)
      return -1;
  }
  return 0;
}
