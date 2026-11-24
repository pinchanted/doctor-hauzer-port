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
    g->hz = 20;
    snprintf(g->fs_dir, sizeof g->fs_dir, "%s", fs_dir);
    path_join(g->orgdata, sizeof g->orgdata, fs_dir, "OrgData");
    char exe[1200];
    path_join(exe, sizeof exe, fs_dir, "launchme");
    if (human_load(&g->human, g->orgdata, exe)) return -1;
    anim_init(&g->anim, &g->human, 2);
    load_spawns(g);
    g->pending_room = g->pending_script = -1;
    return 0;
}

static RoomObject *room_obj(Game *g, int h) {
    for (int i = 0; i < g->room.nobjects; i++) if (g->room.objects[i].id == h) return &g->room.objects[i];
    return NULL;
}

/* The original only re-aims the camera when the zone under the player changes (or the script resets
 * it), so a camera set by a script stays until the player walks into another zone. */
static void pick_camera(Game *g) {
    int z = room_zone_at(&g->room, g->player.pos[0], g->player.pos[2]);
    if (g->cam_mode != CAM_ZONE) {
        if (z == g->cam_zone || g->script.suspended) return;
        g->cam_mode = CAM_ZONE;
    }
    if (z >= 0 && z != 99 && z < g->room.ncams) g->camera = z;
}

static void script_takes_camera(Game *g, int mode) {
    if (g->cam_mode == CAM_ZONE) g->cam_zone = room_zone_at(&g->room, g->player.pos[0], g->player.pos[2]);
    g->cam_mode = mode;
}

static int load_room_data(Game *g, int room_id, int script_id, int keep_player, Renderer *r) {
    char name[64], path[1200];
    snprintf(name, sizeof name, "room/room%03d.bin", room_id);
    path_join(path, sizeof path, g->orgdata, name);
    Room nr;
    if (room_load(&nr, path)) return -1;
    float pos[3] = {0, 200, 0}, ry = 0;
    if (keep_player) { memcpy(pos, g->player.pos, sizeof pos); ry = g->player.ry; }
    else for (int i = 0; i < g->spawn_count; i++) if (g->spawns[i].id == room_id) memcpy(pos, g->spawns[i].pos, sizeof pos);
    player_free(&g->player);
    room_free(&g->room);
    script_free(&g->script);
    g->room = nr;
    g->room_id = room_id;
    g->area = 0;
    snprintf(name, sizeof name, "macro/macro%03d", script_id);
    path_join(path, sizeof path, g->orgdata, name);
    script_load(&g->script, path);
    player_init(&g->player, &g->room, pos, ry, 0);
    memcpy(g->prev_pos, pos, sizeof pos);
    g->prev_ry = ry;
    memcpy(g->prev_body, g->anim.cur, sizeof g->prev_body);
    g->cam_mode = CAM_ZONE;
    g->camera = g->room.ncams > 1 ? 1 : 0;
    g->player_hidden = 0;
    pick_camera(g);
    if (r) render_set_room(r, &g->room);
    log_msg("room %03d (script %03d): %d objects, %d cameras, player (%g, %g, %g), camera %d", room_id, script_id,
            g->room.nobjects, g->room.ncams, pos[0], pos[1], pos[2], g->camera);
    script_event(&g->script, g, 1, 0, 0);            /* room entered */
    return 0;
}

int game_load_room(Game *g, int room_id, Renderer *r) {
    if (r) g->renderer = r;
    return load_room_data(g, room_id, room_id, 0, g->renderer);
}

static void apply_pending(Game *g) {
    if (g->script.suspended) return;
    if (g->pending_room >= 0) {
        int room = g->pending_room, scr = g->pending_script >= 0 ? g->pending_script : room;
        g->pending_room = g->pending_script = -1;
        load_room_data(g, room, scr, 1, g->renderer);
    }
}

void game_tick(Game *g, uint32_t pad) {
    memcpy(g->prev_pos, g->player.pos, sizeof g->prev_pos);
    g->prev_ry = g->player.ry;
    memcpy(g->prev_body, g->anim.cur, sizeof g->prev_body);
    g->skip_pressed = (pad & (PAD_START | PAD_X)) != 0;
    if (g->msg_frames > 0) g->msg_frames--;
    if (g->script.suspended) {                       /* a cutscene is playing: the script owns the frame */
        script_resume(&g->script, g);
        apply_pending(g);
        g->ticks++;
        return;
    }
    int n = player_tick(&g->player, pad);
    anim_set(&g->anim, g->player.anim);
    anim_step(&g->anim, &g->human);
    script_event(&g->script, g, 2, 0, 0);            /* every frame */
    for (int i = 0; i < n && !g->script.suspended; i++) {
        Event *e = &g->player.events[i];
        if (e->kind != 1) log_msg("event ON (%d, %d, %d)", e->trigger, e->kind, e->item);
        script_event(&g->script, g, e->trigger, e->kind, e->item);
    }
    apply_pending(g);
    pick_camera(g);
    g->ticks++;
}

static void forward_from_rot(const float rot[3], float f[3]) {
    float z[3] = {0, 0, 1}, o[3] = {0, 0, 0};
    obj_transform(o, rot, z, f);
}

void game_view(Game *g, float alpha, CameraView *cam) {
    if (g->cam_mode == CAM_ACTOR) {
        memcpy(cam->eye, g->cam_pos, sizeof cam->eye);
        if (g->cam_has_target) memcpy(cam->target, g->cam_target, sizeof cam->target);
        else { float f[3]; forward_from_rot(g->cam_rot, f); for (int k = 0; k < 3; k++) cam->target[k] = g->cam_pos[k] + f[k] * 1000; }
    } else {
        const RoomCamera *c = &g->room.cams[g->camera < g->room.ncams ? g->camera : 0];
        memcpy(cam->eye, c->eye, sizeof cam->eye);
        memcpy(cam->target, c->target, sizeof cam->target);
    }
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
    if (g->area == 0 && !g->player_hidden) human_build(&g->human, (const float (*)[3])body, pos, ry, &g->player_tris);
}

void game_free(Game *g) {
    player_free(&g->player);
    room_free(&g->room);
    script_free(&g->script);
    human_free(&g->human);
    free(g->player_tris.v);
}

/* ---------------------------------------------------------------- actors for the script VM */
void game_actor_set(Game *g, int h, int mode, float x, float y, float z) {
    float v[3] = {x, y, z}, *pos = NULL, *rot = NULL;
    float prot[3] = {0, g->player.ry, 0};
    RoomObject *o;
    if (h == 0) { pos = g->cam_pos; rot = g->cam_rot; script_takes_camera(g, CAM_ACTOR); if (mode == 1 || mode == 3) g->cam_has_target = 0; }
    else if (h == 1) { pos = g->player.pos; rot = prot; }
    else if (h >= 100 && (o = room_obj(g, h))) { pos = o->pos; rot = o->rot; }
    else if (h > 1 && h < 100) { pos = g->actor[h].pos; rot = g->actor[h].rot; }
    if (!pos) return;
    switch (mode) {
    case 0: memcpy(pos, v, sizeof v); break;
    case 1: memcpy(rot, v, sizeof v); break;
    case 2: for (int k = 0; k < 3; k++) pos[k] += v[k]; break;
    case 3: for (int k = 0; k < 3; k++) rot[k] += v[k]; break;
    }
    if (h == 1) {
        g->player.ry = fmodf(prot[1] + 256.0f * 8, 256.0f);
        if (mode == 0 || mode == 2) { memcpy(g->prev_pos, g->player.pos, sizeof g->prev_pos); }
    }
}

void game_actor_get(Game *g, int h, float out[3]) {
    RoomObject *o;
    memset(out, 0, sizeof(float) * 3);
    if (h == 0) memcpy(out, g->cam_pos, sizeof(float) * 3);
    else if (h == 1) memcpy(out, g->player.pos, sizeof(float) * 3);
    else if (h == 60) { memcpy(out, g->player.pos, sizeof(float) * 3); out[1] -= 280; }   /* head */
    else if (h >= 100 && (o = room_obj(g, h))) memcpy(out, o->pos, sizeof(float) * 3);
    else if (h > 1 && h < 100) memcpy(out, g->actor[h].pos, sizeof(float) * 3);
}

void game_actor_show(Game *g, int h, int on) {
    RoomObject *o;
    if (h >= 100 && (o = room_obj(g, h))) o->visible = on;
    else if (h > 1 && h < 100) g->actor[h].visible = on;
}

void game_actor_area(Game *g, int h, int area) {
    RoomObject *o;
    if (h >= 100 && (o = room_obj(g, h))) o->area = area;
    else if (h > 1 && h < 100) g->actor[h].area = area;
}

void game_actor_move_fwd(Game *g, int h, float d) {
    float rot[3] = {0, 0, 0}, o3[3] = {0, 0, 0}, v[3] = {0, 0, d}, w[3];
    RoomObject *o;
    float *pos = NULL;
    if (h == 0) { pos = g->cam_pos; memcpy(rot, g->cam_rot, sizeof rot); }
    else if (h == 1) { pos = g->player.pos; rot[1] = g->player.ry; }
    else if (h >= 100 && (o = room_obj(g, h))) { pos = o->pos; memcpy(rot, o->rot, sizeof rot); }
    if (!pos) return;
    obj_transform(o3, rot, v, w);
    for (int k = 0; k < 3; k++) pos[k] += floorf(w[k]);
}

void game_actor_look_at(Game *g, int h, int target, const float *point) {
    float t[3];
    if (point) memcpy(t, point, sizeof t); else game_actor_get(g, target, t);
    if (h == 0) { memcpy(g->cam_target, t, sizeof t); g->cam_has_target = 1; script_takes_camera(g, CAM_ACTOR); return; }
    float p[3]; game_actor_get(g, h, p);
    float dx = t[0] - p[0], dz = t[2] - p[2];
    if (fabsf(dx) + fabsf(dz) < 1e-3f) return;
    /* actors face local -z: ry = atan2(-dx, -dz) in 256ths of a turn */
    float ry = atan2f(-dx, -dz) * 256.0f / (2.0f * 3.14159265f);
    if (ry < 0) ry += 256;
    if (h == 1) g->player.ry = ry;
    else { RoomObject *o = h >= 100 ? room_obj(g, h) : NULL; if (o) o->rot[1] = ry; else if (h > 1 && h < 100) g->actor[h].rot[1] = ry; }
}

void game_actor_no_collide(Game *g, int h) {
    for (int i = 0; i < g->player.nvols; i++) if (g->player.vols[i].owner && g->player.vols[i].owner->id == h) g->player.vols[i].solid = 0;
}

int game_actor_touch(Game *g, int a, int b) {
    float pa[3], pb[3];
    game_actor_get(g, a, pa); game_actor_get(g, b, pb);
    return fabsf(pa[0] - pb[0]) < 120 && fabsf(pa[2] - pb[2]) < 120;   /* approximation of 0x10ea4 */
}

void game_set_area(Game *g, int area) {
    if (area == g->area) return;
    g->area = area;
    g->player.area = area;
    player_rebuild_volumes(&g->player);
}

void game_camera_fixed(Game *g, int id) {
    if (id < 0 || id >= g->room.ncams) return;
    script_takes_camera(g, CAM_FIXED);
    g->camera = id;
}

void game_camera_reset(Game *g) {
    g->cam_mode = CAM_ZONE;
    pick_camera(g);
}

void game_message(Game *g, int id, int time, int reset) {
    g->msg_id = id;
    g->msg_frames = g->hz * 3;
    log_msg("  MESSAGE %d (time %d%s)", id, time, reset ? ", new box" : "");
}
