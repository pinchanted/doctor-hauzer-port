/* Player control, movement and collision - port of tools/player.py (launchme 0x5578 input,
 * 0x5aa4 states, 0x10ee0 collision). See docs/FORMATS.md "Player control". */
#pragma once
#include "room.h"

/* 3DO ControlPad bits */
enum {
    PAD_DOWN = (int)0x80000000u, PAD_UP = 0x40000000, PAD_RIGHT = 0x20000000, PAD_LEFT = 0x10000000,
    PAD_A = 0x08000000, PAD_B = 0x04000000, PAD_C = 0x02000000, PAD_START = 0x01000000,
    PAD_X = 0x00800000, PAD_R = 0x00400000, PAD_L = 0x00200000,
};

/* lo/hi are world coordinates for grid cells; for room objects they are relative to the owner's
 * position, which is added at test time (the game keeps boxes actor-relative, 0x5c498) */
typedef struct { float lo[3], hi[3]; int solid, trigger; RoomObject *owner; } Box;

typedef struct { int kind, trigger, item; } Event;   /* kind 1 floor, 2 examine, 3 use item */

typedef struct {
    Room *room;
    int area;
    float pos[3];
    float ry;                 /* 256 per turn */
    int anim;                 /* animation slot the movement asked for */
    int busy;                 /* ticks left of a jump */
    Box *vols; int nvols;
    Event events[8]; int nevents;
} Player;

void player_init(Player *p, Room *r, const float pos[3], float ry, int area);
void player_free(Player *p);
void player_rebuild_volumes(Player *p);
int player_tick(Player *p, uint32_t pad);   /* returns number of events raised */
