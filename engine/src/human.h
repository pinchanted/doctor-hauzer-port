/* Player model (OrgData/human/HumanPol.bin + HumanTex.bin + item/ItemTex.bin) and its animation.
 * Ports of tools/human.py and tools/anim.py. */
#pragma once
#include "room.h"
#include "util.h"

#define HUMAN_MAX_MESH 80
#define ITEM_COUNT 36                  /* item ids 1..35 (0 = dummy) */

typedef struct {
    Blob pol, tex, itemtex;
    int nmesh;
    Mesh mesh[HUMAN_MAX_MESH];
    int n_human, ntex;                 /* combined texture list: HumanTex then ItemTex */
    const uint8_t *tex_ptr[512];
    uint32_t tex_size[512];
    int table[HUMAN_MAX_MESH][16];     /* face tex -> combined texture index, -1 none */
    /* inventory items (OrgData/item/ItemPol.bin mesh = item id; tables at 0x32bfc + 0x80*id) and the
     * per-item record at 0x35c8c (56 bytes: handle 220+id, idle spin x/y/z per frame, Shift-JIS name) */
    Blob itempol;
    int nitem;
    Mesh item_mesh[ITEM_COUNT];
    int item_table[ITEM_COUNT][16];
    int item_spin[ITEM_COUNT][3];
    char item_name_sjis[ITEM_COUNT][40];
    int item_pages[ITEM_COUNT][8];     /* examine pages (index into ITEM_PAGE_FILE), -1 terminated (0x34894) */
} HumanModel;

extern const char *const ITEM_PAGE_FILE[49];   /* isearch/<name>.img, launchme 0x34100 */

int human_load(HumanModel *h, const char *orgdata, const char *launchme);
void human_free(HumanModel *h);

/* --- animation (launchme 0x3b960 table, stepper 0x4488) --- */
typedef struct { const char *name; int shift, loop, nkeys, keys[8]; } AnimDef;
extern const AnimDef ANIMS[17];

typedef struct {
    int slot, k, t, ticks;
    int keys[10], nkeys;               /* keys[0] = -2 (snapshot), then poses, then -1 */
    float snap[128][3];                /* body pose captured at the switch */
    float cur[128][3];                 /* body pose after the last step */
    int nverts;
} AnimState;

void anim_init(AnimState *a, const HumanModel *h, int slot);
void anim_set(AnimState *a, int slot);              /* SetPlayerAnim: cross-fades from cur */
void anim_step(AnimState *a, const HumanModel *h);  /* one game tick */

/* Assembled player (body + head + hands) as world-space triangles for the renderer. */
typedef struct { float p[3], uv[2]; } Vtx;
typedef struct { int tex; int first, count; } Batch;
typedef struct {
    Vtx *v; int nv, cap;
    Batch b[256]; int nb;
} TriList;

void trilist_reset(TriList *t);
void trilist_add_mesh(TriList *t, const float (*verts)[3], const Mesh *m, const int *table, int skip_markers);
void human_build(const HumanModel *h, const float (*body)[3], const float pos[3], float ry, TriList *out);
