#ifndef XW_SCENE_SCRIPT_H
#define XW_SCENE_SCRIPT_H

#include <stdint.h>

#include "xw_err.h"

/* QuickJS wallpaper scripting (daemon-only).
 * Task 12 scope: runtime, script eval, timeline timers pumped by tick.
 * Object/user-property/mouse bindings land in Task 13. */
typedef struct XWScript XWScript;

int xw_script_create(XWScript **out, XWError *err);
void xw_script_destroy(XWScript *s);

/* Evaluate a wallpaper script. 0 ok, -1 with the JS exception in err. */
int xw_script_eval(XWScript *s, const char *src, const char *filename,
                   XWError *err);

/* Read a global number (tests/bindings). 0 ok, -1 missing/not-a-number. */
int xw_script_get_number(XWScript *s, const char *name, double *out);

/* Fire due timers and drain promise jobs. */
void xw_script_tick(XWScript *s);

/* ms since engine creation, plus any setTime adjustment. */
uint64_t xw_script_now_ms(const XWScript *s);

/* Last timer-callback exception text ("" when none). */
const char *xw_script_last_error(const XWScript *s);

#endif
