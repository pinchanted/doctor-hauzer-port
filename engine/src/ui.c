#include "ui.h"
#include "game.h"
#include "audio.h"
#include "text.h"
#include "movie.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"

/* system sound effects (SE n table, launchme 0xb8bc) */
enum { SE_OK = 3, SE_CANCEL = 4, SE_MOVE = 5, SE_ITEMGET = 6 };

/* the original ran its menus once per field (~30 Hz at its frame rate); speeds below are per 30 Hz frame */
#define MENU_HZ 30.0f

/* English names as the fan translation's examine pages spell them (launchme keeps the Japanese ones) */
static const char *const ITEM_EN[ITEM_COUNT] = {
    "", "Iron Key", "Rusted Key", "Copper Key", "Wooden Key", "Bronze Key", "Patinated Key", "Gold Key",
    "Brass Key", "Silver Key", "Bone Key", "Hauzer's Diary 1", "Hauzer's Diary 2", "Hauzer's Diary 3",
    "Hauzer's Diary 4", "Lithograph Memo", "Digger's Memo", "First Floor Map", "Second Floor Map",
    "Dictionary", "Leather Bound Book", "Dry Flower", "Vase", "Oil Can", "Knife", "Lithograph",
    "Candlestick", "Axe", "Notebook", "Vent Cover", "Lighter", "Crowbar", "Shotgun", "Dynamite",
    "Wife's Photo", "Pot"};

const char *ui_item_name_en(int id) { return id > 0 && id < ITEM_COUNT ? ITEM_EN[id] : ""; }

/* items whose examine page leaves the right half for the model (keys and objects; not the documents) */
static int page_shows_model(int id) { return (id >= 1 && id <= 10) || id >= 20; }

static float speed(const Game *g) { return MENU_HZ / (float)(g->hz > 0 ? g->hz : 20); }

static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

static int pressed(Ui *u, uint32_t pad, uint32_t bit) { return (pad & bit) && !(u->prev_pad & bit); }

static void reset_model(Game *g, int item, float slide) {
    Ui *u = &g->ui;
    u->item = item;
    memset(u->rot, 0, sizeof u->rot);
    memcpy(u->rot_prev, u->rot, sizeof u->rot);
    for (int k = 0; k < 3; k++) u->vel[k] = item > 0 && item < ITEM_COUNT ? (float)g->human.item_spin[item][k] : 0;
    u->slide = u->slide_prev = slide;
}

static void open(Game *g, int mode, uint32_t pad) {
    Ui *u = &g->ui;
    u->mode = mode; u->t = 0; u->closing = 0; u->sel = 0; u->saved = 0;
    u->prev_pad = pad | g->prev_pad;   /* a button still held from the frame that opened it does not count */
}

int ui_open_menu(Game *g) {
    open(g, UI_MENU, PAD_START);
    if (g->item_cursor < 0 || g->item_cursor >= 40 || !g->items[g->item_cursor]) g->item_cursor = 0;
    reset_model(g, g->items[g->item_cursor], 0);
    g->ui.back_to_menu = 0;
    g->ui.pickup_after = 0;
    audio_system_se(SE_OK);
    return 1;
}

int ui_open_pickup(Game *g, int item, int script_target) {
    if (item <= 0 || item >= ITEM_COUNT) return 0;
    open(g, UI_PICKUP, 0);
    g->ui.script_label = script_target;
    g->ui.taken = 0;
    reset_model(g, item, 0);
    log_msg("  ITEM_SCREEN %d", item);
    return 1;
}

int ui_open_picture(Game *g, int n, int fade) {
    open(g, UI_PICTURE, 0);
    g->ui.picture = n;
    g->ui.picture_fade = fade > 0 ? fade : 10;
    log_msg("  PICTURE %d", n);
    return 1;
}

int ui_open_save(Game *g) {
    open(g, UI_SAVE, 0);
    g->ui.sel = g->last_save_slot >= 0 && g->last_save_slot < GAME_SAVE_SLOTS ? g->last_save_slot : 0;
    audio_system_se(SE_OK);
    return 1;
}

/* PICTURE fades are in original frames (~30 per second) */
static int pic_ticks(const Game *g) {
    int n = (int)(g->ui.picture_fade * (g->hz > 0 ? g->hz : 20) / 30.0f + 0.5f);
    return n < 1 ? 1 : n;
}

int ui_covers_screen(const Game *g) {
    int m = g->ui.mode;
    if (!g->ui.in_game && m != UI_NONE) return 1;
    return m == UI_MENU || m == UI_PAGES || m == UI_SAVE || m == UI_MOVIE || m == UI_TITLE || m == UI_LOAD ||
           (m == UI_PICTURE && g->ui.t > pic_ticks(g) && !g->ui.closing);
}

static void spin(Game *g) {
    Ui *u = &g->ui;
    memcpy(u->rot_prev, u->rot, sizeof u->rot);
    u->slide_prev = u->slide;
    float k = speed(g);
    for (int i = 0; i < 3; i++) u->rot[i] += u->vel[i] * k;
}

static void finish(Game *g) {
    Ui *u = &g->ui;
    int mode = u->mode;
    u->mode = UI_NONE;
    if (mode == UI_PICKUP) {
        if (u->taken) {
            game_add_item(g, u->item);
            if (g_globals[105] == 0) g_globals[100 + u->item] = 1;   /* 0xf234 */
        }
        if (u->script_label >= 0) {
            if (u->taken) g->script.pc = u->script_label;
            g->script.wait = 1;
        }
    } else if (mode == UI_PICTURE) {
        g->script.wait = 1;
    }
    if (u->pickup_after) { int it = u->pickup_after; u->pickup_after = 0; ui_open_pickup(g, it, -1); }
}

static void menu_change(Game *g, int dir) {
    int n = 0;
    while (n < 40 && g->items[n]) n++;
    if (n <= 1) return;
    g->item_cursor = (g->item_cursor + dir + n) % n;
    reset_model(g, g->items[g->item_cursor], dir > 0 ? 260.0f : -260.0f);
    audio_system_se(SE_MOVE);
}

static void tick_menu(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    spin(g);
    u->slide *= 0.45f;
    if (fabsf(u->slide) < 1) u->slide = 0;
    if (u->closing) return;
    int empty = !g->items[0];
    if (empty) {
        if (pressed(u, pad, PAD_A | PAD_B | PAD_C | PAD_START)) { audio_system_se(SE_CANCEL); u->closing = 3; }
        return;
    }
    if (pressed(u, pad, PAD_LEFT)) menu_change(g, -1);
    else if (pressed(u, pad, PAD_RIGHT)) menu_change(g, 1);
    else if (pressed(u, pad, PAD_UP) || pressed(u, pad, PAD_DOWN)) { u->sel ^= 1; audio_system_se(SE_MOVE); }
    else if (pressed(u, pad, PAD_B | PAD_C | PAD_START)) { audio_system_se(SE_CANCEL); u->closing = 3; }
    else if (pressed(u, pad, PAD_A)) {
        int it = g->items[g->item_cursor];
        if (u->sel == 0) {                                     /* use: back to the room, then 0x55bc */
            audio_system_se(SE_OK);
            g->use_item = it;
            u->closing = 3;
        } else {                                               /* examine: the item's pages */
            audio_system_se(SE_OK);
            u->mode = UI_PAGES; u->t = 0; u->page = 0;
            u->back_to_menu = 1;
            reset_model(g, it, -500.0f);
            if (it == 20 && g_globals[105] == 0) u->pickup_after = 5;   /* the bronze key in the leather book (0xf7b8) */
        }
    }
}

static int page_count(const Game *g, int item) {
    int n = 0;
    if (item <= 0 || item >= ITEM_COUNT) return 0;
    while (n < 8 && g->human.item_pages[item][n] >= 0) n++;
    return n;
}

static void tick_pages(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    spin(g);
    /* the model slides in from the left to x = 140 (0xf884: 16 frames of +40 from -500) */
    float target = 140.0f;
    if (u->slide < target) { u->slide += 40.0f * speed(g); if (u->slide > target) u->slide = target; }
    if (u->closing) return;
    /* d-pad / L / R change the spin, B stops it (0xf998) */
    float k = 0.25f * speed(g);
    if (pad & PAD_LEFT) u->vel[1] += k;
    if (pad & PAD_RIGHT) u->vel[1] -= k;
    if (pad & PAD_UP) u->vel[0] -= k;
    if (pad & PAD_DOWN) u->vel[0] += k;
    if (pad & PAD_L) u->vel[2] -= k;
    if (pad & PAD_R) u->vel[2] += k;
    if (pressed(u, pad, PAD_B)) memset(u->vel, 0, sizeof u->vel);
    if (pressed(u, pad, PAD_A)) {
        if (u->page + 1 < page_count(g, u->item)) { u->page++; u->t = 0; audio_system_se(SE_MOVE); }
        else { audio_system_se(SE_CANCEL); u->closing = 3; }
    } else if (pressed(u, pad, PAD_C | PAD_START)) { audio_system_se(SE_CANCEL); u->closing = 3; }
}

static void tick_pickup(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    spin(g);
    if (u->closing) return;
    if (pressed(u, pad, PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) { u->sel ^= 1; audio_system_se(SE_MOVE); }
    else if (pressed(u, pad, PAD_A)) {
        u->taken = u->sel == 0;
        audio_system_se(u->taken ? SE_ITEMGET : SE_CANCEL);
        u->closing = 3;
    } else if (pressed(u, pad, PAD_B | PAD_C)) { u->taken = 0; audio_system_se(SE_CANCEL); u->closing = 3; }
}

static void tick_picture(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    if (u->closing) return;
    if (u->t > pic_ticks(g) && pressed(u, pad, PAD_A | PAD_B | PAD_C | PAD_START)) u->closing = pic_ticks(g);
}

static void tick_save(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    if (u->closing) return;
    if (pressed(u, pad, PAD_UP)) { u->sel = (u->sel + GAME_SAVE_SLOTS - 1) % GAME_SAVE_SLOTS; u->saved = 0; audio_system_se(SE_MOVE); }
    else if (pressed(u, pad, PAD_DOWN)) { u->sel = (u->sel + 1) % GAME_SAVE_SLOTS; u->saved = 0; audio_system_se(SE_MOVE); }
    else if (pressed(u, pad, PAD_A)) {
        if (game_save(g, u->sel) == 0) { u->saved = u->sel + 1; audio_system_se(SE_ITEMGET); g->last_save_slot = u->sel; }
        else audio_system_se(SE_CANCEL);
    } else if (pressed(u, pad, PAD_B | PAD_C | PAD_START)) { audio_system_se(SE_CANCEL); u->closing = 3; }
}


/* ---------------------------------------------------------------- front end: movies, title, load */
/* movies the flow can play, by name; scripts name theirs through stream.scp */
static const char *const MOVIES[] = {"OPDS", "SLDS", "MESDS", "STDS", "GODS", "EDDS", "TRDS", "PADDS"};
enum { MV_OPDS, MV_SLDS, MV_MESDS, MV_STDS, MV_GODS, MV_EDDS, MV_TRDS, MV_PADDS, MV_COUNT };
/* each movie's picture width on the 320-wide 3DO screen (FILM header), so it shows at the original size */
static const int MOVIE_W[MV_COUNT] = {300, 290, 290, 290, 260, 300, 260, 320};

static int movie_by_name(const char *n) {
    for (int i = 0; n && i < MV_COUNT; i++) if (!strcmp(MOVIES[i], n)) return i;
    return -1;
}

static int saved_track;

static void stop_movie(Ui *u) {
    if (u->movie) { movie_close(u->movie); u->movie = NULL; }
}

static Movie *open_movie(Game *g, int mv) {
    char p[1200];
    for (int i = g->english ? 0 : 1; i < 2; i++) {
        if (!g->movies[i][0]) continue;
        snprintf(p, sizeof p, "%s/%s.mpg", g->movies[i], MOVIES[mv]);
        if (!file_exists(p)) continue;
        Movie *m = movie_open(p);
        if (m) return m;
    }
    log_msg("ui: movie %s not found (run tools/movies_build.py)", MOVIES[mv]);
    return NULL;
}

static void flow_next(Game *g);

static void start_movie(Game *g, int mv) {
    Ui *u = &g->ui;
    open(g, UI_MOVIE, 0);
    stop_movie(u);
    u->movie_index = mv;
    u->movie_done = 0;
    audio_music(0);
    u->movie = open_movie(g, mv);
    u->movie_missing = u->movie == NULL;
}

static void start_title(Game *g) {
    Ui *u = &g->ui;
    open(g, UI_TITLE, 0);
    stop_movie(u);
    audio_music(0);
    u->in_game = 0;
    u->movie = open_movie(g, MV_SLDS);
    u->movie_done = u->movie == NULL;
    u->title_phase = 0; u->title_chosen = -1; u->title_t = 0;
}

static void start_load(Game *g) {
    open(g, UI_LOAD, 0);
    stop_movie(&g->ui);
    g->ui.sel = g->last_save_slot >= 0 && g->last_save_slot < GAME_SAVE_SLOTS ? g->last_save_slot : 0;
}

static void flow_set(Game *g, const int *steps, int n) {
    Ui *u = &g->ui;
    u->nq = 0;
    for (int i = 0; i < n && i < 8; i++) u->queue[u->nq++] = steps[i];
    flow_next(g);
}

static void flow_next(Game *g) {
    Ui *u = &g->ui;
    stop_movie(u);
    if (!u->nq) { u->mode = UI_NONE; return; }
    int step = u->queue[0];
    memmove(u->queue, u->queue + 1, sizeof(int) * 7);
    u->nq--;
    if (step >= STEP_MOVIE) { start_movie(g, step - STEP_MOVIE); return; }
    switch (step) {
    case STEP_TITLE: start_title(g); break;
    case STEP_NEWGAME:
        u->mode = UI_NONE;
        u->in_game = 1;
        game_new(g);
        break;
    case STEP_CREDITS:                                  /* 0xf70: macroEnd plays the credits over room scenes */
        u->mode = UI_NONE;
        u->in_game = 1;
        if (game_start_cinematic(g, "macro/macroEnd", 1)) flow_next(g);
        break;
    case STEP_DEMO:                                     /* 0xdf8: the attract demos when the title times out */
        u->mode = UI_NONE;
        u->in_game = 1;
        if (game_start_cinematic(g, "macro/macroDemo1", 2)) flow_next(g);
        break;
    case STEP_END_SCREEN:                               /* End.img, faded in over 60 frames */
        open(g, UI_END, 0);
        u->in_game = 0;
        break;
    case STEP_RESUME:                                   /* back to a script that played a movie (0x9ee8) */
        u->mode = UI_NONE;
        if (saved_track > 0) audio_music(saved_track);
        g->script.wait = 1;
        break;
    default: u->mode = UI_NONE; break;
    }
}

void ui_boot(Game *g) {
    static const int s[] = {STEP_MOVIE + MV_OPDS, STEP_TITLE};
    g->ui.in_game = 0;
    flow_set(g, s, 2);
}

void ui_game_over(Game *g) {
    static const int s[] = {STEP_MOVIE + MV_GODS, STEP_TITLE};
    g->ui.in_game = 0;
    flow_set(g, s, 2);
}

void ui_ending(Game *g) {
    static const int s[] = {STEP_MOVIE + MV_EDDS, STEP_CREDITS, STEP_END_SCREEN, STEP_TITLE};
    g->ui.in_game = 0;
    flow_set(g, s, 4);
}

void ui_cinematic_done(Game *g, int aborted) {
    if (!aborted && g->cinematic_kind >= 2 && g->cinematic_kind < 4) {   /* demos 1 -> 2 -> 3 */
        char m[32];
        snprintf(m, sizeof m, "macro/macroDemo%d", g->cinematic_kind);
        if (game_start_cinematic(g, m, g->cinematic_kind + 1) == 0) return;
    }
    log_msg("cinematic %d %s", g->cinematic_kind, aborted ? "skipped" : "finished");
    g->cinematic = 0;
    g->pic_shown = -1;
    audio_music(0);
    flow_next(g);
}

int ui_play_movie(Game *g, int stream_index) {
    int mv = movie_by_name(game_stream_name(g, stream_index));
    log_msg("  MOVIE %d (%s)", stream_index, mv >= 0 ? MOVIES[mv] : "?");
    if (mv < 0) return 0;
    saved_track = audio_music_current();
    int s[] = {STEP_MOVIE + mv, STEP_RESUME};
    flow_set(g, s, 2);
    return 1;
}

void ui_update(Game *g, double seconds) {
    Ui *u = &g->ui;
    if ((u->mode == UI_MOVIE || u->mode == UI_TITLE) && u->movie && !u->movie_done)
        if (!movie_update(u->movie, seconds)) u->movie_done = 1;
    if (u->mode == UI_TITLE) u->title_t += (float)seconds;
}

static void tick_movie(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    if (u->movie_missing) {                            /* no rebuilt movie: a short card for game over, else skip */
        if (u->movie_index != MV_GODS || u->t > 3 * g->hz || pressed(u, pad, PAD_A | PAD_C | PAD_START)) flow_next(g);
        return;
    }
    if (pressed(u, pad, PAD_START | PAD_A | PAD_C)) u->movie_done = 1;   /* skip */
    if (u->movie_done) flow_next(g);
}

static void tick_title(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    if (u->title_chosen < 0) {
        if (pressed(u, pad, PAD_UP)) { u->title_sel = u->title_sel > 0 ? u->title_sel - 1 : 0; audio_system_se(2); u->title_t = u->title_t < 1.5f ? 1.5f : u->title_t; }
        else if (pressed(u, pad, PAD_DOWN)) { u->title_sel = u->title_sel < 3 ? u->title_sel + 1 : 3; audio_system_se(2); u->title_t = u->title_t < 1.5f ? 1.5f : u->title_t; }
        else if (pressed(u, pad, PAD_A | PAD_B | PAD_C | PAD_START | PAD_X)) {   /* 0x2d90 */
            if (u->title_t < 1.5f) u->title_t = 1.5f;                             /* first press finishes the slide-in */
            else { u->title_chosen = u->title_sel; u->t = 0; audio_system_se(SE_OK); }
        } else if (u->movie_done && u->title_t > 1.5f) {                           /* nothing chosen: demos, opening */
            static const int s[] = {STEP_DEMO, STEP_MOVIE + MV_OPDS, STEP_TITLE};
            flow_set(g, s, 3);
        }
        return;
    }
    if (u->t < g->hz * 3 / 2) return;                  /* the choice flashes, then fades */
    switch (u->title_chosen) {
    case 0: { static const int s[] = {STEP_MOVIE + MV_OPDS, STEP_TITLE}; flow_set(g, s, 2); break; }
    case 1: { static const int s[] = {STEP_MOVIE + MV_MESDS, STEP_MOVIE + MV_STDS, STEP_NEWGAME}; flow_set(g, s, 3); break; }
    case 2: start_load(g); break;
    case 3: { static const int s[] = {STEP_MOVIE + MV_PADDS, STEP_TITLE}; flow_set(g, s, 2); break; }
    }
}

static void tick_end(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    if (u->closing) return;
    if (u->t > 3 * g->hz && pressed(u, pad, PAD_A | PAD_B | PAD_C | PAD_START)) u->closing = g->hz;
}

static void tick_load(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    char info[160];
    if (pressed(u, pad, PAD_UP)) { u->sel = (u->sel + GAME_SAVE_SLOTS - 1) % GAME_SAVE_SLOTS; audio_system_se(SE_MOVE); }
    else if (pressed(u, pad, PAD_DOWN)) { u->sel = (u->sel + 1) % GAME_SAVE_SLOTS; audio_system_se(SE_MOVE); }
    else if (pressed(u, pad, PAD_A)) {
        if (game_save_info(g, u->sel, info, sizeof info) == 0) {
            audio_system_se(SE_OK);
            u->mode = UI_NONE;
            u->in_game = 1;
            if (game_load_save(g, u->sel)) { start_title(g); return; }
        } else audio_system_se(SE_CANCEL);
    } else if (pressed(u, pad, PAD_B | PAD_C)) { audio_system_se(SE_CANCEL); start_title(g); }
}

void ui_tick(Game *g, uint32_t pad) {
    Ui *u = &g->ui;
    if (u->mode == UI_NONE) return;
    u->t++;
    switch (u->mode) {
    case UI_MENU: tick_menu(g, pad); break;
    case UI_PAGES: tick_pages(g, pad); break;
    case UI_PICKUP: tick_pickup(g, pad); break;
    case UI_PICTURE: tick_picture(g, pad); break;
    case UI_SAVE: tick_save(g, pad); break;
    case UI_MOVIE: tick_movie(g, pad); break;
    case UI_TITLE: tick_title(g, pad); break;
    case UI_LOAD: tick_load(g, pad); break;
    case UI_END: tick_end(g, pad); break;
    }
    u->prev_pad = pad;
    if (u->closing && u->mode == UI_END) { if (--u->closing == 0) flow_next(g); return; }
    if (u->closing && --u->closing == 0) {
        if (u->mode == UI_PAGES && u->back_to_menu && !u->pickup_after) {
            u->mode = UI_MENU; u->t = 0; u->sel = 1;
            reset_model(g, g->items[g->item_cursor], 0);
        } else finish(g);
    }
}

/* ---------------------------------------------------------------- drawing */
typedef struct { Game *g; Renderer *r; int w, h; float sx, sy, s; } Ctx;   /* stage: 4:3 at (sx, sy), scale s per 320x240 px */

static uint64_t fnv(const void *p, size_t n, uint64_t h) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
    return h;
}


/* a still by its OrgData-relative name without extension ("isearch/sea05"): the upscaled PNG (English
 * set first when playing in English), else the original picture decoded from the disc */
static uint64_t still(Ctx *c, const char *rel) {
    Game *g = c->g;
    uint64_t key = fnv(rel, strlen(rel), 0xcbf29ce484222325ull ^ (uint64_t)(g->english ? 7 : 3));
    if (render_ui_has(c->r, key)) return key;
    char p[1400];
    Image im = {0};
    for (int i = g->english ? 0 : 1; i < 2 && !im.rgba; i++) {
        if (!g->stills[i][0]) continue;
        snprintf(p, sizeof p, "%s/%s.png", g->stills[i], rel);
        if (!file_exists(p)) continue;
        int w, h, n;
        uint8_t *px = stbi_load(p, &w, &h, &n, 4);
        if (px) { im.w = w; im.h = h; im.rgba = malloc((size_t)w * h * 4); memcpy(im.rgba, px, (size_t)w * h * 4); stbi_image_free(px); }
    }
    if (!im.rgba) {
        const char *names[2] = {"%s/%s.img", "%s/%s"};
        for (int i = 0; i < 2 && !im.rgba; i++) {
            snprintf(p, sizeof p, names[i], g->orgdata, rel);
            Blob b = blob_load(p);
            if (b.data) { img_decode(b.data, b.size, &im); blob_free(&b); }
        }
    }
    if (!im.rgba) {   /* nothing found: a black placeholder so we do not retry every frame */
        im.w = im.h = 1; im.rgba = calloc(1, 4); im.rgba[3] = 255;
        log_msg("ui: missing still %s", rel);
    }
    render_ui_put(c->r, key, &im);
    image_free(&im);
    return key;
}

static void draw_still(Ctx *c, const char *rel, float alpha) {
    uint64_t k = still(c, rel);
    render_ui_draw(c->r, k, c->sx, c->sy, 320 * c->s, 240 * c->s, alpha, c->w, c->h);
}

/* text in stage coordinates (320x240), centred on x; px = text height in stage pixels */
static void draw_text(Ctx *c, const uint32_t *cp, float px, int style, float x, float y, float alpha, int align) {
    if (!cp || !cp[0]) return;
    int ipx = (int)(px * c->s + 0.5f);
    if (ipx < 10) ipx = 10;
    size_t n = 0; while (cp[n]) n++;
    uint64_t key = fnv(cp, n * 4, 0x84222325cbf29ce4ull ^ (uint64_t)ipx * 31 ^ (uint64_t)style << 40);
    if (!render_ui_has(c->r, key)) {
        Image im;
        if (text_render(cp, ipx, style, &im)) return;
        render_ui_put(c->r, key, &im);
        image_free(&im);
    }
    int tw, th;
    if (render_ui_size(c->r, key, &tw, &th)) return;
    float X = c->sx + x * c->s, Y = c->sy + y * c->s;
    if (align == 0) X -= tw / 2.0f; else if (align > 0) X -= tw;
    render_ui_draw(c->r, key, floorf(X), floorf(Y - th / 2.0f), (float)tw, (float)th, alpha, c->w, c->h);
}

/* pixel size of a text label (rendered into the cache if needed) */
static void text_size(Ctx *c, const uint32_t *cp, float px, int style, int *w, int *h) {
    *w = *h = 0;
    if (!cp || !cp[0]) return;
    int ipx = (int)(px * c->s + 0.5f);
    if (ipx < 10) ipx = 10;
    size_t n = 0; while (cp[n]) n++;
    uint64_t key = fnv(cp, n * 4, 0x84222325cbf29ce4ull ^ (uint64_t)ipx * 31 ^ (uint64_t)style << 40);
    if (!render_ui_has(c->r, key)) {
        Image im;
        if (text_render(cp, ipx, style, &im)) return;
        render_ui_put(c->r, key, &im);
        image_free(&im);
    }
    render_ui_size(c->r, key, w, h);
}

static void draw_utf8(Ctx *c, const char *s, float px, int style, float x, float y, float alpha, int align) {
    uint32_t cp[256];
    text_utf8(s, cp, 256);
    draw_text(c, cp, px, style, x, y, alpha, align);
}

static void item_name(const Game *g, int id, uint32_t *out, int cap) {
    if (g->english) text_utf8(ui_item_name_en(id), out, cap);
    else text_sjis(g->human.item_name_sjis[id > 0 && id < ITEM_COUNT ? id : 0], out, cap);
}

/* the item model at (x, y, z) in front of a camera at the origin looking along +z, as the menu sets it
 * up (0xf884 / 0xfb38) */
static void draw_item(Ctx *c, int item, float x, float y, float z, const float rot[3]) {
    Game *g = c->g;
    if (item <= 0 || item >= g->human.nitem) return;
    const Mesh *m = &g->human.item_mesh[item];
    static float wv[1024][3];
    int n = m->nverts < 1024 ? m->nverts : 1024;
    float pos[3] = {x, y, z};
    for (int i = 0; i < n; i++) obj_transform(pos, rot, m->verts[i], wv[i]);
    trilist_reset(&g->ui_tris);
    trilist_add_mesh(&g->ui_tris, (const float (*)[3])wv, m, g->human.item_table[item], 0);
    CameraView cam;
    memset(&cam, 0, sizeof cam);
    cam.target[2] = 1000;
    render_model(c->r, &g->ui_tris, &cam, (int)c->sx, (int)c->sy, (int)(320 * c->s), (int)(240 * c->s), c->w, c->h);
}

static void lerp_rot(const Ui *u, float a, float out[3]) {
    for (int k = 0; k < 3; k++) out[k] = u->rot_prev[k] + (u->rot[k] - u->rot_prev[k]) * a;
}

static float fade_in_out(const Ui *u, float a, int in_ticks, int out_ticks) {
    float t = in_ticks > 0 ? ((float)u->t - 1 + a) / in_ticks : 1;
    if (t > 1) t = 1;
    if (t < 0) t = 0;
    if (u->closing && out_ticks > 0) { float o = ((float)u->closing - a) / out_ticks; if (o < t) t = o < 0 ? 0 : o; }
    return t;
}

static void draw_menu(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    float f = fade_in_out(u, a, 3, 3);
    render_clear(c->w, c->h);
    draw_still(c, "window/cmd01", f);
    int it = g->items[g->item_cursor];
    uint32_t name[64];
    if (!g->items[0]) {
        if (g->english) draw_utf8(c, "You aren't carrying anything.", 15, TEXT_INK, 160, 175, f, 0);
        else draw_utf8(c, "持ち物は何もない", 15, TEXT_INK, 160, 175, f, 0);
    } else {
        float rot[3]; lerp_rot(u, a, rot);
        float x = u->slide_prev + (u->slide - u->slide_prev) * a;
        if (f > 0.5f) draw_item(c, it, x, -50, 400, rot);
        item_name(g, it, name, 64);
        draw_text(c, name, 20, TEXT_INK, 160, 150, f, 0);
        int n = 0; while (n < 40 && g->items[n]) n++;
        if (n > 1) {
            char buf[32]; snprintf(buf, sizeof buf, "%d / %d", g->item_cursor + 1, n);
            draw_utf8(c, "◀", 14, TEXT_INK, 52, 150, f * 0.8f, 0);
            draw_utf8(c, "▶", 14, TEXT_INK, 268, 150, f * 0.8f, 0);
            draw_utf8(c, buf, 10, TEXT_INK, 296, 226, f * 0.7f, 1);
        }
        static const char *opt_en[2] = {"Use", "Examine"}, *opt_jp[2] = {"使う", "調べる"};
        for (int i = 0; i < 2; i++) {
            float y = 184 + i * 28;
            const char *s = g->english ? opt_en[i] : opt_jp[i];
            draw_utf8(c, s, 18, TEXT_INK, 160, y, f * (u->sel == i ? 1.0f : 0.4f), 0);
            if (u->sel == i) {
                draw_utf8(c, "▶", 12, TEXT_INK, 112, y, f, 0);
                draw_utf8(c, "◀", 12, TEXT_INK, 208, y, f, 0);
            }
        }
    }
}

static void draw_pages(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    float f = fade_in_out(u, a, 3, 3);
    render_clear(c->w, c->h);
    int pi = u->item > 0 && u->item < ITEM_COUNT ? g->human.item_pages[u->item][u->page] : -1;
    if (pi >= 0) {
        char rel[64]; snprintf(rel, sizeof rel, "isearch/%s", ITEM_PAGE_FILE[pi]);
        draw_still(c, rel, u->page > 0 ? fade_in_out(u, a, 3, 3) : f);
    }
    if (page_shows_model(u->item) && f > 0.5f) {
        float rot[3]; lerp_rot(u, a, rot);
        float x = u->slide_prev + (u->slide - u->slide_prev) * a;
        draw_item(c, u->item, x, -50, 400, rot);
    }
    int n = page_count(g, u->item);
    if (n > 1) {
        char buf[32]; snprintf(buf, sizeof buf, "%d / %d", u->page + 1, n);
        draw_utf8(c, buf, 10, TEXT_SHADOW, 312, 230, f * 0.8f, 1);
    }
}

static void draw_pickup(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    float f = fade_in_out(u, a, 3, 3);
    render_ui_rect(c->r, 0, 0, (float)c->w, (float)c->h, 0.55f * f, c->w, c->h);
    float rot[3]; lerp_rot(u, a, rot);
    if (f > 0.3f) draw_item(c, u->item, 0, -55, 420, rot);
    uint32_t name[64], line[160];
    item_name(g, u->item, name, 64);
    char nm[160] = {0};
    /* name back to UTF-8 for the sentence */
    {
        char *o = nm;
        for (int i = 0; name[i] && o < nm + sizeof nm - 5; i++) {
            uint32_t ch = name[i];
            if (ch < 0x80) *o++ = (char)ch;
            else if (ch < 0x800) { *o++ = (char)(0xC0 | ch >> 6); *o++ = (char)(0x80 | (ch & 63)); }
            else { *o++ = (char)(0xE0 | ch >> 12); *o++ = (char)(0x80 | ((ch >> 6) & 63)); *o++ = (char)(0x80 | (ch & 63)); }
        }
        *o = 0;
    }
    char s[256];
    if (g->english) snprintf(s, sizeof s, "You found the %s.\nTake it?", nm);
    else snprintf(s, sizeof s, "%sを見つけました\n取りますか？", nm);   /* 0xf040 / 0xf050 */
    text_utf8(s, line, 160);
    render_ui_rect(c->r, c->sx + 60 * c->s, c->sy + 194 * c->s, 200 * c->s, 28 * c->s, 0.5f * f, c->w, c->h);
    draw_text(c, line, 15, TEXT_PANEL, 160, 164, f, 0);
    static const char *yes[2] = {"Yes", "はい"}, *no[2] = {"No", "いいえ"};
    int jp = !g->english;
    draw_utf8(c, yes[jp], 16, TEXT_SHADOW, 120, 208, f * (u->sel == 0 ? 1.0f : 0.45f), 0);
    draw_utf8(c, no[jp], 16, TEXT_SHADOW, 200, 208, f * (u->sel == 1 ? 1.0f : 0.45f), 0);
    draw_utf8(c, "▶", 12, TEXT_SHADOW, u->sel == 0 ? 94 : 170, 208, f, 0);
}

static void draw_picture(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    int n = pic_ticks(g);
    float f = fade_in_out(u, a, n, n);
    if (g->ui.t > n && !u->closing) render_clear(c->w, c->h);
    else render_ui_rect(c->r, 0, 0, (float)c->w, (float)c->h, f, c->w, c->h);
    /* images/image.scp: one path per line, 0-based (0x6ce8 -> 0x187a8) */
    char rel[96] = "";
    const char *e = game_image_name(g, u->picture);
    if (e) snprintf(rel, sizeof rel, "images/%s", e);
    char *dot = strrchr(rel, '.');
    if (dot && (!strcmp(dot, ".img") || !strcmp(dot, ".IMG"))) *dot = 0;
    if (rel[0]) draw_still(c, rel, f);
}

static void draw_save(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    float f = fade_in_out(u, a, 3, 3);
    render_clear(c->w, c->h);
    draw_still(c, "window/note", f);
    int jp = !g->english;
    draw_utf8(c, jp ? "どこに記録しますか？" : "Save to which page?", 16, TEXT_INK, 175, 33, f, 0);  /* between the ruled lines */
    for (int i = 0; i < GAME_SAVE_SLOTS; i++) {
        char info[160], s[220];
        float y = 59 + i * 50;
        if (game_save_info(g, i, info, sizeof info)) snprintf(info, sizeof info, "%s", jp ? "―― 空き ――" : "- empty -");
        snprintf(s, sizeof s, "%d   %s", i + 1, info);
        draw_utf8(c, s, 14, TEXT_INK, 86, y, f * (u->sel == i ? 1.0f : 0.5f), -1);
        if (u->sel == i) draw_utf8(c, "▶", 11, TEXT_INK, 72, y, f, 0);
    }
    if (u->saved) draw_utf8(c, jp ? "記録しました" : "Saved.", 15, TEXT_INK, 175, 209, f, 0);
}



/* ---------------------------------------------------------------- English captions for the movies */
typedef struct { char movie[16]; int style; float t0, t1, r[4]; uint32_t text[600]; } Cue;
enum { CUE_SUB, CUE_BOX, CUE_LABEL, CUE_PAGE };
static Cue g_cues[96];
static int g_ncues;
static struct { char movie[16]; float w, h; } g_cue_size[16];
static int g_ncue_size;

/* text/movies_en.txt (see the file for the format) */
void ui_load_movie_text(const char *path) {
    g_ncues = g_ncue_size = 0;
    Blob b = blob_load(path);
    if (!b.data) return;
    char *s = malloc(b.size + 1);
    memcpy(s, b.data, b.size); s[b.size] = 0;
    blob_free(&b);
    char cur[16] = "";
    for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (!n || line[0] == '#') continue;
        if (line[0] == '@') {
            float w, h; char m[16];
            if (sscanf(line + 1, "%15s %f %f", m, &w, &h) == 3 && g_ncue_size < 16) {
                snprintf(cur, sizeof cur, "%s", m);
                snprintf(g_cue_size[g_ncue_size].movie, 16, "%s", m);
                g_cue_size[g_ncue_size].w = w; g_cue_size[g_ncue_size].h = h; g_ncue_size++;
            }
            continue;
        }
        char *bar = strchr(line, '|');
        if (!bar || g_ncues >= 96) continue;
        *bar = 0;
        Cue *c = &g_cues[g_ncues];
        memset(c, 0, sizeof *c);
        char st[16];
        int k = sscanf(line, "%15s %f %f %f %f %f %f", st, &c->t0, &c->t1, &c->r[0], &c->r[1], &c->r[2], &c->r[3]);
        if (k < 3) continue;
        c->style = !strcmp(st, "box") ? CUE_BOX : !strcmp(st, "label") ? CUE_LABEL : !strcmp(st, "page") ? CUE_PAGE : CUE_SUB;
        snprintf(c->movie, sizeof c->movie, "%s", cur);
        /* text: skip one space after '|', turn \n into new lines */
        char *t = bar + 1, buf[600]; size_t o = 0;
        if (*t == ' ') t++;
        for (; *t && o < sizeof buf - 1; t++) {
            if (t[0] == '\\' && t[1] == 'n') { buf[o++] = '\n'; t++; }
            else buf[o++] = *t;
        }
        buf[o] = 0;
        text_utf8(buf, c->text, 600);
        g_ncues++;
    }
    free(s);
    log_msg("movie captions: %d from %s", g_ncues, path);
}

/* draws the captions due at time t over the movie picture at (mx, my, mw, mh) */
static void draw_cues(Ctx *c, const char *movie, double t, float mx, float my, float mw, float mh) {
    float sw = 0, shh = 0;
    for (int i = 0; i < g_ncue_size; i++) if (!strcmp(g_cue_size[i].movie, movie)) { sw = g_cue_size[i].w; shh = g_cue_size[i].h; }
    if (sw <= 0) return;
    float kx = mw / sw, ky = mh / shh;
    for (int i = 0; i < g_ncues; i++) {
        Cue *q = &g_cues[i];
        if (strcmp(q->movie, movie) || t < q->t0 || t > q->t1) continue;
        float fade = q->style == CUE_LABEL ? 0.05f : q->style == CUE_PAGE ? 0.6f : 0.25f;
        float a = (float)((t - q->t0) / fade); if (a > 1) a = 1;
        float b = (float)((q->t1 - t) / fade); if (b < a) a = b;
        if (a < 0) a = 0;
        float cover = (float)((t - q->t0) / 0.12); if (cover > 1) cover = 1;
        float cb = (float)((q->t1 - t) / 0.12); if (cb < cover) cover = cb;
        if (q->style == CUE_PAGE) {
            render_ui_fill(c->r, mx, my, mw, mh, 0, 0, 0, 1, c->w, c->h);
            draw_text(c, q->text, 12.5f * 240 / 212, TEXT_SHADOW, (mx + mw / 2 - c->sx) / c->s, (my + mh / 2 - c->sy) / c->s, a, 0);
            continue;
        }
        float x0 = mx + q->r[0] * kx, y0 = my + q->r[1] * ky, x1 = mx + q->r[2] * kx, y1 = my + q->r[3] * ky;
        float cx = ((x0 + x1) / 2 - c->sx) / c->s, cy = ((y0 + y1) / 2 - c->sy) / c->s;
        float hpx = (y1 - y0) / c->s;                     /* box height in stage pixels */
        int lines = 1; for (int k = 0; q->text[k]; k++) if (q->text[k] == '\n') lines++;
        float px = hpx / (lines * 1.25f + 0.25f);
        if (px > 15) px = 15;
        if (q->style == CUE_LABEL) {
            render_ui_fill(c->r, x0, y0, x1 - x0, y1 - y0, 0.62f, 0.52f, 0.92f, cover, c->w, c->h);
            render_ui_fill(c->r, x0 + 2, y0 + 2, x1 - x0 - 4, y1 - y0 - 4, 0.33f, 0.22f, 0.62f, cover, c->w, c->h);
            px = hpx * 0.5f;
        } else if (q->style == CUE_BOX) {
            render_ui_fill(c->r, x0, y0, x1 - x0, y1 - y0, 0.85f, 0.85f, 0.85f, cover * 0.95f, c->w, c->h);
            float bw = c->s > 2 ? c->s : 2;
            render_ui_fill(c->r, x0 + bw, y0 + bw, x1 - x0 - 2 * bw, y1 - y0 - 2 * bw, 0.06f, 0.06f, 0.07f, cover, c->w, c->h);
            px = hpx * 0.48f;
        } else {   /* caption band: opaque so no Japanese shows through, widened to fit the English */
            int tw = 0, th = 0;
            px = 11.5f;
            text_size(c, q->text, px, TEXT_SHADOW, &tw, &th);
            float need = tw + 8 * c->s, needh = th + 2 * c->s;
            if (need > x1 - x0) { float m = (x0 + x1) / 2; x0 = m - need / 2; x1 = m + need / 2; }
            if (needh > y1 - y0) { float m = (y0 + y1) / 2; y0 = m - needh / 2; y1 = m + needh / 2; }
            if (y1 > my + mh) { y0 -= y1 - (my + mh); y1 = my + mh; cy = ((y0 + y1) / 2 - c->sy) / c->s; }
            if (x0 < mx) { x1 += mx - x0; x0 = mx; }
            if (x1 > mx + mw) { x0 -= x1 - (mx + mw); x1 = mx + mw; }
            render_ui_fill(c->r, x0, y0, x1 - x0, y1 - y0, 0.02f, 0.02f, 0.03f, cover, c->w, c->h);
        }
        draw_text(c, q->text, px, TEXT_SHADOW, cx, cy, a, 0);
    }
}

/* the movie picture at its original size on the 4:3 stage */
static void draw_movie_frame(Ctx *c, int mv, float alpha) {
    Ui *u = &c->g->ui;
    const uint8_t *rgb; int w, h; unsigned serial;
    if (!u->movie || movie_frame(u->movie, &rgb, &w, &h, &serial)) return;
    float dw = (mv >= 0 && mv < MV_COUNT ? MOVIE_W[mv] : 320) * c->s, dh = dw * h / w;
    if (dh > 240 * c->s) { dh = 240 * c->s; dw = dh * w / h; }
    float x = floorf(c->sx + (320 * c->s - dw) / 2), y = floorf(c->sy + (240 * c->s - dh) / 2);
    render_movie(c->r, rgb, w, h, serial, x, y, dw, dh, alpha, c->w, c->h);
    if (c->g->english && mv >= 0) draw_cues(c, MOVIES[mv], movie_time(u->movie), x, y, dw, dh);
}

static void draw_movie(Ctx *c, float a) {
    Ui *u = &c->g->ui;
    render_clear(c->w, c->h);
    if (u->movie_missing) {
        if (u->movie_index == MV_GODS) draw_utf8(c, "GAME OVER", 30, TEXT_SHADOW, 160, 120, 1, 0);
        return;
    }
    draw_movie_frame(c, u->movie_index, 1);
}

static void draw_title(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    render_clear(c->w, c->h);
    draw_movie_frame(c, MV_SLDS, 1);
    /* the four menu items (menu/xxx.anim) slide in from the right (0x2c58), at x = 94, y = 74 + 29 i on the
     * 3DO screen; the choice glows in its colour (frames 1-3 of each anim) */
    static const char *label[4] = {"OPENING", "START", "CONTINUE", "OPERATION"};
    static const uint32_t col[4] = {0x40ff5000u, 0xff3c8c00u, 0xffd22800u, 0x3cdcff00u};
    float p = u->title_t / 1.5f; if (p > 1) p = 1;
    p = 1 - (1 - p) * (1 - p) * (1 - p);
    float out = 1;
    if (u->title_chosen >= 0) { float k = ((float)u->t + a) / (g->hz * 1.5f); out = k < 0.6f ? 1 : 1 - (k - 0.6f) / 0.4f; if (out < 0) out = 0; }
    for (int i = 0; i < 4; i++) {
        float x = 162 + (1 - p) * (320 + i * 40), y = 86 + 29 * i;
        int sel = i == u->title_sel;
        float al = out;
        if (u->title_chosen >= 0 && i != u->title_chosen) al *= 0.25f;
        if (u->title_chosen == i) al *= 0.6f + 0.4f * (((u->t / 2) & 1) ? 1.0f : 0.0f);
        if (sel && p >= 1) draw_utf8(c, label[i], 19, (int)(TEXT_GLOW | col[i]), x, y, al, 0);
        else draw_utf8(c, label[i], 19, TEXT_SHADOW, x, y, al * (p >= 1 ? 0.92f : 1), 0);
    }
}

static void draw_end(Ctx *c, float a) {
    Ui *u = &c->g->ui;
    render_clear(c->w, c->h);
    float f = fade_in_out(u, a, c->g->hz, c->g->hz);
    draw_still(c, "images/END.IMG", f);
}

static void draw_load(Ctx *c, float a) {
    Game *g = c->g;
    Ui *u = &g->ui;
    float f = fade_in_out(u, a, 3, 3);
    render_clear(c->w, c->h);
    draw_still(c, "window/note", f);
    int jp = !g->english;
    draw_utf8(c, jp ? "どのページから続けますか？" : "Continue from which page?", 16, TEXT_INK, 175, 33, f, 0);
    for (int i = 0; i < GAME_SAVE_SLOTS; i++) {
        char info[160], s[220];
        float y = 59 + i * 50;
        int has = game_save_info(g, i, info, sizeof info) == 0;
        if (!has) snprintf(info, sizeof info, "%s", jp ? "―― 空き ――" : "- empty -");
        snprintf(s, sizeof s, "%d   %s", i + 1, info);
        draw_utf8(c, s, 14, TEXT_INK, 86, y, f * (u->sel == i ? 1.0f : 0.5f), -1);
        if (u->sel == i) draw_utf8(c, "▶", 11, TEXT_INK, 72, y, f, 0);
    }
}

static Ctx stage(Game *g, Renderer *r, int w, int h) {
    Ctx c = {g, r, w, h, 0, 0, 0};
    float s = (float)h / 240.0f;
    if (320 * s > w) s = (float)w / 320.0f;
    c.s = s; c.sx = floorf((w - 320 * s) / 2); c.sy = floorf((h - 240 * s) / 2);
    return c;
}

void ui_draw_picture(Game *g, Renderer *r, int w, int h) {
    Ctx c = stage(g, r, w, h);
    render_clear(w, h);
    const char *e = game_image_name(g, g->pic_shown);
    if (!e) return;
    char rel[96];
    snprintf(rel, sizeof rel, "images/%s", e);
    char *dot = strrchr(rel, '.');
    if (dot && (!strcmp(dot, ".img") || !strcmp(dot, ".IMG"))) *dot = 0;
    draw_still(&c, rel, 1);
}

void ui_draw(Game *g, Renderer *r, int w, int h, float alpha) {
    if (g->ui.mode == UI_NONE) return;
    Ctx c = stage(g, r, w, h);
    switch (g->ui.mode) {
    case UI_MENU: draw_menu(&c, alpha); break;
    case UI_PAGES: draw_pages(&c, alpha); break;
    case UI_PICKUP: draw_pickup(&c, alpha); break;
    case UI_PICTURE: draw_picture(&c, alpha); break;
    case UI_SAVE: draw_save(&c, alpha); break;
    case UI_MOVIE: draw_movie(&c, alpha); break;
    case UI_TITLE: draw_title(&c, alpha); break;
    case UI_LOAD: draw_load(&c, alpha); break;
    case UI_END: draw_end(&c, alpha); break;
    }
}
