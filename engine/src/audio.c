#include "audio.h"
#include "util.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_RATE 44100
#define MAX_VOICES 16

typedef struct { short *pcm; int frames, channels, rate; } Sound;
typedef struct { const Sound *s; double pos, step; int active; } Voice;

static SDL_AudioDeviceID g_dev;
static char g_orgdata[1024];
static Sound g_system[8], g_slots[16], g_music;
static int g_slot_loaded[16];
static Voice g_voices[MAX_VOICES];
static double g_music_pos, g_music_step;
static int g_music_track = -1, g_music_on;
static float g_vol_music = 0.8f, g_vol_fx = 1.0f;
static char g_geffect[64][128];
/* streamed PCM (movie soundtracks): a ring of stereo float frames at OUT_RATE */
#define STREAM_CAP (OUT_RATE * 2)
static float g_stream[STREAM_CAP][2];
static int g_stream_r, g_stream_w, g_stream_on;
static float g_vol_stream = 1.0f;
static int g_ngeffect;

static const char *SYSTEM_SE[8] = {"ASHI_YUKA1", "AKERU1.AIFF", "SIMERU1.AIFF", "KETTEI.AIFF", "CANCEL.AIFF",
                                   "SENTAKU.AIFF", "ITEMGET.AIFF", "SAKEBI.AIFF"};
static const char *MUSIC[12] = {"music001.aifc", "music002.aifc", "music003.aifc", "music004.aifc", "music005.aifc",
                                "music006.aifc", "tokei_007.aifc", "inu_008.aifc", "iwa_009.aifc", "mizu_010.aifc",
                                "FUKUROU.aifc", "ending.aifc"};

/* 80-bit IEEE extended (AIFF sample rate) */
static double ext80(const uint8_t *p) {
    int e = ((p[0] & 0x7f) << 8) | p[1];
    uint64_t m = 0;
    for (int i = 0; i < 8; i++) m = (m << 8) | p[2 + i];
    if (!e && !m) return 0;
    return ldexp((double)m, e - 16383 - 63) * ((p[0] & 0x80) ? -1 : 1);
}

int audio_decode_file(const char *path, short **pcm, int *frames, int *channels, int *rate) {
    Blob b = blob_load(path);
    if (!b.data || b.size < 12 || memcmp(b.data, "FORM", 4)) { blob_free(&b); return -1; }
    int ch = 1, bits = 8, sdx2 = 0;
    long nfr = 0;
    double sr = 22050;
    const uint8_t *snd = NULL;
    size_t nsnd = 0, off = 12;
    while (off + 8 <= b.size) {
        const uint8_t *c = b.data + off;
        uint32_t len = (uint32_t)be32(c + 4);
        if (off + 8 + len > b.size) len = (uint32_t)(b.size - off - 8);
        if (!memcmp(c, "COMM", 4)) {
            ch = be16(c + 8); nfr = (long)(uint32_t)be32(c + 10); bits = be16(c + 14); sr = ext80(c + 16);
            if (len >= 22 && !memcmp(c + 26, "SDX2", 4)) sdx2 = 1;   /* AIFC compression type */
        } else if (!memcmp(c, "SSND", 4)) {
            uint32_t o = (uint32_t)be32(c + 8);
            snd = c + 16 + o; nsnd = len >= 8 + o ? len - 8 - o : 0;
        }
        off += 8 + len + (len & 1);
    }
    if (!snd || ch < 1 || ch > 2) { blob_free(&b); return -1; }
    long n = sdx2 ? (long)(nsnd / (size_t)ch) : (long)(nsnd / (size_t)(ch * (bits / 8)));
    if (nfr && nfr < n) n = nfr;
    short *o = malloc(sizeof *o * (size_t)(n * ch + 1));
    if (sdx2) {                                       /* Squareroot-Delta-Exact: 8-bit codes -> 16-bit */
        int prev[2] = {0, 0};
        for (long i = 0; i < n; i++)
            for (int k = 0; k < ch; k++) {
                int v = (signed char)snd[i * ch + k];
                int sq = v * abs(v) * 2;
                prev[k] = (v & 1) ? prev[k] + sq : sq;
                if (prev[k] > 32767) prev[k] = 32767;
                if (prev[k] < -32768) prev[k] = -32768;
                o[i * ch + k] = (short)prev[k];
            }
    } else if (bits == 8) {
        for (long i = 0; i < n * ch; i++) o[i] = (short)((signed char)snd[i] * 256);
    } else {
        for (long i = 0; i < n * ch; i++) o[i] = (short)be16(snd + 2 * i);
    }
    blob_free(&b);
    *pcm = o; *frames = (int)n; *channels = ch; *rate = (int)(sr + 0.5);
    return 0;
}

static int load_sound(Sound *s, const char *rel) {
    char p[1200];
    path_join(p, sizeof p, g_orgdata, rel);
    free(s->pcm);
    memset(s, 0, sizeof *s);
    if (audio_decode_file(p, &s->pcm, &s->frames, &s->channels, &s->rate)) { log_msg("audio: cannot read %s", p); return -1; }
    return 0;
}

static float sample(const Sound *s, double pos, int ch) {
    long i = (long)pos;
    if (i >= s->frames) return 0;
    float f = (float)(pos - (double)i);
    int c = s->channels == 2 ? ch : 0;
    float a = s->pcm[i * s->channels + c], b = (i + 1 < s->frames) ? s->pcm[(i + 1) * s->channels + c] : a;
    return a + (b - a) * f;
}

static void mix(void *ud, Uint8 *stream, int len) {
    (void)ud;
    short *out = (short *)stream;
    int frames = len / 4;
    for (int f = 0; f < frames; f++) {
        float l = 0, r = 0;
        if (g_music_on && g_music.pcm) {
            l += sample(&g_music, g_music_pos, 0) * g_vol_music;
            r += sample(&g_music, g_music_pos, 1) * g_vol_music;
            g_music_pos += g_music_step;
            if (g_music_pos >= g_music.frames) g_music_pos -= g_music.frames;   /* tracks loop */
        }
        if (g_stream_on && g_stream_r != g_stream_w) {
            l += g_stream[g_stream_r][0] * 32767.0f * g_vol_stream;
            r += g_stream[g_stream_r][1] * 32767.0f * g_vol_stream;
            g_stream_r = (g_stream_r + 1) % STREAM_CAP;
        }
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vo = &g_voices[v];
            if (!vo->active) continue;
            l += sample(vo->s, vo->pos, 0) * g_vol_fx;
            r += sample(vo->s, vo->pos, 1) * g_vol_fx;
            vo->pos += vo->step;
            if (vo->pos >= vo->s->frames) vo->active = 0;
        }
        l = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
        r = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
        out[2 * f] = (short)l; out[2 * f + 1] = (short)r;
    }
}

int audio_init(const char *orgdata) {
    snprintf(g_orgdata, sizeof g_orgdata, "%s", orgdata);
    for (int i = 0; i < 8; i++) {
        char rel[128]; snprintf(rel, sizeof rel, "effect/%s", SYSTEM_SE[i]);
        load_sound(&g_system[i], rel);
    }
    char p[1200];
    path_join(p, sizeof p, orgdata, "effect/Geffect.list");
    Blob b = blob_load(p);
    if (b.data) {                                    /* "OrgData/effect/NAME" entries separated by CR */
        size_t start = 0;
        for (size_t i = 0; i <= b.size && g_ngeffect < 64; i++)
            if (i == b.size || b.data[i] == '\r' || b.data[i] == '\n') {
                if (i > start) {
                    size_t n = i - start;
                    if (n > 8 && !memcmp(b.data + start, "OrgData/", 8)) { start += 8; n -= 8; }
                    if (n >= sizeof g_geffect[0]) n = sizeof g_geffect[0] - 1;
                    memcpy(g_geffect[g_ngeffect], b.data + start, n);
                    g_geffect[g_ngeffect++][n] = 0;
                }
                start = i + 1;
            }
        blob_free(&b);
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { log_msg("audio: %s", SDL_GetError()); return -1; }
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = OUT_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = mix;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) { log_msg("audio: no device (%s)", SDL_GetError()); return -1; }
    SDL_PauseAudioDevice(g_dev, 0);
    log_msg("audio: %d effects listed, device open", g_ngeffect);
    return 0;
}

void audio_shutdown(void) { if (g_dev) SDL_CloseAudioDevice(g_dev); g_dev = 0; }

static void start_voice(const Sound *s) {
    if (!g_dev || !s->pcm) return;
    SDL_LockAudioDevice(g_dev);
    for (int v = 0; v < MAX_VOICES; v++)
        if (!g_voices[v].active) {
            g_voices[v].s = s; g_voices[v].pos = 0; g_voices[v].step = (double)s->rate / OUT_RATE; g_voices[v].active = 1;
            break;
        }
    SDL_UnlockAudioDevice(g_dev);
}

void audio_system_se(int n) { if (n >= 0 && n < 8) start_voice(&g_system[n]); }

/* decode outside the audio lock, then swap the new data in */
static void swap_in(Sound *dst, Sound *fresh, int stop_voices) {
    if (g_dev) SDL_LockAudioDevice(g_dev);
    if (stop_voices) for (int v = 0; v < MAX_VOICES; v++) if (g_voices[v].s == dst) g_voices[v].active = 0;
    Sound old = *dst;
    *dst = *fresh;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    free(old.pcm);
}

void audio_slot_load(int slot, int effect) {
    if (slot < 0 || slot >= 16 || effect < 0 || effect >= g_ngeffect) return;
    Sound fresh = {0};
    load_sound(&fresh, g_geffect[effect]);
    swap_in(&g_slots[slot], &fresh, 1);
    g_slot_loaded[slot] = g_slots[slot].pcm != NULL;
}

void audio_slot_play(int slot) { if (slot >= 0 && slot < 16 && g_slot_loaded[slot]) start_voice(&g_slots[slot]); }
void audio_slot_free(int slot) { (void)slot; /* the sample stays until the slot is reloaded, so a playing sound finishes */ }

void audio_music(int track) {
    if (track == g_music_track) return;
    g_music_track = track;
    Sound fresh = {0};
    if (track >= 1 && track <= 12) {
        char rel[128]; snprintf(rel, sizeof rel, "music/%s", MUSIC[track - 1]);
        load_sound(&fresh, rel);
    }
    if (g_dev) SDL_LockAudioDevice(g_dev);
    g_music_on = 0;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
    swap_in(&g_music, &fresh, 0);
    if (g_dev) SDL_LockAudioDevice(g_dev);
    g_music_pos = 0;
    g_music_step = g_music.rate ? (double)g_music.rate / OUT_RATE : 1;
    g_music_on = g_music.pcm != NULL;
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
}

void audio_set_volume(float music, float effects) { g_vol_music = music; g_vol_fx = effects; }

/* ---------------------------------------------------------------- streamed PCM (movies) */
void audio_stream_begin(void) {
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    g_stream_r = g_stream_w = 0; g_stream_on = 1;
    SDL_UnlockAudioDevice(g_dev);
}

/* interleaved stereo floats (-1..1) at 44.1 kHz; drops what does not fit */
void audio_stream_push(const float *lr, int frames) {
    if (!g_dev || !g_stream_on) return;
    SDL_LockAudioDevice(g_dev);
    for (int i = 0; i < frames; i++) {
        int nw = (g_stream_w + 1) % STREAM_CAP;
        if (nw == g_stream_r) break;
        g_stream[g_stream_w][0] = lr[2 * i]; g_stream[g_stream_w][1] = lr[2 * i + 1];
        g_stream_w = nw;
    }
    SDL_UnlockAudioDevice(g_dev);
}

int audio_stream_queued(void) {
    if (!g_dev) return 0;
    SDL_LockAudioDevice(g_dev);
    int n = (g_stream_w - g_stream_r + STREAM_CAP) % STREAM_CAP;
    SDL_UnlockAudioDevice(g_dev);
    return n;
}

void audio_stream_end(void) {
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    g_stream_on = 0; g_stream_r = g_stream_w = 0;
    SDL_UnlockAudioDevice(g_dev);
}

int audio_available(void) { return g_dev != 0; }

int audio_music_current(void) { return g_music_on ? g_music_track : 0; }
