#include "human.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* texture table per mesh, from launchme's loader at 0x3480 (16 (type, value) pairs each) */
static uint32_t table_for_mesh(int i) {
    if (i < 47) return 0x322fc;
    if (i == 47 || i == 48 || (i >= 51 && i <= 56)) return 0x329fc;
    switch (i) {
    case 49: return 0x32a7c; case 50: return 0x32afc; case 57: return 0x326fc;
    case 58: case 59: return 0x32b7c; case 60: return 0x3217c; case 61: return 0x3237c;
    case 62: return 0x323fc; case 63: return 0x3247c; case 64: return 0x327fc;
    case 65: return 0x3277c; case 66: return 0x3287c; case 67: return 0x328fc; case 68: return 0x3297c;
    }
    return 0;
}

/* AIF image: file offset = address + 0x80? launchme is loaded at 0 with its header, so the
 * address equals the file offset (the Python tools read it the same way). */
int human_load(HumanModel *h, const char *orgdata, const char *launchme) {
    memset(h, 0, sizeof *h);
    char p[1024];
    path_join(p, sizeof p, orgdata, "human/HumanPol.bin"); h->pol = blob_load(p);
    path_join(p, sizeof p, orgdata, "human/HumanTex.bin"); h->tex = blob_load(p);
    path_join(p, sizeof p, orgdata, "item/ItemTex.bin");   h->itemtex = blob_load(p);
    Blob exe = blob_load(launchme);
    if (!h->pol.data || !h->tex.data || !h->itemtex.data || !exe.data) {
        log_msg("human: missing HumanPol/HumanTex/ItemTex/launchme");
        blob_free(&exe);
        return -1;
    }
    int n = be32(h->pol.data);
    if (n > HUMAN_MAX_MESH) n = HUMAN_MAX_MESH;
    for (int i = 0; i < n; i++) mesh_read(h->pol.data, h->pol.size, (size_t)be32(h->pol.data + 4 + 8 * i), &h->mesh[i]);
    h->nmesh = n;
    int k = be32(h->tex.data), ki = be32(h->itemtex.data);
    for (int i = 0; i < k && h->ntex < 512; i++, h->ntex++) {
        h->tex_ptr[h->ntex] = h->tex.data + be32(h->tex.data + 4 + 8 * i);
        h->tex_size[h->ntex] = (uint32_t)be32(h->tex.data + 8 + 8 * i);
    }
    h->n_human = h->ntex;
    for (int i = 0; i < ki && h->ntex < 512; i++, h->ntex++) {
        h->tex_ptr[h->ntex] = h->itemtex.data + be32(h->itemtex.data + 4 + 8 * i);
        h->tex_size[h->ntex] = (uint32_t)be32(h->itemtex.data + 8 + 8 * i);
    }
    /* items */
    path_join(p, sizeof p, orgdata, "item/ItemPol.bin"); h->itempol = blob_load(p);
    if (h->itempol.data) {
        /* the loader at 0x32e4 puts mesh k (texture table 0x32bfc + 0x80*k) in actor handle 220 + k, and
         * item id k's record names handle 220 + k: mesh 0 is the dummy item */
        int ni = be32(h->itempol.data);
        if (ni > ITEM_COUNT) ni = ITEM_COUNT;
        for (int i = 0; i < ni; i++) {
            mesh_read(h->itempol.data, h->itempol.size, (size_t)be32(h->itempol.data + 4 + 8 * i), &h->item_mesh[i]);
            for (int t = 0; t < 16; t++) {
                uint32_t a = 0x32bfc + 0x80u * (uint32_t)i;
                int typ = be32(exe.data + a + 8 * t), val = be32(exe.data + a + 4 + 8 * t);
                h->item_table[i][t] = typ == 3 ? h->n_human + val : typ == 2 ? val : -1;
            }
        }
        h->nitem = ni;
    }
    for (int id = 0; id < ITEM_COUNT; id++) {
        const uint8_t *e = exe.data + 0x35c8c + 56 * id;
        for (int k = 0; k < 3; k++) h->item_spin[id][k] = be32(e + 4 + 4 * k);
        memcpy(h->item_name_sjis[id], e + 16, 39);
        h->item_name_sjis[id][39] = 0;
        int k = 0;
        for (; k < 8; k++) {
            int v = be32(exe.data + 0x34894 + 32 * id + 4 * k);
            if (v < 0 || v >= 49) break;
            h->item_pages[id][k] = v;
        }
        if (k < 8) h->item_pages[id][k] = -1;
        if (id > 34) h->item_pages[id][0] = -1;   /* the table only covers ids 0..34 */
    }
    for (int i = 0; i < n; i++) {
        uint32_t a = table_for_mesh(i);
        for (int t = 0; t < 16; t++) {
            int typ = a ? be32(exe.data + a + 8 * t) : 0, val = a ? be32(exe.data + a + 4 + 8 * t) : 0;
            h->table[i][t] = typ == 2 ? val : typ == 3 ? h->n_human + val : -1;
        }
    }
    blob_free(&exe);
    return 0;
}

const char *const ITEM_PAGE_FILE[49] = {
    "sea01", "sea02", "sea03", "sea04", "sea05", "sea06", "sea07", "sea08", "sea09", "sea10",
    "sea11_01", "sea11_02", "sea11_03", "sea11_04", "sea12_01", "sea12_02", "sea12_03",
    "sea13_01", "sea13_02", "sea13_03", "sea13_04", "sea13_05",
    "sea14_01", "sea14_02", "sea14_03", "sea14_04", "sea14_05", "sea14_06",
    "sea15", "sea16", "sea17", "sea18", "sea19_01", "sea19_02", "sea20", "sea21", "sea22", "sea23", "sea24",
    "sea25", "sea26", "sea27", "sea28", "sea29", "sea30", "sea31", "sea32", "sea33", "sea34"};

void human_free(HumanModel *h) {
    for (int i = 0; i < h->nmesh; i++) mesh_free(&h->mesh[i]);
    for (int i = 0; i < h->nitem; i++) mesh_free(&h->item_mesh[i]);
    blob_free(&h->itempol);
    blob_free(&h->pol); blob_free(&h->tex); blob_free(&h->itemtex);
}

/* ---------------------------------------------------------------- animation */
const AnimDef ANIMS[17] = {
    {"idle_start", 4, 2, 5, {1, 1, 2, 1, 3}},
    {"turn", 3, 1, 2, {4, 5}},
    {"idle", 4, 2, 5, {1, 1, 2, 1, 3}},
    {"walk", 2, 1, 4, {7, 8, 9, 6}},
    {"walk_back", 2, 1, 4, {6, 9, 8, 7}},
    {"jump", 2, 3, 6, {4, 10, 11, 12, 44, 8}},
    {"run", 1, 1, 4, {13, 14, 15, 16}},
    {"push", 2, 2, 5, {21, 17, 18, 19, 20}},
    {"pull", 2, 1, 4, {22, 24, 23, 25}},
    {"examine", 3, 2, 3, {27, 27, 27}},
    {"hold_r_down", 3, 1, 2, {30, 30}},
    {"hold_r_up", 3, 1, 2, {31, 31}},
    {"reach", 2, 1, 3, {32, 33, 34}},
    {"collapse", 4, 1, 5, {35, 36, 37, 38, 39}},
    {"sidestep", 2, 1, 4, {40, 41, 42, 43}},
    {"hold_r_right", 3, 2, 3, {28, 28, 28}},
    {"hold_r_left", 3, 2, 3, {29, 29, 29}},
};

static void anim_load_keys(AnimState *a, int slot) {
    const AnimDef *d = &ANIMS[slot];
    a->slot = slot;
    a->keys[0] = -2;
    for (int i = 0; i < d->nkeys; i++) a->keys[1 + i] = d->keys[i];
    a->keys[1 + d->nkeys] = -1;
    a->nkeys = d->nkeys + 2;
    a->k = a->t = a->ticks = 0;
}

void anim_init(AnimState *a, const HumanModel *h, int slot) {
    memset(a, 0, sizeof *a);
    a->nverts = h->mesh[1].nverts < 128 ? h->mesh[1].nverts : 128;
    for (int i = 0; i < a->nverts; i++) for (int c = 0; c < 3; c++) a->cur[i][c] = a->snap[i][c] = h->mesh[1].verts[i][c];
    anim_load_keys(a, slot);
}

void anim_set(AnimState *a, int slot) {
    if (slot == a->slot || slot < 0 || slot >= 17) return;
    memcpy(a->snap, a->cur, sizeof a->snap);
    anim_load_keys(a, slot);
}

static const float (*key_verts(const AnimState *a, const HumanModel *h, int key))[3] {
    if (key == -2) return (const float (*)[3])a->snap;
    return (const float (*)[3])h->mesh[key].verts;
}

void anim_step(AnimState *a, const HumanModel *h) {
    const AnimDef *d = &ANIMS[a->slot];
    int span = 1 << d->shift;
    if (a->t > span - 1) { a->t = 0; a->k++; }
    if (a->keys[a->k] == -1) { a->k = d->loop; a->ticks = 0; }
    int nxt = a->keys[a->k + 1] != -1 ? a->k + 1 : d->loop;
    const float (*va)[3] = key_verts(a, h, a->keys[a->k]);
    const float (*vb)[3] = key_verts(a, h, a->keys[nxt]);
    float f = (float)a->t / (float)span;
    float tmp[128][3];
    for (int i = 0; i < a->nverts; i++) for (int c = 0; c < 3; c++) tmp[i][c] = va[i][c] + (vb[i][c] - va[i][c]) * f;
    memcpy(a->cur, tmp, sizeof tmp);
    a->t += a->k == 0 ? 2 : 1;
    a->ticks++;
}

/* ---------------------------------------------------------------- triangles */
void trilist_reset(TriList *t) { t->nv = 0; t->nb = 0; }

static Vtx *tl_push(TriList *t, int tex, int n) {
    if (t->nv + n > t->cap) {
        t->cap = (t->nv + n) * 2 + 1024;
        t->v = realloc(t->v, sizeof *t->v * (size_t)t->cap);
    }
    if (!t->nb || t->b[t->nb - 1].tex != tex) {
        if (t->nb < 256) { t->b[t->nb].tex = tex; t->b[t->nb].first = t->nv; t->b[t->nb].count = 0; t->nb++; }
    }
    Vtx *v = t->v + t->nv;
    t->nv += n;
    t->b[t->nb - 1].count += n;
    return v;
}

static void put(Vtx *v, const float p[3], float u, float w) { memcpy(v->p, p, sizeof v->p); v->uv[0] = u; v->uv[1] = w; }

/* quads: corners map to CEL corners (0,0) (1,0) (1,1) (0,1). Triangles: the 3DO draws them as a CEL
 * quad with the last corner collapsed onto v2, so the whole texture is squeezed in - tessellate the
 * bilinear patch P(u,v) = (1-v)((1-u)p0 + u p1) + v p2 into 4x4 cells. */
static void add_face(TriList *t, const float (*vs)[3], const Face *f, int tex) {
    if (f->n == 4) {
        const float *p0 = vs[f->v[0]], *p1 = vs[f->v[1]], *p2 = vs[f->v[2]], *p3 = vs[f->v[3]];
        Vtx *v = tl_push(t, tex, 6);
        put(v + 0, p0, 0, 0); put(v + 1, p1, 1, 0); put(v + 2, p2, 1, 1);
        put(v + 3, p0, 0, 0); put(v + 4, p2, 1, 1); put(v + 5, p3, 0, 1);
    } else if (f->n == 3) {
        const float *p0 = vs[f->v[0]], *p1 = vs[f->v[1]], *p2 = vs[f->v[2]];
        const int N = 4;
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
            float us[2] = {(float)i / N, (float)(i + 1) / N}, ws[2] = {(float)j / N, (float)(j + 1) / N};
            float P[4][3], UV[4][2];
            int cu[4] = {0, 1, 1, 0}, cw[4] = {0, 0, 1, 1};
            for (int c = 0; c < 4; c++) {
                float u = us[cu[c]], w = ws[cw[c]];
                for (int k = 0; k < 3; k++) P[c][k] = (1 - w) * ((1 - u) * p0[k] + u * p1[k]) + w * p2[k];
                UV[c][0] = u; UV[c][1] = w;
            }
            Vtx *v = tl_push(t, tex, 6);
            put(v + 0, P[0], UV[0][0], UV[0][1]); put(v + 1, P[1], UV[1][0], UV[1][1]); put(v + 2, P[2], UV[2][0], UV[2][1]);
            put(v + 3, P[0], UV[0][0], UV[0][1]); put(v + 4, P[2], UV[2][0], UV[2][1]); put(v + 5, P[3], UV[3][0], UV[3][1]);
        }
    }
}

static int is_marker(const Face *f) {
    for (int k = 0; k < f->n; k++) if (f->v[k] < 62 || f->v[k] > 70) return 0;
    return 1;
}

void trilist_add_mesh(TriList *t, const float (*verts)[3], const Mesh *m, const int *table, int skip_markers) {
    for (int tx = 0; tx < 16; tx++) {           /* grouped by texture to keep draw batches few */
        if (table[tx] < 0) continue;
        for (int i = 0; i < m->nfaces; i++) {
            const Face *f = &m->faces[i];
            if (f->tex != tx || (skip_markers && is_marker(f))) continue;
            add_face(t, verts, f, table[tx]);
        }
    }
}

/* rigid parts sit on marker triangles (attach point, forward, up); see tools/human.py _basis */
static void basis(const float *p0, const float *p1, const float *p2, float o[3], float R[3][3]) {
    float f[3], u[3], r[3];
    for (int k = 0; k < 3; k++) { o[k] = p0[k]; f[k] = p1[k] - p0[k]; u[k] = p2[k] - p0[k]; }
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (fl < 1e-6f) fl = 1;
    for (int k = 0; k < 3; k++) f[k] /= fl;
    float d = u[0] * f[0] + u[1] * f[1] + u[2] * f[2];
    for (int k = 0; k < 3; k++) u[k] -= f[k] * d;
    float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (ul < 1e-6f) ul = 1;
    for (int k = 0; k < 3; k++) u[k] /= ul;
    r[0] = u[1] * f[2] - u[2] * f[1]; r[1] = u[2] * f[0] - u[0] * f[2]; r[2] = u[0] * f[1] - u[1] * f[0];
    /* local +x -> r, +y -> -u, +z -> -f (columns) */
    for (int k = 0; k < 3; k++) { R[k][0] = r[k]; R[k][1] = -u[k]; R[k][2] = -f[k]; }
}

void human_build(const HumanModel *h, const float (*body)[3], const float pos[3], float ry, TriList *out) {
    static float world[128][3], part[256][3];
    float rot[3] = {0, ry, 0};
    const Mesh *bm = &h->mesh[1];
    int nb = bm->nverts < 128 ? bm->nverts : 128;
    for (int i = 0; i < nb; i++) obj_transform(pos, rot, body[i], world[i]);
    trilist_add_mesh(out, (const float (*)[3])world, bm, h->table[1], 1);
    static const int marker[3][3] = {{62, 63, 64}, {65, 66, 67}, {68, 69, 70}};
    static const int part_mesh[3] = {58, 59, 57};
    for (int pi = 0; pi < 3; pi++) {
        float o[3], R[3][3];
        basis(world[marker[pi][0]], world[marker[pi][1]], world[marker[pi][2]], o, R);
        const Mesh *pm = &h->mesh[part_mesh[pi]];
        int n = pm->nverts < 256 ? pm->nverts : 256;
        for (int i = 0; i < n; i++)
            for (int k = 0; k < 3; k++)
                part[i][k] = o[k] + R[k][0] * pm->verts[i][0] + R[k][1] * pm->verts[i][1] + R[k][2] * pm->verts[i][2];
        trilist_add_mesh(out, (const float (*)[3])part, pm, h->table[part_mesh[pi]], 0);
    }
}
