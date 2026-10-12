#ifndef XW_SCENE_H
#define XW_SCENE_H

#include "backend.h"

/* Default frame rate when opt->fps is 0 (auto). */
#define XW_SCENE_FPS_DEFAULT 30.0

/* Painted-frame counter (0 when uninitialized). For tests/debug. */
unsigned long xw_scene_frames(const XWBackend *b);

/* Parsed object count (-1 when uninitialized). */
int xw_scene_object_count(const XWBackend *b);

/* Whether the GL renderer is active (0 = clear-color fallback). */
int xw_scene_has_gl(const XWBackend *b);

#endif /* XW_SCENE_H */
