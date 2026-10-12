#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene_pkg.h"

typedef struct {
  char *name;
  uint32_t off;
  uint32_t size;
} XWEntry;

struct XWPkg {
  unsigned char *buf;
  size_t len;
  XWEntry *entries;
  int count;
};

static uint32_t rd32le(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint16_t rd16le(const unsigned char *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

int xw_pkg_open(const char *path, XWPkg **out) {
  if (!path || !out) return -1;
  *out = NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long sz = ftell(f);
  if (sz < 8) {
    fclose(f);
    return -1;
  }
  rewind(f);
  unsigned char *buf = malloc((size_t)sz);
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
  if (memcmp(buf, "PKGV", 4) != 0) {
    free(buf);
    return -1;
  }
  uint32_t n = rd32le(buf + 4);
  if (n > 65536) {
    free(buf);
    return -1;
  }
  XWEntry *ents = calloc(n ? n : 1, sizeof(XWEntry));
  if (!ents) {
    free(buf);
    return -1;
  }
  size_t pos = 8;
  for (uint32_t i = 0; i < n; i++) {
    if (pos + 2 > (size_t)sz) goto fail;
    uint16_t nl = rd16le(buf + pos);
    pos += 2;
    if (nl == 0 || nl > 1024) goto fail;
    if (pos + nl + 8 > (size_t)sz) goto fail;
    char *nm = malloc(nl + 1);
    if (!nm) goto fail;
    memcpy(nm, buf + pos, nl);
    nm[nl] = '\0';
    pos += nl;
    uint32_t off = rd32le(buf + pos);
    uint32_t size = rd32le(buf + pos + 4);
    pos += 8;
    if ((size_t)off + size > (size_t)sz) {
      free(nm);
      goto fail;
    }
    ents[i].name = nm;
    ents[i].off = off;
    ents[i].size = size;
  }
  XWPkg *p = malloc(sizeof(XWPkg));
  if (!p) goto fail;
  p->buf = buf;
  p->len = (size_t)sz;
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
  if (!p) return;
  for (int i = 0; i < p->count; i++) free(p->entries[i].name);
  free(p->entries);
  free(p->buf);
  free(p);
}

int xw_pkg_count(const XWPkg *p) {
  if (!p) return 0;
  return p->count;
}

int xw_pkg_name(const XWPkg *p, int idx, char *buf, size_t n) {
  if (!p || !buf || n == 0) return -1;
  if (idx < 0 || idx >= p->count) return -1;
  size_t need = strlen(p->entries[idx].name) + 1;
  if (need > n) return -1;
  memcpy(buf, p->entries[idx].name, need);
  return 0;
}

int xw_pkg_find(const XWPkg *p, const char *name) {
  if (!p || !name) return -1;
  for (int i = 0; i < p->count; i++)
    if (strcmp(p->entries[i].name, name) == 0) return i;
  return -1;
}

int xw_pkg_read(const XWPkg *p, int idx, unsigned char **out, size_t *len) {
  if (!p || idx < 0 || idx >= p->count) return -1;
  if (!out || !len) return -1;
  uint32_t size = p->entries[idx].size;
  uint32_t off = p->entries[idx].off;
  unsigned char *b = malloc(size ? size : 1);
  if (!b) return -1;
  memcpy(b, p->buf + off, size);
  *out = b;
  *len = size;
  return 0;
}
