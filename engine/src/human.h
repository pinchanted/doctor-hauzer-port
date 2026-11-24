/* Player model (OrgData/human/HumanPol.bin + HumanTex.bin + item/ItemTex.bin) and its animation.
 * Ports of tools/human.py and tools/anim.py. */
#pragma once
#include "room.h"
#include "util.h"

#define HUMAN_MAX_MESH 80

typedef struct {
    Blob pol, tex, itemtex;
    int nmesh;
    Mesh mesh[HUMAN_MAX_MESH];
    int n_human, ntex;                 /* combined texture list: HumanTex then ItemTex */
    const uint8_t *tex_ptr[512];
    uint32_t tex_size[512];
    int table[HUMAN_MAX_MESH][16];     /* face tex -> combined texture index, -1 none */
} HumanModel;

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
