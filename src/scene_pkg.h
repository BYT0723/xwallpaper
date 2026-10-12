#ifndef XW_SCENE_PKG_H
#define XW_SCENE_PKG_H

#include <stddef.h>

typedef struct XWPkg XWPkg;

int xw_pkg_open(const char *path, XWPkg **out);
void xw_pkg_close(XWPkg *p);
int xw_pkg_count(const XWPkg *p);
int xw_pkg_name(const XWPkg *p, int idx, char *buf, size_t n);
int xw_pkg_find(const XWPkg *p, const char *name);
int xw_pkg_read(const XWPkg *p, int idx, unsigned char **out, size_t *len);

#endif
