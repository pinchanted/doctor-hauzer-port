/* Modal screens: the Start-button item menu (launchme 0xF26C), examine pages (isearch/), the item
 * pick-up prompt (ITEM_SCREEN, 0xECD4), full-screen pictures (PICTURE, 0x19B4 with images/image.scp)
 * and the notebook save screen (item 28). While one is open the game and the script are frozen.
 *
 * Drawing is high resolution: the 4x upscaled stills fill a 4:3 stage, item models are rendered at
 * screen resolution with HD textures, and all text is drawn with the TrueType font. */
#pragma once
#include <stdint.h>
#include "render.h"

struct Game;

enum { UI_NONE, UI_MENU, UI_PICKUP, UI_PICTURE, UI_PAGES, UI_SAVE, UI_MOVIE, UI_TITLE, UI_LOAD };

/* front-end flow steps (launchme 0x680): movies by stream.scp index, the title, a new game, back to the game */
enum { STEP_TITLE = 1, STEP_NEWGAME, STEP_RESUME, STEP_GAMEOVER_CARD, STEP_MOVIE = 100 };
struct Movie;

typedef struct {
    int mode, t;              /* t = ticks since the screen opened */
    int sel;                  /* menu: 0 use / 1 examine; pick-up: 0 yes / 1 no; save: slot */
    int item, page;
    float rot[3], rot_prev[3], vel[3];
    float slide, slide_prev;  /* model x offset (menu item change, page slide-in) */
    int picture;
    int closing;              /* fade-out ticks left; the screen finishes when it reaches 0 */
    int script_label;         /* ITEM_SCREEN: script jump target when the item is taken, -1 = none */
    int taken;
    int back_to_menu;         /* examine pages return to the menu */
    int pickup_after;         /* item to offer when the screen closes (bronze key in the leather book) */
    int saved;                /* save screen: slot just written (feedback) */
    int picture_fade;         /* PICTURE fade length in original frames */
    uint32_t prev_pad;
    /* front end */
    int queue[8], nq;         /* steps still to run */
    struct Movie *movie;
    int movie_index, movie_done, movie_missing;
    int title_phase, title_sel, title_chosen;
    float title_t;            /* seconds since the title opened */
    int in_game;              /* a game is running under the screens (else the front end owns everything) */
} Ui;

int ui_open_menu(struct Game *g);
int ui_open_pickup(struct Game *g, int item, int script_target);   /* script_target < 0: not from a script */
int ui_open_picture(struct Game *g, int n, int fade);
int ui_play_movie(struct Game *g, int stream_index);                /* MOVIE n from a script */
void ui_boot(struct Game *g);                                        /* opening movie, then the title */
void ui_game_over(struct Game *g);
void ui_update(struct Game *g, double seconds);                      /* real time: movie decoding */
int ui_open_save(struct Game *g);
void ui_tick(struct Game *g, uint32_t pad);
int ui_covers_screen(const struct Game *g);                         /* the room need not be drawn */
void ui_draw(struct Game *g, Renderer *r, int w, int h, float alpha);
const char *ui_item_name_en(int id);
void ui_load_movie_text(const char *path);                          /* English captions for the movies */
