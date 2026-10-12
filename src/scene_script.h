#ifndef XW_SCENE_SCRIPT_H
#define XW_SCENE_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "scene_model.h"
#include "xw_err.h"

/* QuickJS wallpaper scripting (daemon-only).
 * Task 12 scope: runtime, script eval, timeline timers pumped by tick.
 * Task 13 scope: scene object read/write, user props, mouse input.
 * Render application of resolved values is Task 14. */
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

/* Borrow the live scene and parsed props (NULL clears; backend owns both).
 * Overrides set via scene.setProperty shadow parsed props. */
int xw_script_set_scene(XWScript *s, XWScene *scene);
int xw_script_set_props(XWScript *s, const XWUserProp *props, int n);

/* Mouse position in window pixels (backend feeds from X events). */
void xw_script_set_mouse(XWScript *s, float x, float y);

/* Override (or add) a user prop value. Get checks overrides first. */
int xw_script_set_user_prop(XWScript *s, const char *name, const char *value);
int xw_script_get_user_prop(XWScript *s, const char *name, char *value,
                            size_t n);

/* Resolve an object's alpha: USER refs read props/overrides (fallback to
 * the parsed constant), EXPRESSION is a constant until Task 14. */
float xw_script_resolve_alpha(XWScript *s, const XWSceneObject *o);

#endif
