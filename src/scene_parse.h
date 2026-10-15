#ifndef XW_SCENE_PARSE_H
#define XW_SCENE_PARSE_H

#include "scene_model.h"
#include "xw_err.h"

int xw_scene_parse(const char *dir, XWScene *out, XWError *err);
void xw_scene_free(XWScene *s);

#endif
