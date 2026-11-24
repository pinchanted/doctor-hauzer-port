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

int game_init(Game *g, const char *fs_dir, const char *text_override) {
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
    char mes[1200];
    path_join(mes, sizeof mes, g->orgdata, "mes/mes.scp");
    messages_load(&g->mes, mes, exe, text_override);
    /* the original counted message time in rendered frames; it drew rooms at roughly 10 fps */
    g->msg_scale_num = 1; g->msg_scale_den = 10;
    return 0;
}

static void message_tick(Game *g) {
    if (g->msg_frames > 0) { g->msg_frames--; return; }
    /* pop the next queued message: time 0 = queue empty (box closes); id 0 = a pause without text */
    if (g->mq[0].time == 0) { g->msg_shown = 0; g->msg_id = 0; }
    else {
        g->msg_id = g->mq[0].id;
        g->msg_shown = g->msg_id != 0 && messages_get(&g->mes, g->msg_id) != NULL;
        g->msg_frames = g->mq[0].time * g->hz * g->msg_scale_num / g->msg_scale_den;
    }
    memmove(&g->mq[0], &g->mq[1], sizeof g->mq[0] * 31);
    memset(&g->mq[31], 0, sizeof g->mq[31]);
}

static void finish_script_frame(Game *g);

static RoomObject *room_obj(Game *g, int h) {
    for (int i = 0; i < g->room.nobjects; i++) if (g->room.objects[i].id == h) return &g->room.objects[i];
    return NULL;
}

/* The original only re-aims the camera when the zone under the player changes (or the script resets
 * it), so a camera set by a script stays until the player walks into another zone. */
static void pick_camera(Game *g) {
    int z = room_zone_at(&g->room, g->player.pos[0], g->player.pos[2]);
    if (g->camera < 0) g->camera = g->room.ncams > 1 ? 1 : 0;
    if (g->cam_mode != CAM_ZONE) {
        if (z == g->cam_zone || g->script.suspended) return;
        g->cam_mode = CAM_ZONE;
    }
    if (z >= 0 && z != 99 && z < g->room.ncams && z != g->camera) {
        g->camera = z;
        g->zone_event = z + 10;          /* 0x4b8c fires event (zone + 10) when the camera changes */
    }
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
    g->cam_reset_pending = 0;
    memset(g->actor, 0, sizeof g->actor);
    memset(g->mq, 0, sizeof g->mq); g->msg_frames = 0; g->msg_shown = 0;
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

static void fade_tick(Game *g) {
    if (g->fade_n <= 0) return;
    if (++g->fade_t >= g->fade_n) {
        g->fade_n = 0;
        if (g->fade_snap_back) { g->fade_from = g->fade_to = 0; g->fade_snap_back = 0; }
        else g->fade_from = g->fade_to;
    }
}

/* FADE_OUT: to black over n vblanks, then the brightness snaps back over a cleared screen, so the next
 * drawn frame appears at once (0x1a40). FADE_IN: from black over n vblanks (0x1a8c). */
void game_fade(Game *g, int out, int vblanks) {
    g->fade_from = out ? 0.0f : 1.0f;
    g->fade_to = out ? 1.0f : 0.0f;
    g->fade_t = 0;
    g->fade_n = vblanks * g->hz / 60 + 1;
    g->fade_snap_back = out;
}

float game_fade_level(const Game *g, float alpha) {
    if (g->gameover) return 1.0f;
    if (g->fade_n <= 0) return g->fade_to;
    float t = (g->fade_t + alpha) / (float)g->fade_n;
    if (t > 1) t = 1;
    return g->fade_from + (g->fade_to - g->fade_from) * t;
}

static void new_game(Game *g) {
    memset(g_globals, 0, sizeof g_globals);
    memset(g_flags, 0, sizeof g_flags);
    g->view_mode = 0;
    g->fade_from = 1.0f; g->fade_to = 0.0f; g->fade_t = 0; g->fade_n = g->hz; g->fade_snap_back = 0;
    load_room_data(g, 1, 1, 0, g->renderer);
}

void game_tick(Game *g, uint32_t pad) {
    fade_tick(g);
    if (g->gameover) {                               /* game over card, then a new game (original: GODS movie, title) */
        if (--g->gameover == 0) new_game(g);
        return;
    }
    if (g->exit_requested && !g->script.suspended) {
        g->exit_requested = 0;
        g->gameover = g->hz * 4;
        log_msg("GAME OVER");
        return;
    }
    memcpy(g->prev_pos, g->player.pos, sizeof g->prev_pos);
    g->prev_ry = g->player.ry;
    memcpy(g->prev_body, g->anim.cur, sizeof g->prev_body);
    g->skip_pressed = (pad & (PAD_START | PAD_X)) != 0;
    message_tick(g);
    if (g->script.suspended) {                       /* a cutscene is playing: the script owns the frame */
        script_resume(&g->script, g);
        finish_script_frame(g);
        apply_pending(g);
        g->ticks++;
        g->prev_pad = pad;
        return;
    }
    /* the per-frame player code (0x4fb8) shows the body every frame, except in first person */
    g->player_hidden = g->view_mode == 2;
    if ((pad & PAD_L) && !(g->prev_pad & PAD_L)) {   /* L: cycle the view (0x4a9c) */
        g->view_mode = (g->view_mode + 1) % 3;
        if (g->view_mode == 0) game_camera_reset(g);
        script_event(&g->script, g, g->view_mode == 0 ? 27 : g->view_mode == 1 ? 28 : 29, 0, 0);
        log_msg("view mode %d", g->view_mode);
    }
    g->prev_pad = pad;
    int n = player_tick(&g->player, pad);
    anim_set(&g->anim, g->player.anim);
    anim_step(&g->anim, &g->human);
    script_event(&g->script, g, 2, 0, 0);            /* every frame */
    for (int i = 0; i < n && !g->script.suspended; i++) {
        Event *e = &g->player.events[i];
        if (e->kind != 1) log_msg("event ON (%d, %d, %d)", e->trigger, e->kind, e->item);
        script_event(&g->script, g, e->trigger, e->kind, e->item);
    }
    finish_script_frame(g);
    apply_pending(g);
    pick_camera(g);
    if (g->zone_event && !g->script.suspended) { int e = g->zone_event; g->zone_event = 0; script_event(&g->script, g, e, 0, 0); }
    g->ticks++;
}

/* A script camera looks along +z at rotation 0 and turns the opposite way to objects (its angles
 * build the world->camera transform): ry = 64 looks along -x (room 201's sliding-object scene). */
static void forward_from_rot(const float rot[3], float f[3]) {
    float z[3] = {0, 0, 1}, o[3] = {0, 0, 0}, r[3] = {-rot[0], -rot[1], -rot[2]};
    obj_transform(o, r, z, f);
}

void game_view(Game *g, float alpha, CameraView *cam) {
    memset(cam->up, 0, sizeof cam->up);
    cam->cull_shell = 0;
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
    int normal = g->cam_mode != CAM_ACTOR && !g->script.suspended;
    float k = 2.0f * 3.14159265f / 256.0f, fwd[3] = {-sinf(ry * k), 0, -cosf(ry * k)};
    if (normal && g->view_mode == 1) {               /* overhead: 1500 above the player, looking down (0x4d14) */
        for (int i = 0; i < 3; i++) cam->target[i] = pos[i];
        cam->eye[0] = pos[0]; cam->eye[1] = pos[1] - 1500; cam->eye[2] = pos[2];
        memcpy(cam->up, fwd, sizeof fwd);
        cam->cull_shell = 1;
    } else if (normal && g->view_mode == 2) {         /* first person: at the head marker (0x51f8) */
        float rot[3] = {0, ry, 0}, h0[3], h1[3];
        obj_transform(pos, rot, body[68], h0);
        obj_transform(pos, rot, body[69], h1);
        float d[3] = {h1[0] - h0[0], h1[1] - h0[1], h1[2] - h0[2]};
        float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l < 1e-3f) { memcpy(d, fwd, sizeof d); l = 1; }
        for (int i = 0; i < 3; i++) { cam->eye[i] = h0[i]; cam->target[i] = h0[i] + d[i] / l * 1000; }
    }
    trilist_reset(&g->player_tris);
    trilist_reset(&g->overlay_tris);
    if (g->area == 0 && !g->player_hidden) human_build(&g->human, (const float (*)[3])body, pos, ry, &g->player_tris);
    /* close-up head actors (handles 50-59 = HumanPol meshes 47-56), shown by cutscenes */
    for (int h = 50; h < 60; h++) {
        SmallActor *a = &g->actor[h];
        int mi = h - 3;
        if (!a->visible || a->area != g->area || mi >= g->human.nmesh) continue;
        const Mesh *m = &g->human.mesh[mi];
        static float wv[512][3];
        int n = m->nverts < 512 ? m->nverts : 512;
        for (int i = 0; i < n; i++) obj_transform(a->pos, a->rot, m->verts[i], wv[i]);
        trilist_add_mesh(&g->overlay_tris, (const float (*)[3])wv, m, g->human.table[mi], 0);
    }
}

void game_free(Game *g) {
    player_free(&g->player);
    room_free(&g->room);
    script_free(&g->script);
    human_free(&g->human);
    free(g->player_tris.v);
    free(g->overlay_tris.v);
    messages_free(&g->mes);
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

/* 0x4d8c only invalidates the current camera; the per-frame camera code (0x4b8c) re-aims it on the
 * next normal frame, i.e. after the script run that asked for it has finished */
void game_camera_reset(Game *g) { g->cam_reset_pending = 1; }

static void finish_script_frame(Game *g) {
    if (g->script.suspended) return;
    if (g->cam_reset_pending) { g->cam_reset_pending = 0; g->cam_mode = CAM_ZONE; g->camera = -1; }
}

void game_message(Game *g, int id, int time, int reset) {
    if (reset) { memset(g->mq, 0, sizeof g->mq); g->msg_frames = 0; g->mq[0].id = id; g->mq[0].time = time; }
    else for (int i = 0; i < 32; i++) if (g->mq[i].time == 0) { g->mq[i].id = id; g->mq[i].time = time; break; }
    log_msg("  MESSAGE %d (time %d%s)", id, time, reset ? ", new box" : "");
}
