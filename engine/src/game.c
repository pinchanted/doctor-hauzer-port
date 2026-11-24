#include "game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* start.script: "ROOM001  1 x y z ..." (room table with spawn positions) */
static void load_spawns(Game *g) {
    char p[1200];
    path_join(p, sizeof p, g->orgdata, "start.script");
    Blob b = blob_load(p);
    if (!b.data) return;
    char *s = malloc(b.size + 1);
    memcpy(s, b.data, b.size); s[b.size] = 0;
    char *tok = strtok(s, " \t\r\n");
    while (tok && g->spawn_count < 64) {
        if (!strncmp(tok, "ROOM", 4)) {
            int v[4], ok = 1;
            for (int i = 0; i < 4; i++) { char *t = strtok(NULL, " \t\r\n"); if (!t) { ok = 0; break; } v[i] = atoi(t); }
            if (!ok) break;
            g->spawns[g->spawn_count].id = v[0];
            for (int i = 0; i < 3; i++) g->spawns[g->spawn_count].pos[i] = (float)v[1 + i];
            g->spawn_count++;
        }
        tok = strtok(NULL, " \t\r\n");
    }
    free(s);
    blob_free(&b);
}

int game_init(Game *g, const char *fs_dir) {
    memset(g, 0, sizeof *g);
    snprintf(g->fs_dir, sizeof g->fs_dir, "%s", fs_dir);
    path_join(g->orgdata, sizeof g->orgdata, fs_dir, "OrgData");
    char exe[1200];
    path_join(exe, sizeof exe, fs_dir, "launchme");
    if (human_load(&g->human, g->orgdata, exe)) return -1;
    anim_init(&g->anim, &g->human, 2);
    load_spawns(g);
    return 0;
}

static void pick_camera(Game *g) {
    int z = room_zone_at(&g->room, g->player.pos[0], g->player.pos[2]);
    if (z >= 0 && z != 99 && z < g->room.ncams) g->camera = z;
}

int game_load_room(Game *g, int room_id, Renderer *r) {
    char name[64], path[1200];
    snprintf(name, sizeof name, "room/room%03d.bin", room_id);
    path_join(path, sizeof path, g->orgdata, name);
    Room nr;
    if (room_load(&nr, path)) return -1;
    player_free(&g->player);
    room_free(&g->room);
    g->room = nr;
    g->room_id = room_id;
    g->area = 0;
    float pos[3] = {0, 200, 0};
    for (int i = 0; i < g->spawn_count; i++) if (g->spawns[i].id == room_id) memcpy(pos, g->spawns[i].pos, sizeof pos);
    player_init(&g->player, &g->room, pos, 0, 0);
    memcpy(g->prev_pos, pos, sizeof pos);
    g->prev_ry = 0;
    memcpy(g->prev_body, g->anim.cur, sizeof g->prev_body);
    g->camera = g->room.ncams > 1 ? 1 : 0;
    pick_camera(g);
    if (r) render_set_room(r, &g->room);
    log_msg("room %03d: %d objects, %d cameras, spawn (%g, %g, %g), camera %d", room_id, g->room.nobjects,
            g->room.ncams, pos[0], pos[1], pos[2], g->camera);
    return 0;
}

void game_tick(Game *g, uint32_t pad) {
    memcpy(g->prev_pos, g->player.pos, sizeof g->prev_pos);
    g->prev_ry = g->player.ry;
    memcpy(g->prev_body, g->anim.cur, sizeof g->prev_body);
    int n = player_tick(&g->player, pad);
    for (int i = 0; i < n; i++) {
        Event *e = &g->player.events[i];
        if (e->kind != 1)   /* floor events fire every tick while standing on one; keep the log readable */
            log_msg("event ON (%d, %d, %d)", e->trigger, e->kind, e->item);
    }
    anim_set(&g->anim, g->player.anim);
    anim_step(&g->anim, &g->human);
    pick_camera(g);
    g->ticks++;
}

void game_view(Game *g, float alpha, CameraView *cam) {
    const RoomCamera *c = &g->room.cams[g->camera < g->room.ncams ? g->camera : 0];
    memcpy(cam->eye, c->eye, sizeof cam->eye);
    memcpy(cam->target, c->target, sizeof cam->target);
    /* display-rate interpolation between the last two logic ticks */
    float pos[3], body[128][3];
    for (int k = 0; k < 3; k++) pos[k] = g->prev_pos[k] + (g->player.pos[k] - g->prev_pos[k]) * alpha;
    float d = g->player.ry - g->prev_ry;
    if (d > 128) d -= 256;
    if (d < -128) d += 256;
    float ry = g->prev_ry + d * alpha;
    for (int i = 0; i < g->anim.nverts; i++)
        for (int k = 0; k < 3; k++) body[i][k] = g->prev_body[i][k] + (g->anim.cur[i][k] - g->prev_body[i][k]) * alpha;
    trilist_reset(&g->player_tris);
    if (g->area == 0) human_build(&g->human, (const float (*)[3])body, pos, ry, &g->player_tris);
}

void game_free(Game *g) {
    player_free(&g->player);
    room_free(&g->room);
    human_free(&g->human);
    free(g->player_tris.v);
}
