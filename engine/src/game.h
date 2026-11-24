/* Game state: current room and script, actors, player, camera, fixed-rate logic. */
#pragma once
#include "human.h"
#include "player.h"
#include "render.h"
#include "room.h"
#include "script.h"
#include "text.h"

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
