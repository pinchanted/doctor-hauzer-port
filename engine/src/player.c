#include "player.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WALK 14
#define RUN 28
#define JUMP_STEP 28
#define PROBE 80
#define TURN 8

static const float PLAYER_BOX[6] = {-60, -270, -60, 60, 0, 60};
static const float FLOOR_BOX[6] = {-30, -270, -30, 30, 0, 30};

static Box actor_box(const float pos[3], const float rel[6]) {
    Box b; memset(&b, 0, sizeof b);
    for (int i = 0; i < 3; i++) { b.lo[i] = pos[i] + rel[i]; b.hi[i] = pos[i] + rel[3 + i]; }
    return b;
}

static int overlaps(const Box *a, const Box *b) {
    for (int i = 0; i < 3; i++) if (!(a->lo[i] < b->hi[i] && b->lo[i] < a->hi[i])) return 0;
    return 1;
}

/* world-space copy of a volume; NULL result flag when its object is not in the current area */
static int world(const Player *p, const Box *v, Box *out) {
    *out = *v;
    if (!v->owner) return 1;
    if (v->owner->area != p->area) return 0;
    for (int k = 0; k < 3; k++) { out->lo[k] += v->owner->pos[k]; out->hi[k] += v->owner->pos[k]; }
    return 1;
}

static int vol_hits(const Player *p, const Box *v, const Box *b) {
    Box w;
    return world(p, v, &w) && overlaps(&w, b);
}

void player_rebuild_volumes(Player *p) {
    Room *r = p->room;
    free(p->vols);
    p->vols = calloc(400 + (size_t)r->nobjects, sizeof *p->vols);
    p->nvols = 0;
    for (int row = 0; row < 20; row++) {
        float z1 = 2000.0f - row * 200.0f;
        for (int col = 0; col < 20; col++) {
            int k = row * 20 + col, zone = r->zones[k], trig = r->triggers[k];
            if (zone != 99 && trig == 0) continue;
            float x0 = -2000.0f + col * 200.0f;
            Box b = {{x0, -200, z1 - 200}, {x0 + 200, 200, z1}, zone == 99 ? 99 : 0, trig, NULL};
            p->vols[p->nvols++] = b;
        }
    }
    for (int i = 1; i < r->nobjects; i++) {
        RoomObject *o = &r->objects[i];
        if (!o->mesh.nverts || !(o->collidable || o->trigger)) continue;
        Box b; memset(&b, 0, sizeof b);
        for (int k = 0; k < 3; k++) { b.lo[k] = 1e9f; b.hi[k] = -1e9f; }
        for (int v = 0; v < o->mesh.nverts; v++)       /* local vertices: rotation ignored, as in 0x10d28 */
            for (int k = 0; k < 3; k++) {
                if (o->mesh.verts[v][k] < b.lo[k]) b.lo[k] = o->mesh.verts[v][k];
                if (o->mesh.verts[v][k] > b.hi[k]) b.hi[k] = o->mesh.verts[v][k];
            }
        b.solid = o->collidable; b.trigger = o->trigger; b.owner = o;
        p->vols[p->nvols++] = b;
    }
}

void player_init(Player *p, Room *r, const float pos[3], float ry, int area) {
    memset(p, 0, sizeof *p);
    p->room = r; p->area = area; p->ry = ry; p->anim = 2;
    memcpy(p->pos, pos, sizeof p->pos);
    player_rebuild_volumes(p);
}

void player_free(Player *p) { free(p->vols); p->vols = NULL; }

static void vec(const Player *p, float d, float *vx, float *vz) {
    float a = p->ry * 2.0f * 3.14159265358979f / 256.0f;
    /* the game rotates (0, 0, d) by the actor's angles (same convention as the model): */
    *vx = d * sinf(a);       /* forward moves pass d < 0, i.e. along the model's -z */
    *vz = d * cosf(a);
}

static void move(Player *p, float d, int ux, int uz) {
    float vx, vz; vec(p, d, &vx, &vz);
    if (ux) p->pos[0] += floorf(vx);
    if (uz) p->pos[2] += floorf(vz);
}

static Box *hit(Player *p) {
    Box b = actor_box(p->pos, PLAYER_BOX);
    for (int i = 0; i < p->nvols; i++) if (p->vols[i].solid && vol_hits(p, &p->vols[i], &b)) return &p->vols[i];
    return NULL;
}

static int probe(Player *p, float dist) {
    float save[3]; memcpy(save, p->pos, sizeof save);
    move(p, -dist, 1, 1);
    Box b = actor_box(p->pos, PLAYER_BOX);
    int best = 0;
    for (int i = 0; i < p->nvols; i++)
        if (p->vols[i].trigger && vol_hits(p, &p->vols[i], &b) && (!best || p->vols[i].trigger < best)) best = p->vols[i].trigger;
    memcpy(p->pos, save, sizeof save);
    return best;
}

static int floor_trigger(Player *p) {
    Box b = actor_box(p->pos, FLOOR_BOX);
    for (int i = 0; i < p->nvols; i++) if (p->vols[i].trigger && vol_hits(p, &p->vols[i], &b)) return p->vols[i].trigger;
    return 0;
}

static void event(Player *p, int kind, int trig, int item) {
    if (p->nevents < 8) { Event e = {kind, trig, item}; p->events[p->nevents++] = e; }
}

/* walk/run: full move only to detect a pushable, then x alone and z alone, each undone on a hit */
static Box *slide(Player *p, float d) {
    float save[3]; memcpy(save, p->pos, sizeof save);
    move(p, d, 1, 1);
    Box *h = hit(p);
    memcpy(p->pos, save, sizeof save);
    if (h && h->solid == 2) return h;
    for (int ax = 0; ax < 2; ax++) {
        float s[3]; memcpy(s, p->pos, sizeof s);
        move(p, d, ax == 0, ax == 1);
        if (hit(p)) memcpy(p->pos, s, sizeof s);
    }
    return NULL;
}

static void push(Player *p, Box *box, float step) {
    if (!box->owner) return;
    float vx, vz; vec(p, step, &vx, &vz);
    float dx = floorf(vx), dz = floorf(vz);
    float save[3]; memcpy(save, p->pos, sizeof save);
    box->owner->pos[0] += dx; box->owner->pos[2] += dz;
    p->pos[0] += dx; p->pos[2] += dz;
    Box pb = actor_box(p->pos, PLAYER_BOX), bw;
    world(p, box, &bw);
    for (int i = 0; i < p->nvols; i++) {
        Box *v = &p->vols[i];
        if (v == box || !v->solid) continue;
        if (vol_hits(p, v, &bw) || vol_hits(p, v, &pb)) {
            box->owner->pos[0] -= dx; box->owner->pos[2] -= dz;
            memcpy(p->pos, save, sizeof save);
            return;
        }
    }
}

static void state(Player *p, int s, int item) {
    float save[3];
    switch (s) {
    case 0: p->anim = 2; break;
    case 1: { p->anim = 3; Box *h = slide(p, -WALK); if (h) { p->anim = 7; push(p, h, -12); } break; }
    case 2: p->anim = 4; memcpy(save, p->pos, sizeof save); move(p, WALK, 1, 1); if (hit(p)) memcpy(p->pos, save, sizeof save); break;
    case 3: p->anim = 5; p->busy = 24; break;
    case 4: p->ry = fmodf(p->ry - TURN + 256.0f, 256.0f); break;
    case 5: p->ry = fmodf(p->ry + TURN, 256.0f); break;
    case 6: { p->anim = 9; int t = probe(p, PROBE); if (t) event(p, 2, t, 0); break; }
    case 7: { p->anim = 6; Box *h = slide(p, -RUN); if (h) { p->anim = 7; push(p, h, -18); } break; }
    case 8: {
        p->anim = 8;
        memcpy(save, p->pos, sizeof save);
        move(p, -36, 1, 1);
        Box *h = hit(p);
        memcpy(p->pos, save, sizeof save);
        if (h && h->solid == 2) push(p, h, WALK);
        else { move(p, WALK, 1, 1); if (hit(p)) memcpy(p->pos, save, sizeof save); }
        break;
    }
    case 9: case 10: case 11: case 12: p->anim = s + 1; break;
    case 13: { int t = probe(p, PROBE); if (t) event(p, 3, t, item); break; }
    case 14: p->anim = 15; break;
    case 15: p->anim = 16; break;
    }
}

int player_tick(Player *p, uint32_t pad) {
    p->nevents = 0;
    if (p->busy) {                       /* jump: 8 standing, 12 moving 28/tick, 4 landing */
        int n = 24 - p->busy;
        if (n >= 8 && n < 20) {
            float save[3]; memcpy(save, p->pos, sizeof save);
            move(p, -JUMP_STEP, 1, 1);
            if (hit(p)) memcpy(p->pos, save, sizeof save);
        }
        p->busy--;
    } else {
        int acted = 0;
        if ((pad & PAD_A) && !(pad & (PAD_UP | PAD_DOWN))) { state(p, 6, 0); acted = 1; }
        if (pad & PAD_B) {
            if (pad & PAD_UP) { state(p, 7, 0); acted = 1; }
            if (pad & PAD_DOWN) { state(p, 8, 0); acted = 1; }
            if (pad & PAD_RIGHT) { state(p, 4, 0); acted = 1; }
            if (pad & PAD_LEFT) { state(p, 5, 0); acted = 1; }
        }
        if (pad & PAD_C) { state(p, 3, 0); acted = 1; }
        else if (!acted && (pad & PAD_R)) {
            if (pad & PAD_UP) { state(p, 10, 0); acted = 1; }
            if (pad & PAD_DOWN) { state(p, 9, 0); acted = 1; }
            if (pad & PAD_RIGHT) { state(p, 14, 0); acted = 1; }
            if (pad & PAD_LEFT) { state(p, 15, 0); acted = 1; }
        }
        if (!acted) {
            if (pad & PAD_RIGHT) { p->anim = 1; state(p, 4, 0); }
            else if (pad & PAD_LEFT) { p->anim = 1; state(p, 5, 0); }
            else if (pad & PAD_UP) state(p, 1, 0);
            else if (pad & PAD_DOWN) state(p, 2, 0);
            else state(p, 0, 0);
        }
    }
    int t = floor_trigger(p);
    if (t) event(p, 1, t, 0);
    return p->nevents;
}
