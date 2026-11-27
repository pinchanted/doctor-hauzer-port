#include "movie.h"
#include "audio.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#include "../third_party/pl_mpeg.h"

struct Movie {
    plm_t *plm;
    uint8_t *rgb;
    int w, h;
    unsigned serial;
    int audio;
};

static void on_video(plm_t *plm, plm_frame_t *f, void *user) {
    Movie *m = user;
    (void)plm;
    if ((int)f->width != m->w || (int)f->height != m->h || !m->rgb) {
        free(m->rgb);
        m->w = (int)f->width; m->h = (int)f->height;
        m->rgb = malloc((size_t)m->w * m->h * 3);
    }
    plm_frame_to_rgb(f, m->rgb, m->w * 3);
    m->serial++;
}

static void on_audio(plm_t *plm, plm_samples_t *s, void *user) {
    (void)plm; (void)user;
    audio_stream_push(s->interleaved, (int)s->count);
}

Movie *movie_open(const char *path) {
    plm_t *plm = plm_create_with_filename(path);
    if (!plm) return NULL;
    if (!plm_has_headers(plm)) { plm_destroy(plm); return NULL; }
    Movie *m = calloc(1, sizeof *m);
    m->plm = plm;
    plm_set_video_decode_callback(plm, on_video, m);
    m->audio = plm_get_num_audio_streams(plm) > 0 && audio_available();
    plm_set_audio_enabled(plm, m->audio);
    if (m->audio) {
        plm_set_audio_decode_callback(plm, on_audio, m);
        plm_set_audio_lead_time(plm, 0.1);
        audio_stream_begin();
    }
    plm_set_loop(plm, 0);
    log_msg("movie %s: %dx%d, %.1f s%s", path, plm_get_width(plm), plm_get_height(plm), plm_get_duration(plm),
            m->audio ? "" : " (no audio)");
    return m;
}

int movie_update(Movie *m, double seconds) {
    if (!m) return 0;
    if (seconds > 0.25) seconds = 0.25;
    plm_decode(m->plm, seconds);
    return !plm_has_ended(m->plm);
}

int movie_frame(const Movie *m, const uint8_t **rgb, int *w, int *h, unsigned *serial) {
    if (!m || !m->rgb) return -1;
    *rgb = m->rgb; *w = m->w; *h = m->h; *serial = m->serial;
    return 0;
}

double movie_time(const Movie *m) { return m ? plm_get_time(m->plm) : 0; }
double movie_duration(const Movie *m) { return m ? plm_get_duration(m->plm) : 0; }

void movie_close(Movie *m) {
    if (!m) return;
    if (m->audio) audio_stream_end();
    plm_destroy(m->plm);
    free(m->rgb);
    free(m);
}
