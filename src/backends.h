#ifndef XW_BACKENDS_H
#define XW_BACKENDS_H

#include "backend.h"

/* Backend constructors. Each returns a static XWBackend template; app copies
 * it per window instance. */
XWBackend *xw_image_backend_new(void);
XWBackend *xw_video_backend_new(void);
XWBackend *xw_web_backend_new(void);
XWBackend *xw_scene_backend_new(void);

#endif /* XW_BACKENDS_H */
