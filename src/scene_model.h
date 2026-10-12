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

typedef struct XWSceneObject {
  char id[XW_SCENE_ID_MAX];
  char model[XW_SCENE_PATH_MAX];
  char material[XW_SCENE_PATH_MAX];
  float origin[2];
  float scale[2];
  float size[2];
  int visible;
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

typedef struct XWScene {
  XWSceneCamera camera;
  XWSceneObject *objects;
  int n_objects;
} XWScene;

#endif
