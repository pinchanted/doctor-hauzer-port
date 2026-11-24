/* Game state: current room, player, camera selection, fixed-rate logic. */
#pragma once
#include "human.h"
#include "player.h"
#include "render.h"
#include "room.h"

typedef struct {
    char fs_dir[1024];        /* extracted disc root (contains launchme and OrgData/) */
    char orgdata[1100];
    Room room;
    int room_id;
    HumanModel human;
    AnimState anim;
    Player player;
    int area;
    int camera;
    float prev_pos[3], prev_ry;
    float prev_body[128][3];
    TriList player_tris;
    uint64_t ticks;
    int spawn_count;
    struct { int id; float pos[3]; } spawns[64];
} Game;

int game_init(Game *g, const char *fs_dir);
int game_load_room(Game *g, int room_id, Renderer *r);
void game_tick(Game *g, uint32_t pad);
void game_view(Game *g, float alpha, CameraView *cam);   /* also builds g->player_tris */
void game_free(Game *g);
