#ifndef XW_SCENE_MODEL_H
#define XW_SCENE_MODEL_H

#define XW_SCENE_ID_MAX 64
#define XW_SCENE_PATH_MAX 256
#define XW_SCENE_COMBOS_MAX 8
#define XW_SCENE_BINDS_MAX 8

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
} XWScenePass;

typedef struct XWSceneEffect {
  char name[XW_SCENE_ID_MAX];
  XWScenePass *passes;
  int n_passes;
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
  XW_DYN_CONSTANT = 0,
  XW_DYN_USER,
  XW_DYN_EXPRESSION,
} XWDynKind;

typedef struct XWDynFloat {
  XWDynKind kind;
  float constant;
  char ref[128];
} XWDynFloat;

typedef struct XWParticleCfg {
  float rate;
  int max_count;
  char sprite[XW_SCENE_PATH_MAX];
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
  char model[XW_SCENE_PATH_MAX];
  char material[XW_SCENE_PATH_MAX];
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
