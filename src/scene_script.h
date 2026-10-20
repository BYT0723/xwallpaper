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

/* engine.runtime / engine.frametime for this frame, in seconds. */
void xw_script_set_frame(XWScript *s, double runtime_s, double frametime_s);

/* Mouse position in window pixels (backend feeds from X events). */
void xw_script_set_mouse(XWScript *s, float x, float y);

/* Override (or add) a user prop value. Get checks overrides first. */
int xw_script_set_user_prop(XWScript *s, const char *name, const char *value);
int xw_script_get_user_prop(XWScript *s, const char *name, char *value,
                            size_t n);
/* Drop one override (0 ok, -1 when missing). Used by clear-props. */
int xw_script_clear_user_prop(XWScript *s, const char *name);
/* Snapshot all overrides with their parsed type (for disk persistence).
 * Returns 0 with *n set, -1 on bad input or too-small cap. */
int xw_script_list_overrides(XWScript *s, XWUserProp *out, int cap, int *n);

/* Resolve an object's alpha: USER refs read props/overrides (fallback to
 * the parsed constant), EXPRESSION is a constant until Task 14. */
float xw_script_resolve_alpha(XWScript *s, const XWSceneObject *o);

/* A text layer's script. Wallpaper Engine runs the script's `update(value)`
 * every frame and uses the returned string as the layer's text; the script
 * may declare its own properties through createScriptProperties(). */
typedef struct XWTextLayer XWTextLayer;

/* text a scripted layer may produce (the engine's layers are short strings) */
#define XW_TEXT_LAYER_MAX 256

int xw_script_text_layer_create(XWTextLayer **out, const char *src,
                                const char *initial, XWError *err);
/* Run update() and return the layer's current text (never NULL). */
const char *xw_script_text_layer_update(XWTextLayer *l);
void xw_script_text_layer_free(XWTextLayer *l);

/* A scripted object property (origin / scale / size): the engine runs the
 * script's update(value) every frame and keeps the value it returns. */
typedef struct XWPropScript XWPropScript;

int xw_script_prop_create(XWPropScript **out, const char *src, XWError *err);

/* Hand a property script the scene (which owns the render order it may
 * reorder through thisScene.sortLayer) and the index of the object it belongs
 * to (thisLayer). Installs the thisScene/thisLayer bindings and runs init()
 * once, matching ScriptEngine's callHook("_init"). */
void xw_script_prop_bind(XWPropScript *p, XWScene *scene, int owner);

/* Run update() with `in`; fills out3 and returns 0, or returns -1 when the
 * script throws or returns nothing (the caller then keeps its value). */
int xw_script_prop_update(XWPropScript *p, const float in[3], float out3[3]);
/* Boolean-valued properties (visible) hand update() a plain boolean. */
int xw_script_prop_update_bool(XWPropScript *p, int in, int *out);
void xw_script_prop_free(XWPropScript *p);

/* Run an object's scripted transform properties for this frame (origin,
 * scale, size, visible). Scripts that throw or declare no update() leave the
 * parsed value alone. */
void xw_script_apply_object_props(XWScript *s, XWSceneObject *o);

/* Read a string global (a missing/non-string name returns -1). */
int xw_script_get_string(XWScript *s, const char *name, char *out, size_t n);

#endif
