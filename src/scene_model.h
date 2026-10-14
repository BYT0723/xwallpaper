#ifndef XW_SCENE_MODEL_H
#define XW_SCENE_MODEL_H

#define XW_SCENE_ID_MAX 64
/* most dependencies an object may declare (the engine's scenes use a handful) */
#define XW_SCENE_DEPS_MAX 16
#define XW_SCENE_PATH_MAX 256
#define XW_SCENE_COMBOS_MAX 8
#define XW_SCENE_BINDS_MAX 8
#define XW_SCENE_CONSTS_MAX 12
#define XW_SCENE_PASS_TEX_MAX 4
#define XW_PARTICLE_TEX_MAX 4

typedef struct XWSceneConst {
  char key[32];
  char value[64];
} XWSceneConst;

typedef enum {
  XW_BLEND_NORMAL = 0,
  XW_BLEND_TRANSLUCENT,
  XW_BLEND_ADDITIVE,
} XWBlend;

typedef struct XWSceneCombo {
  char key[32];
  char value[32];
} XWSceneCombo;

typedef struct XWSceneBind {
  char name[64];
  int slot;
} XWSceneBind;

typedef struct XWScenePass {
  char shader[XW_SCENE_PATH_MAX];
  XWSceneCombo combos[XW_SCENE_COMBOS_MAX];
  int n_combos;
  XWSceneBind binds[XW_SCENE_BINDS_MAX];
  int n_binds;
  /* real workshop passes: constants + texture refs from the scene doc,
   * shader file resolved through the effect.json/material chain */
  XWSceneConst consts[XW_SCENE_CONSTS_MAX];
  int n_consts;
  char textures[XW_SCENE_PASS_TEX_MAX][XW_SCENE_PATH_MAX];
  int n_textures;
  /* render-target name when the pass writes offscreen ("_rt_...") */
  char target[XW_SCENE_PATH_MAX];
} XWScenePass;

#define XW_SCENE_FBOS_MAX 8

/* Effect-declared offscreen buffer ("fbos" in effect.json): the pass
 * targets above resolve to these by name. */
typedef struct XWSceneFbo {
  char name[XW_SCENE_ID_MAX];
  int width, height;
  char format[16];
  char uvs[16];
} XWSceneFbo;

typedef struct XWSceneEffect {
  char name[XW_SCENE_ID_MAX];
  XWScenePass *passes;
  int n_passes;
  int visible; /* effects can be toggled off per instance */
  XWSceneFbo fbos[XW_SCENE_FBOS_MAX];
  int n_fbos;
} XWSceneEffect;

/* Object kinds. Explicit "type" wins; otherwise inferred from payload:
 * "text" -> TEXT, "sound" -> SOUND, "emitter" -> PARTICLE, else IMAGE. */
typedef enum {
  XW_OBJ_IMAGE = 0,
  XW_OBJ_PARTICLE,
  XW_OBJ_TEXT,
  XW_OBJ_SOUND,
} XWObjType;

/* Animatable float: number -> CONSTANT, {"user": name} -> USER,
 * {"expression": src} -> EXPRESSION (ref holds the source). */
typedef enum {
  XW_DYN_UNSET = 0, /* a zero-initialised struct means "no override", so the
                     * neutral value (1.0) applies instead of 0 */
  XW_DYN_CONSTANT,
  XW_DYN_USER,
  XW_DYN_EXPRESSION,
} XWDynKind;

typedef struct XWDynFloat {
  XWDynKind kind;
  float constant;
  char ref[128];
} XWDynFloat;

/* oscillateposition / oscillatealpha / oscillatesize operator parameters.
 * Defaults mirror ObjectParser::parseOperator in the engine. */
typedef struct XWOscillate {
  int present;
  float freq_min, freq_max;
  float scale_min, scale_max;
  float phase_min, phase_max;
  float mask[3]; /* oscillateposition only; the engine defaults to (1,1,0) */
} XWOscillate;

typedef struct XWParticleCfg {
  float rate;  int max_count;
  char sprite[XW_SCENE_PATH_MAX];
  float life;
  float speed;
  float spread;
  float size_min;
  float size_max;
  float color[4];
  float gravity;
  /* real workshop instanceoverride multipliers (default constant 1) */
  XWDynFloat count_mult;
  XWDynFloat life_mult;
  XWDynFloat speed_mult;
  XWDynFloat size_mult;
  /* faithful emitter/operator data (engine space: centered pixels, Y up) */
  float origin[3];    /* emitter origin, Y flipped at spawn */
  float dist_min[3];  /* spawn box inner extent */
  float dist_max[3];  /* spawn box outer extent */
  float dirs[3];      /* per-axis multiplier (directions, Y flipped) */
  int emitter_sphere; /* "sphererandom" (disk/shell) instead of "boxrandom" */
  float sign[3];      /* per-axis sign force: 0 both, 1 positive, -1 negative */
  float vel_min[3];   /* velocityrandom min (Y flipped at spawn) */
  float vel_max[3];   /* velocityrandom max */
  float gravity_v[3]; /* movement gravity (Y flipped) */
  float drag;         /* movement drag */
  /* particle material pass (the engine runs it per particle quad, e.g.
   * the built-in genericparticle shader) */
  char pshader[XW_SCENE_PATH_MAX];
  char ptextures[XW_PARTICLE_TEX_MAX][XW_SCENE_PATH_MAX];
  int n_ptextures;
  XWSceneCombo pcombos[XW_SCENE_COMBOS_MAX];
  int n_pcombos;
  XWSceneConst pconsts[XW_SCENE_CONSTS_MAX];
  int n_pconsts;
  XWBlend pblend;
  int flags;          /* engine particle flags; bit 2 = perspective */
  char renderer[32];  /* "sprite" | "spritetrail" | "rope" ... */
  float trail_max;    /* renderer maxlength in pixels (trail length) */
  /* spritetrail renders through genericparticle's TRAILRENDERER path: the
   * shader stretches the quad along the velocity, scaled by `length` and
   * clamped to maxlength/minlength */
  int has_trail;
  float trail_length;
  float trail_min;
  char animation_mode[24]; /* "sequence" | "" (sprite-sheet animation) */
  float seq_mult;          /* sequencemultiplier: sheet animation speed */
  float size_exponent;     /* sizerandom exponent (1 = linear) */
  /* alpharandom / colorrandom / rotationrandom ranges (engine defaults) */
  float alpha_min, alpha_max;
  float color_min[3], color_max[3];
  float rot_min, rot_max;
  int has_color_random;
  /* turbulentvelocityrandom: initial velocity comes from a curl-noise field
   * (see scene_particle.c), shaped by these parameters */
  int has_turb;
  float turb_speed_min, turb_speed_max;
  float turb_offset;     /* radians, tilts the result around `right` */
  float turb_scale;      /* < 2 limits the deviation from `forward` */
  float turb_time_scale; /* shifts the field over time */
  float turb_phase_min, turb_phase_max;
  float turb_forward[3], turb_right[3]; /* field basis, JSON space (Y up) */
  int has_alpha_fade;      /* alphafade operator present */
  float fade_in, fade_out; /* alphafade fadeintime / fadeouttime (0..1) */
  /* oscillator operators (each runs at most once, like the engine's single
   * per-particle oscillator state) */
  XWOscillate osc_pos, osc_alpha, osc_size;
  /* angularmovement operator + angularvelocityrandom initializer (the
   * engine works on a vec3; only the in-plane z spin reaches our quad) */
  int has_angular_movement;
  float am_drag, am_force_z;
  int has_av_random;
  float av_min, av_max, av_exponent;
} XWParticleCfg;

typedef struct XWTextCfg {
  char content[256];
  char font[XW_SCENE_PATH_MAX];
  float size;
  float color[4];
} XWTextCfg;

typedef struct XWSoundCfg {
  char file[XW_SCENE_PATH_MAX];
  float volume;
  int loop;
} XWSoundCfg;

typedef struct XWSceneObject {
  char id[XW_SCENE_ID_MAX];
  XWObjType type;
  /* `dependencies`: ids this object must be drawn after (the engine emits a
   * DFS post-order over them); stored as strings, matching `id` */
  char deps[XW_SCENE_DEPS_MAX][XW_SCENE_ID_MAX];
  int n_deps;
  char model[XW_SCENE_PATH_MAX];
  char material[XW_SCENE_PATH_MAX];
  char texture[XW_SCENE_PATH_MAX]; /* image base texture, "" = none */
  char mat_tex[XW_SCENE_PATH_MAX]; /* material texture ref (real walls) */
  XWBlend blend;                   /* material blending (real walls) */
  float origin[2];
  float scale[2];
  float size[2];
  int visible;
  XWDynFloat alpha;
  XWParticleCfg particle;
  XWTextCfg text;
  XWSoundCfg sound;
  XWSceneEffect *effects;
  int n_effects;
} XWSceneObject;

typedef struct XWSceneCamera {
  float ortho_w;
  float ortho_h;
  float clearcolor[3];
  int bloom;
  int parallax;
  /* perspective parameters, used by particles whose flags have bit 2 set */
  float fov;
  float nearz;
  float farz;
} XWSceneCamera;

typedef struct XWUserProp {
  char name[64];
  char type[16];
  char value[256];
} XWUserProp;

typedef struct XWScene {
  XWSceneCamera camera;
  char script[XW_SCENE_PATH_MAX];
  XWUserProp *props;
  int n_props;
  XWSceneObject *objects;
  int n_objects;
} XWScene;

#endif
