/* Game state: current room and script, actors, player, camera, fixed-rate logic. */
#pragma once
#include "human.h"
#include "player.h"
#include "render.h"
#include "room.h"
#include "script.h"
#include "text.h"
#include "ui.h"

#define GAME_SAVE_SLOTS 3

/* actor handles (launchme's 56-byte actor table at 0x58aa0): 0 camera, 1 player body,
 * 3-49 poses, 50-59 close-up heads, 60 head, 61+ hands/panels/items, 100+ room objects */
typedef struct { float pos[3], rot[3]; int visible, area; } SmallActor;

enum { CAM_ZONE, CAM_FIXED, CAM_ACTOR };

typedef struct Game {
    char fs_dir[1024];        /* extracted disc root (contains launchme and OrgData/) */
    char orgdata[1100];
    Room room;
    int room_id;
    Script script;
    HumanModel human;
    AnimState anim;
    Player player;
    int area;
    int hz;
    /* camera */
    int cam_mode, camera, cam_zone;   /* zone under the player when the script took the camera */
    float cam_pos[3], cam_rot[3], cam_target[3];
    int cam_has_target;
    SmallActor actor[100];
    int player_hidden, skip_pressed;
    int view_mode;            /* L button: 0 fixed cameras, 1 overhead, 2 first person (launchme 0x4a9c) */
    int cam_reset_pending;    /* CAMERA_RESET takes effect when the script run ends (0x4b8c) */
    uint32_t prev_pad;
    int zone_event;
    int footstep_slot;        /* 0 = default footstep (system SE 0), else effect slot (FC7F, 0x6418) */
    /* screen fades (FADE_OUT / FADE_IN, 0x1a40 / 0x1a8c) and game over (EXIT -> 0xc90) */
    float fade_from, fade_to; int fade_t, fade_n, fade_snap_back;
    int gameover;             /* ticks left of the game-over card; 0 = playing */
    int gameover_msg;
    int pending_room, pending_script, exit_requested;
    /* messages (launchme 0x4868 queue / 0x48fc pop / 0x4a28 per-frame countdown) */
    Messages mes;
    struct { int id, time; } mq[32];
    int msg_id, msg_frames, msg_shown;
    int msg_scale_num, msg_scale_den;   /* original frames -> our ticks */
    /* interpolation */
    float prev_pos[3], prev_ry;
    float prev_body[128][3];
    TriList player_tris, overlay_tris;
    uint64_t ticks;
    int spawn_count;
    struct { int id; float pos[3]; } spawns[64];
    Renderer *renderer;
    int script_id;
    /* items: inventory slots (launchme 0x3e088, 40 ids, packed from slot 0) and the menu cursor (0x32064) */
    int items[40], item_cursor;
    int use_item;             /* chosen with "use" in the menu; applied on the next tick (0x55bc) */
    Ui ui;
    TriList ui_tris;
    int english;              /* English text and item names */
    char stills[2][1024];     /* upscaled stills: [0] English set, [1] original set (PNG, OrgData layout) */
    char save_dir[1024];
    int last_save_slot;
    char image_names[32][32]; /* images/image.scp */
    char stream_names[32][16];/* stream/stream.scp: OPDS, OPDS, SLDS, MESDS, STDS, GODS, EDDS, TRDS */
    char movies[2][1024];     /* rebuilt movies (.mpg): [0] English set, [1] original */
    /* PICTURE (0x19b4): fade the screen out, then the picture in; it stays on screen until the script
     * draws a frame, a FADE_OUT clears the screen, or the script run ends */
    int pic_shown, pic_next, pic_phase, pic_fade_in;
    int wait_button;          /* WAIT_BUTTON (0xc470): 1 = waiting for release, 2 = for a press */
    int ending_requested;     /* ENDING (0xfc6b, room 313) */
    int cinematic;            /* the credits or an attract demo: LOAD_ROOM switches at once, no room events */
    int cinematic_kind;       /* 1 credits, 2..4 demo 1..3 */
} Game;

int game_init(Game *g, const char *fs_dir, const char *text_override);
int game_load_room(Game *g, int room_id, Renderer *r);           /* spawn from start.script, fires event 1 */
void game_tick(Game *g, uint32_t pad);
void game_view(Game *g, float alpha, CameraView *cam);          /* also builds g->player_tris */
void game_free(Game *g);

/* used by the script VM */
void game_actor_set(Game *g, int h, int mode, float x, float y, float z);  /* 0 pos, 1 rot, 2 add pos, 3 add rot */
void game_actor_get(Game *g, int h, float out[3]);
void game_actor_show(Game *g, int h, int on);
void game_actor_area(Game *g, int h, int area);
void game_actor_move_fwd(Game *g, int h, float d);
void game_actor_look_at(Game *g, int h, int target, const float *point);
void game_actor_no_collide(Game *g, int h);
int game_actor_touch(Game *g, int a, int b);
void game_set_area(Game *g, int area);
void game_camera_fixed(Game *g, int id);
void game_camera_reset(Game *g);
void game_message(Game *g, int id, int time, int reset);
void game_fade(Game *g, int out, int vblanks);
float game_fade_level(const Game *g, float alpha);

/* inventory (0xe398 has, 0xe344 add, 0xe424 remove) */
int game_has_item(const Game *g, int item);
int game_add_item(Game *g, int item);
void game_remove_item(Game *g, int item);
const char *game_image_name(const Game *g, int n);    /* image.scp entry n (file name) */
const char *game_stream_name(const Game *g, int n);   /* stream.scp entry n (OPDS, SLDS, ...) */
void game_new(Game *g);                               /* fresh state, room 001 */
int game_start_cinematic(Game *g, const char *macro, int kind);   /* macroEnd / macroDemoN (0xf70, 0xdf8) */
void game_show_picture(Game *g, int n, int fade_out, int fade_in);
void game_load_room_now(Game *g, int room_id);
void game_clear_picture(Game *g);

/* saves: <save_dir>/hauzer<N>.sav */
int game_save(Game *g, int slot);
int game_save_info(const Game *g, int slot, char *out, int cap);   /* 0 if the slot holds a save */
int game_load_save(Game *g, int slot);
