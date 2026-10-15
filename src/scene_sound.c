#include <stdlib.h>

#ifdef XW_HAVE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#endif

#include "scene_sound.h"

int xw_sound_mix_volume(float obj_vol, double global_vol) {
#ifdef XW_HAVE_SDL_MIXER
  const float maxv = (float)MIX_MAX_VOLUME;
#else
  const float maxv = 128.0f;
#endif
  if (obj_vol < 0.0f)
    obj_vol = 0.0f;
  if (obj_vol > 1.0f)
    obj_vol = 1.0f;
  if (global_vol < 0.0)
    global_vol = 0.0;
  if (global_vol > 100.0)
    global_vol = 100.0;
  return (int)(obj_vol * (float)(global_vol / 100.0) * maxv + 0.5f);
}

#ifdef XW_HAVE_SDL_MIXER

struct XWSound {
  Mix_Chunk *chunk;
  int channel;
};

static int g_refs = 0;
static int g_opened = 0;

int xw_sound_sys_init(XWError *err) {
  if (g_refs > 0) {
    g_refs++;
    return 0;
  }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    XW_ERROR_SET(err, 1, "SDL audio: %.200s", SDL_GetError());
    return -1;
  }
  if ((Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG) == 0) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    XW_ERROR_SET(err, 1, "mixer ogg: %.200s", Mix_GetError());
    return -1;
  }
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    XW_ERROR_SET(err, 1, "open audio: %.200s", Mix_GetError());
    return -1;
  }
  g_opened = 1;
  g_refs = 1;
  return 0;
}

void xw_sound_sys_free(void) {
  if (g_refs <= 0)
    return;
  if (--g_refs == 0 && g_opened) {
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_opened = 0;
  }
}

int xw_sound_load(struct XWSound **out, const char *path, XWError *err) {
  struct XWSound *s;
  Mix_Chunk *c;
  if (!out || !path || !path[0]) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  *out = NULL;
  if (!g_opened) {
    XW_ERROR_SET(err, 1, "sound system not initialized");
    return -1;
  }
  c = Mix_LoadWAV(path);
  if (!c) {
    XW_ERROR_SET(err, 1, "cannot load '%.60s': %.150s", path, Mix_GetError());
    return -1;
  }
  s = calloc(1, sizeof(*s));
  if (!s) {
    Mix_FreeChunk(c);
    XW_ERROR_SET(err, 1, "out of memory");
    return -1;
  }
  s->chunk = c;
  s->channel = -1;
  *out = s;
  return 0;
}

int xw_sound_play(struct XWSound *s, float obj_vol, double global_vol,
                  int loop, XWError *err) {
  int ch;
  if (!s || !s->chunk) {
    XW_ERROR_SET(err, 1, "invalid args");
    return -1;
  }
  ch = Mix_PlayChannel(-1, s->chunk, loop ? -1 : 0);
  if (ch < 0) {
    XW_ERROR_SET(err, 1, "play: %.200s", Mix_GetError());
    return -1;
  }
  s->channel = ch;
  Mix_Volume(ch, xw_sound_mix_volume(obj_vol, global_vol));
  return 0;
}

int xw_sound_set_volumes(struct XWSound *s, float obj_vol, double global_vol) {
  if (!s)
    return -1;
  if (s->channel >= 0)
    Mix_Volume(s->channel, xw_sound_mix_volume(obj_vol, global_vol));
  return 0;
}

int xw_sound_pause(struct XWSound *s, int paused) {
  if (!s || s->channel < 0)
    return -1;
  if (paused)
    Mix_Pause(s->channel);
  else
    Mix_Resume(s->channel);
  return 0;
}

void xw_sound_stop(struct XWSound *s) {
  if (!s)
    return;
  if (s->channel >= 0) {
    Mix_HaltChannel(s->channel);
    s->channel = -1;
  }
}

void xw_sound_free(struct XWSound *s) {
  if (!s)
    return;
  xw_sound_stop(s);
  Mix_FreeChunk(s->chunk);
  free(s);
}

int xw_sound_playing(const struct XWSound *s) {
  if (!s || s->channel < 0)
    return 0;
  return Mix_Playing(s->channel) ? 1 : 0;
}

#else /* !XW_HAVE_SDL_MIXER */

struct XWSound {
  int unused;
};

int xw_sound_sys_init(XWError *err) {
  XW_ERROR_SET(err, 1, "scene sound not compiled in (need SDL2_mixer)");
  return -1;
}

void xw_sound_sys_free(void) {}

int xw_sound_load(struct XWSound **out, const char *path, XWError *err) {
  (void)out;
  (void)path;
  XW_ERROR_SET(err, 1, "scene sound not compiled in (need SDL2_mixer)");
  return -1;
}

int xw_sound_play(struct XWSound *s, float obj_vol, double global_vol,
                  int loop, XWError *err) {
  (void)s;
  (void)obj_vol;
  (void)global_vol;
  (void)loop;
  XW_ERROR_SET(err, 1, "scene sound not compiled in (need SDL2_mixer)");
  return -1;
}

int xw_sound_set_volumes(struct XWSound *s, float obj_vol, double global_vol) {
  (void)s;
  (void)obj_vol;
  (void)global_vol;
  return -1;
}

int xw_sound_pause(struct XWSound *s, int paused) {
  (void)s;
  (void)paused;
  return -1;
}

void xw_sound_stop(struct XWSound *s) { (void)s; }

void xw_sound_free(struct XWSound *s) { (void)s; }

int xw_sound_playing(const struct XWSound *s) {
  (void)s;
  return 0;
}

#endif /* XW_HAVE_SDL_MIXER */
