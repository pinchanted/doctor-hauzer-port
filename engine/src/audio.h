/* Sound: effects (AIFF 8/16-bit PCM) and music (AIFC SDX2, stereo 44.1 kHz) mixed through SDL audio.
 * Mirrors the game's sound commands:
 *   SE n          fixed system effects (launchme table at 0x35598: footstep, door open/close, menu, item, scream)
 *   SE_LOAD s,n   load OrgData/effect/Geffect.list entry n into slot s;  SE_PLAY s;  SE_FREE s
 *   BGM n         music track n (1-12, launchme's list; 0 = stop), looped */
#pragma once

int audio_init(const char *orgdata);        /* opens the device; harmless if there is none */
void audio_shutdown(void);
void audio_system_se(int n);
void audio_slot_load(int slot, int effect);
void audio_slot_play(int slot);
void audio_slot_free(int slot);
void audio_music(int track);
int audio_music_current(void);              /* track playing, 0 none */
void audio_set_volume(float music, float effects);
/* movie soundtracks: stereo float frames at 44.1 kHz pushed as they are decoded */
void audio_stream_begin(void);
void audio_stream_push(const float *lr, int frames);
int audio_stream_queued(void);
void audio_stream_end(void);
int audio_available(void);
int audio_decode_file(const char *path, short **pcm, int *frames, int *channels, int *rate);   /* tests */
