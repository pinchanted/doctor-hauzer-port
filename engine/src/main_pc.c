/* PC front end (SDL2). Also the headless test harness:
 *   hauzer --data <extracted/fs> [--hd <assets_hd/textures>] [--room N] [--hz 20]
 *          [--shot out.png --size 960x544 --input "UP*40,A,B+UP*10"]    (renders one frame and exits)
 * Keys: arrows = d-pad, Z = A (examine), X = B (run / handle), C = C (jump), Enter = Start (skip),
 *       Q = L (change view: fixed / overhead / first person), W = R, PageUp/PageDown = previous/next room, H = toggle HD textures, F12 = screenshot.
 *       Enter (Start) opens the item menu outside cutscenes. F1-F3 load save slot 1-3, Shift+F1-F3 save to it.
 *   --stills <dir>   upscaled stills (default: assets_hd/stills beside --hd; English set in <dir>_en)
 *   --load N         start from save slot N        --give 5,20   put items in the inventory (testing)
 *   Without --room / --load the game boots like the original: opening movie, then the title menu.
 *   Movies are read from assets_hd/movies (movies_en in English) beside the stills: tools/movies_build.py.
 */
#include "game.h"
#include "render.h"
#include "audio.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#define strtok_r strtok_s
#define strdup _strdup
#endif

static const int ROOMS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211,
                            213, 301, 302, 303, 304, 305};
#define NROOMS ((int)(sizeof ROOMS / sizeof ROOMS[0]))

static uint32_t parse_pad(const char *s) {
    uint32_t pad = 0;
    char buf[64]; snprintf(buf, sizeof buf, "%s", s);
    for (char *t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
        if (!strcmp(t, "UP")) pad |= PAD_UP; else if (!strcmp(t, "DOWN")) pad |= PAD_DOWN;
        else if (!strcmp(t, "LEFT")) pad |= PAD_LEFT; else if (!strcmp(t, "RIGHT")) pad |= PAD_RIGHT;
        else if (!strcmp(t, "A")) pad |= PAD_A; else if (!strcmp(t, "B")) pad |= PAD_B; else if (!strcmp(t, "C")) pad |= PAD_C;
        else if (!strcmp(t, "R")) pad |= PAD_R; else if (!strcmp(t, "L")) pad |= PAD_L;
        else if (!strcmp(t, "START")) pad |= PAD_START;
    }
    return pad;
}

/* "UP*40,A,B+UP*10" -> runs the ticks */
static void run_input(Game *g, const char *spec) {
    char *s = strdup(spec), *save = NULL;
    for (char *item = strtok_r(s, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
        int n = 1;
        char *star = strchr(item, '*');
        if (star) { *star = 0; n = atoi(star + 1); }
        uint32_t pad = parse_pad(item);
        for (int i = 0; i < n; i++) { game_tick(g, pad); ui_update(g, 1.0 / g->hz); }
    }
    free(s);
}

/* the current message, centred near the bottom, text height 1/20 of the screen */
static void draw_message(Renderer *rnd, Game *g, int w, int h) {
    static int key = -1, bw, bh;
    if (g->ui.mode != UI_NONE) return;
    static const uint32_t GAME_OVER[] = {'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R', 0};
    int id = g->gameover ? 9999 : g->msg_id;
    if (!g->gameover && !g->msg_shown) return;
    int px = h / 18 > 12 ? h / 18 : 12;
    if (g->gameover) px *= 2;
    int k = id * 4096 + px;
    Image im = {0};
    if (k != key) {
        if (text_render_box(g->gameover ? GAME_OVER : messages_get(&g->mes, g->msg_id), px, &im)) return;
        bw = im.w; bh = im.h; key = k;
    }
    render_overlay(rnd, k, im.rgba ? &im : NULL, (w - bw) / 2, g->gameover ? (h - bh) / 2 : h - bh - h / 14, w, h);
    image_free(&im);
}

static uint32_t read_pad(SDL_GameController *gc) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t pad = 0;
    if (k[SDL_SCANCODE_UP]) pad |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) pad |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) pad |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) pad |= PAD_RIGHT;
    if (k[SDL_SCANCODE_Z]) pad |= PAD_A;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LSHIFT]) pad |= PAD_B;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_SPACE]) pad |= PAD_C;
    if (k[SDL_SCANCODE_RETURN]) pad |= PAD_START;
    if (k[SDL_SCANCODE_Q]) pad |= PAD_L;
    if (k[SDL_SCANCODE_W]) pad |= PAD_R;
    if (gc) {
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP)) pad |= PAD_UP;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) pad |= PAD_DOWN;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) pad |= PAD_LEFT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) pad |= PAD_RIGHT;
        int ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (ay < -16000) pad |= PAD_UP;
        if (ay > 16000) pad |= PAD_DOWN;
        if (ax < -16000) pad |= PAD_LEFT;
        if (ax > 16000) pad |= PAD_RIGHT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) pad |= PAD_A;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) pad |= PAD_B;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) pad |= PAD_C;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) pad |= PAD_START;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) pad |= PAD_L;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) pad |= PAD_R;
    }
    return pad;
}

int main(int argc, char **argv) {
    const char *data = "extracted/fs", *hd = NULL, *shot = NULL, *input = NULL, *textf = NULL, *fontf = NULL, *stills = NULL;
    const char *give = NULL, *event = NULL;
    int room = 1, hz = 20, w = 1280, h = 720, camera = -1, jp = 0, load = 0, direct = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--hd") && i + 1 < argc) hd = argv[++i];
        else if (!strcmp(argv[i], "--room") && i + 1 < argc) { room = atoi(argv[++i]); direct = 1; }
        else if (!strcmp(argv[i], "--hz") && i + 1 < argc) hz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--camera") && i + 1 < argc) camera = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--text") && i + 1 < argc) textf = argv[++i];
        else if (!strcmp(argv[i], "--font") && i + 1 < argc) fontf = argv[++i];
        else if (!strcmp(argv[i], "--jp")) jp = 1;
        else if (!strcmp(argv[i], "--stills") && i + 1 < argc) stills = argv[++i];
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) load = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--give") && i + 1 < argc) give = argv[++i];
        else if (!strcmp(argv[i], "--event") && i + 1 < argc) event = argv[++i];
    }
    if (hz < 1) hz = 20;
    int gles = getenv("HZ_GLES") != NULL;
    if (shot && !getenv("SDL_VIDEODRIVER")) { SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1); gles = 1; }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (gles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (!shot) { SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1); SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4); }
    SDL_Window *win = SDL_CreateWindow("Doctor Hauzer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                       SDL_WINDOW_OPENGL | (shot ? 0 : SDL_WINDOW_RESIZABLE));
    if (!win && !shot) {   /* no MSAA available: retry without */
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0); SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        win = SDL_CreateWindow("Doctor Hauzer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    }
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "GL context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);

    Renderer *rnd = render_create(gles, hd);
    static Game g;
    char fontbuf[1200];
    if (!fontf) {
        char *base = SDL_GetBasePath();
        path_join(fontbuf, sizeof fontbuf, base ? base : ".", "fonts/NotoSansJP.ttf");
        if (base) SDL_free(base);
        fontf = fontbuf;
    }
    text_init(fontf);
    /* English by default when the converted fan translation is present (tools/make_text_en.py) */
    char textbuf[1200];
    if (!textf && !jp) {
        char *base = SDL_GetBasePath();
        path_join(textbuf, sizeof textbuf, base ? base : ".", "text/mes_en.txt");
        if (base) SDL_free(base);
        FILE *tf = fopen(textbuf, "rb");
        if (tf) { fclose(tf); textf = textbuf; }
    }
    if (!rnd || game_init(&g, data, textf)) { fprintf(stderr, "init failed (data: %s)\n", data); return 1; }
    g.hz = hz;
    g.english = textf != NULL;
    if (textf) {   /* captions for the Japanese baked into the movies: movies_en.txt beside the text file */
        char sp[1200]; snprintf(sp, sizeof sp, "%s", textf);
        char *sl = strrchr(sp, '/'), *bs = strrchr(sp, '\\');
        if (bs > sl) sl = bs;
        if (sl) snprintf(sl + 1, sizeof sp - (size_t)(sl + 1 - sp), "movies_en.txt"); else snprintf(sp, sizeof sp, "movies_en.txt");
        ui_load_movie_text(sp);
    }
    /* upscaled stills: --stills <dir>, else assets_hd/stills next to the --hd textures; English set in <dir>_en */
    {
        char base[1024] = "";
        if (stills) snprintf(base, sizeof base, "%s", stills);
        else if (hd) {
            snprintf(base, sizeof base, "%s", hd);
            size_t n = strlen(base);
            while (n && (base[n - 1] == '/' || base[n - 1] == '\\')) base[--n] = 0;
            char *sl = strrchr(base, '/'), *bs = strrchr(base, '\\');
            if (bs > sl) sl = bs;
            if (sl) snprintf(sl + 1, sizeof base - (size_t)(sl + 1 - base), "stills"); else snprintf(base, sizeof base, "stills");
        }
        if (base[0]) {
            snprintf(g.stills[1], sizeof g.stills[1], "%s", base); snprintf(g.stills[0], sizeof g.stills[0], "%s_en", base);
            /* movies beside the stills: assets_hd/movies and movies_en */
            snprintf(g.movies[1], sizeof g.movies[1], "%s", base);
            size_t n = strlen(g.movies[1]);
            if (n >= 6 && !strcmp(g.movies[1] + n - 6, "stills")) {
                g.movies[1][n - 6] = 0;
                strncat(g.movies[1], "movies", sizeof g.movies[1] - strlen(g.movies[1]) - 1);
            } else snprintf(g.movies[1], sizeof g.movies[1], "movies");
            snprintf(g.movies[0], sizeof g.movies[0], "%s_en", g.movies[1]);
        }
        log_msg("stills: %s, movies: %s", base[0] ? base : "(original pictures from the disc)", g.movies[1][0] ? g.movies[1] : "-");
    }
    {
        char *pref = SDL_GetPrefPath("Pinchanted", "DoctorHauzer");
        if (pref) { snprintf(g.save_dir, sizeof g.save_dir, "%s", pref); SDL_free(pref); }
    }
    if (give) for (const char *p = give; *p;) { int it = atoi(p); if (it > 0) game_add_item(&g, it); while (*p && *p != ',') p++; if (*p) p++; }
    if (!shot) audio_init(g.orgdata);
    render_set_human(rnd, &g.human);
    if (game_load_room(&g, room, rnd)) { fprintf(stderr, "cannot load room %d\n", room); return 1; }
    if (load > 0) direct = 1;
    if (direct) g.ui.in_game = 1;
    else ui_boot(&g);                                /* opening movie, then the title (--room N starts in a room) */
    if (load > 0 && game_load_save(&g, load - 1)) fprintf(stderr, "no save in slot %d\n", load);

    if (shot) {
        if (event) {   /* testing: "PRE*n" ticks first, then fire the script event a,b,c */
            int a = 0, b = 0, c = 0, pre = 0;
            if (sscanf(event, "%d*%d,%d,%d,%d", &pre, &pre, &a, &b, &c) != 5) { pre = 0; sscanf(event, "%d,%d,%d", &a, &b, &c); }
            for (int i = 0; i < pre; i++) game_tick(&g, 0);
            script_event(&g.script, &g, a, b, c);
        }
        if (input) run_input(&g, input);
        if (camera >= 0 && camera < g.room.ncams) g.camera = camera;
        CameraView cam;
        game_view(&g, 1.0f, &cam);
        if (!ui_covers_screen(&g)) {
            render_frame(rnd, &g.room, g.area, &cam, &g.player_tris, &g.overlay_tris, w, h);
            render_dim(rnd, game_fade_level(&g, 1.0f), w, h);
            draw_message(rnd, &g, w, h);
        }
        ui_draw(&g, rnd, w, h, 1.0f);
        int rc = render_screenshot(w, h, shot);
        log_msg("room %03d area %d: player at (%g, %g, %g) ry %g, camera %d (mode %d) -> %s", g.room_id, g.area,
                g.player.pos[0], g.player.pos[1], g.player.pos[2], g.player.ry, g.camera, g.cam_mode, shot);
        return rc ? 1 : 0;
    }

    SDL_GameController *gc = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { gc = SDL_GameControllerOpen(i); break; }
    int ri = 0;
    for (int i = 0; i < NROOMS; i++) if (ROOMS[i] == room) ri = i;
    int use_hd = hd != NULL, running = 1, shots = 0;
    double step = 1.0 / hz, acc = 0;
    Uint64 last = SDL_GetPerformanceCounter(), last_ui = last;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e.cdevice.which);
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                else if (k == SDLK_PAGEDOWN || k == SDLK_PAGEUP) {
                    ri = (ri + (k == SDLK_PAGEDOWN ? 1 : NROOMS - 1)) % NROOMS;
                    game_load_room(&g, ROOMS[ri], rnd);
                } else if (k == SDLK_h && hd) {
                    use_hd = !use_hd;
                    render_destroy(rnd);
                    rnd = render_create(gles, use_hd ? hd : NULL);
                    g.renderer = rnd;
                    render_set_room(rnd, &g.room); render_set_human(rnd, &g.human);
                    log_msg("textures: %s", use_hd ? "HD" : "original");
                } else if (k >= SDLK_F1 && k <= SDLK_F3 && (e.key.keysym.mod & KMOD_SHIFT)) {
                    game_save(&g, (int)(k - SDLK_F1));           /* Shift+F1..F3: quick save to slot 1..3 */
                } else if (k >= SDLK_F1 && k <= SDLK_F3) {
                    if (game_load_save(&g, (int)(k - SDLK_F1))) log_msg("slot %d is empty", (int)(k - SDLK_F1) + 1);
                } else if (k == SDLK_F12) {
                    char name[64]; snprintf(name, sizeof name, "hauzer_%03d.png", shots++);
                    int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
                    render_screenshot(dw, dh, name);
                    log_msg("saved %s", name);
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (acc > 0.25) acc = 0.25;
        uint32_t pad = read_pad(gc);
        ui_update(&g, (double)(now - last_ui) / (double)SDL_GetPerformanceFrequency());
        last_ui = now;
        while (acc >= step) { game_tick(&g, pad); acc -= step; }
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        CameraView cam;
        float alpha = (float)(acc / step);
        game_view(&g, alpha, &cam);
        if (!ui_covers_screen(&g)) {
            render_frame(rnd, &g.room, g.area, &cam, &g.player_tris, &g.overlay_tris, dw, dh);
            render_dim(rnd, game_fade_level(&g, alpha), dw, dh);
            draw_message(rnd, &g, dw, dh);
        }
        ui_draw(&g, rnd, dw, dh, alpha);
        SDL_GL_SwapWindow(win);
    }
    audio_shutdown();
    render_destroy(rnd);
    game_free(&g);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
