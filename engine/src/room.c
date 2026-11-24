#include "room.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int mesh_read(const uint8_t *d, size_t n, size_t off, Mesh *m) {
    memset(m, 0, sizeof *m);
    if (off + 32 > n) return -1;
    int nv = be32(d + off), nf = be32(d + off + 4);
    if (nv < 0 || nv > 20000 || nf < 0 || nf > 40000) return -1;
    size_t p = off + 32;
    if (p + 12 * (size_t)nv > n) return -1;
    m->verts = malloc(sizeof *m->verts * (nv ? nv : 1));
    m->faces = malloc(sizeof *m->faces * (nf ? nf : 1));
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++) m->verts[i][k] = (float)be32(d + p + 12 * i + 4 * k);
    m->nverts = nv;
    p += 12 * (size_t)nv;
    for (int i = 0; i < nf; i++) {
        if (p + 8 > n) break;
        int fn = be32(d + p), tex = be32(d + p + 4);
        if (fn < 1 || fn > 8 || p + 8 + 4 * (size_t)fn > n) break;   /* a few 2-vertex faces exist */
        Face f = {fn, tex, {0}};
        int ok = 1;
        for (int k = 0; k < fn; k++) {
            f.v[k] = be32(d + p + 8 + 4 * k);
            if (f.v[k] < 0 || f.v[k] >= nv) ok = 0;
        }
        if (ok) m->faces[m->nfaces++] = f;
        p += 8 + 4 * (size_t)fn;
    }
    return 0;
}

void mesh_free(Mesh *m) { free(m->verts); free(m->faces); memset(m, 0, sizeof *m); }

/* rotation order from the game's object builder: x, then y, then z, then translate */
void obj_transform(const float pos[3], const float rot[3], const float in[3], float out[3]) {
    float x = in[0], y = in[1], z = in[2];
    const float k = 2.0f * 3.14159265358979f / 256.0f;
    if (rot[0]) { float c = cosf(rot[0] * k), s = sinf(rot[0] * k), ny = y * c - z * s; z = y * s + z * c; y = ny; }
    if (rot[1]) { float c = cosf(rot[1] * k), s = sinf(rot[1] * k), nx = x * c + z * s; z = -x * s + z * c; x = nx; }
    if (rot[2]) { float c = cosf(rot[2] * k), s = sinf(rot[2] * k), nx = x * c - y * s; y = x * s + y * c; x = nx; }
    out[0] = x + pos[0]; out[1] = y + pos[1]; out[2] = z + pos[2];
}

int room_load(Room *r, const char *path) {
    memset(r, 0, sizeof *r);
    r->file = blob_load(path);
    if (!r->file.data) { log_msg("room: cannot read %s", path); return -1; }
    const uint8_t *d = r->file.data;
    size_t n = r->file.size;
    r->objects = calloc(256, sizeof *r->objects);
    for (int k = 0; 0x18 + 0x80 * (size_t)(k + 1) <= n && k < 256; k++) {
        const uint8_t *w = d + 0x18 + 0x80 * (size_t)k;
        if (be32(w) == -1) break;
        int moff = be32(w + 4), msize = be32(w + 8);
        if (!(moff > 0 && msize >= 32 && (size_t)moff + (size_t)msize <= n)) continue;
        RoomObject *o = &r->objects[r->nobjects];
        o->id = be32(w);
        if (mesh_read(d, n, (size_t)moff, &o->mesh)) continue;
        for (int i = 0; i < 3; i++) { o->pos[i] = (float)be32(w + 0x0C + 4 * i); o->rot[i] = (float)be32(w + 0x18 + 4 * i); }
        for (int i = 0; i < 16; i++) o->table[i] = be32(w + 0x24 + 4 * i);
        o->flags64 = be32(w + 0x64); o->draw68 = be32(w + 0x68); o->nocull6c = be32(w + 0x6C);
        o->collidable = be32(w + 0x70); o->trigger = be32(w + 0x74);
        o->area = be32(w + 0x78); o->visible = be32(w + 0x7C) != 0;
        r->nobjects++;
    }
    uint32_t to = (uint32_t)be32(d);
    if (to + 4 <= n) {
        int nt = be32(d + to);
        if (nt > 0 && nt < 4096) {
            r->tex_off = calloc((size_t)nt, 4); r->tex_size = calloc((size_t)nt, 4);
            for (int i = 0; i < nt && to + 12 + 8 * (size_t)i <= n; i++) {
                r->tex_off[i] = to + (uint32_t)be32(d + to + 4 + 8 * i);
                r->tex_size[i] = (uint32_t)be32(d + to + 8 + 8 * i);
                if (r->tex_off[i] + r->tex_size[i] > n) r->tex_size[i] = 0;
                r->ntex = i + 1;
            }
        }
    }
    uint32_t C = (uint32_t)be32(d + 8);
    if (C + 0x640 * 2 <= n)
        for (int i = 0; i < 400; i++) { r->zones[i] = be32(d + C + 4 * i); r->triggers[i] = be32(d + C + 0x640 + 4 * i); }
    uint32_t E = (uint32_t)be32(d + 0x10);
    for (int i = 0; i < 64 && E + 32 * (size_t)(i + 1) <= n; i++) {
        const uint8_t *w = d + E + 32 * (size_t)i;
        if (be32(w) != i) break;
        for (int k = 0; k < 3; k++) { r->cams[i].eye[k] = (float)be32(w + 4 + 4 * k); r->cams[i].target[k] = (float)be32(w + 16 + 4 * k); }
        r->ncams = i + 1;
    }
    return 0;
}

void room_free(Room *r) {
    for (int i = 0; i < r->nobjects; i++) mesh_free(&r->objects[i].mesh);
    free(r->objects); free(r->tex_off); free(r->tex_size);
    blob_free(&r->file);
    memset(r, 0, sizeof *r);
}

int room_zone_at(const Room *r, float x, float z) {
    int col = (int)floorf((x + 2000.0f) / 200.0f), row = (int)floorf((2000.0f - z) / 200.0f);
    if (col < 0 || col >= 20 || row < 0 || row >= 20) return -1;
    return r->zones[row * 20 + col];
}
