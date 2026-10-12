#ifndef XW_SCENE_SOUND_H
#define XW_SCENE_SOUND_H

#include "xw_err.h"

/* Sound-object playback (daemon-only, SDL2_mixer chunk per object).
 * Device failures degrade: init succeeds with empty slots and the
 * wallpaper runs mute. Mixer headers never leak here. */
struct XWSound;

/* Refcounted SDL audio + mixer init (OGG decoder). 0 ok, -1 no device. */
int xw_sound_sys_init(XWError *err);
void xw_sound_sys_free(void);

/* Load a file into a chunk (wav/ogg by content). Strict: missing file
 * or dead device fails. */
int xw_sound_load(struct XWSound **out, const char *path, XWError *err);

/* Play looped (loop!=0) or once. Volume = object 0..1 times global 0..100. */
int xw_sound_play(struct XWSound *s, float obj_vol, double global_vol,
                  int loop, XWError *err);
int xw_sound_set_volumes(struct XWSound *s, float obj_vol, double global_vol);
int xw_sound_pause(struct XWSound *s, int paused);
void xw_sound_stop(struct XWSound *s);
void xw_sound_free(struct XWSound *s);
int xw_sound_playing(const struct XWSound *s);

/* Pure: object volume times global volume mapped to 0..MIX_MAX_VOLUME. */
int xw_sound_mix_volume(float obj_vol, double global_vol);

#endif
