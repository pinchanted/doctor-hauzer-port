#include "script.h"
#include "game.h"
#include <stdlib.h>
#include <string.h>

int g_globals[256];
int g_flags[32];

/* operand-token counts per opcode, from each handler's script-counter increment (tools/macro_dis.py
 * ARITY); -1 = variable length (skip to the next opcode token) */
static int arity(int op) {
    static const struct { int op, n; } T[] = {
        {0xfc01, 0}, {0xfc02, 0}, {0xfc03, 1}, {0xfc04, 1}, {0xfc05, 4}, {0xfc06, 2}, {0xfc08, 2}, {0xfc09, 1},
        {0xfc0a, 0}, {0xfc0d, 4}, {0xfc0e, 0}, {0xfc0f, 0}, {0xfc10, 3}, {0xfc11, 4}, {0xfc60, 0}, {0xfc61, 0},
        {0xfc62, 0}, {0xfc63, 0}, {0xfc64, 1}, {0xfc65, 0}, {0xfc66, 2}, {0xfc67, 2}, {0xfc68, 2}, {0xfc69, 2},
        {0xfc6a, 1}, {0xfc6b, 0}, {0xfc6c, 0}, {0xfc6d, 2}, {0xfc70, 1}, {0xfc71, -1}, {0xfc72, 3}, {0xfc73, 1},
        {0xfc74, 0}, {0xfc75, 1}, {0xfc76, 1}, {0xfc77, 2}, {0xfc78, 1}, {0xfc79, 1}, {0xfc7a, 0}, {0xfc7b, 0},
        {0xfc7c, 0}, {0xfc7d, 1}, {0xfc7e, 3}, {0xfc7f, 1}, {0xfc80, 0}, {0xfc81, 0}, {0xfc82, 0}, {0xfc83, 2},
        {0xfc84, 2}, {0xfc85, 0}, {0xfc86, 0}, {0xfc87, 0}, {0xfc88, 0}, {0xfc89, 12}, {0xfc8a, 0}, {0xfc8b, 0},
        {0xfc8c, 1}, {0xfc8d, 2}, {0xfc8e, 2}, {0xfc8f, 2}, {0xfc90, 3}, {0xfc91, 0}, {0xfc92, 0}, {0xfc93, 2},
        {0xfc94, 1}, {0xfc95, 1}, {0xfc96, 3}, {0xfc97, 3}, {0xfc98, 5}, {0xfc99, 1}, {0xfc9a, 11}, {0xfc9b, 0},
        {0xfc9c, 1}, {0xfc9d, 0}, {0xfc9e, 0}, {0xfc9f, 1}, {0xfca0, 4}, {0xfca1, 4}, {0xfca2, 1}, {0xfca3, 4},
        {0xfca4, 4}, {0xfca5, 1}, {0xfca6, 1}, {0xfca7, 0}, {0xfca8, 0}, {0xfca9, 1}, {0xfcaa, 3}, {0xfcab, 0},
        {0xfcac, 0}, {0xfcad, 0}, {0xfcae, 2}, {0xfcaf, 0}, {0xfcb0, 1}, {0xfcb1, 1}, {0xfcb2, 0}, {0xfcb3, 1},
        {0xfcb4, 1}, {0xfcb5, 2}, {0xfcb6, 2}, {0xfcb7, 1}, {0xfcb8, 1}, {0xfcb9, 1}, {0xfcba, 1}, {0xfcbb, 1},
        {0xfcbc, 1}, {0xfcbd, 1}, {0xfcbe, 1}, {0xfcbf, 1}, {0xfcc0, 2}, {0xfcc1, 1}, {0xfcc2, 1}, {0xfcc3, 2},
        {0xfcc4, 2}, {0xfcc5, 0}, {0xfcc6, 1}, {0xfcc7, 3}, {0xfcc8, 1}, {0xfcc9, 2}, {0xfcca, 1}, {0xfccb, 0},
        {0xfccc, 0}, {0xfccd, 0}, {0xfcce, 1}, {0xfccf, 2}, {0xfce0, 1}, {0xfce1, 1}, {0xfce2, 1}, {0xfce3, 1},
        {0xfce4, 1}, {0xfce5, 1}, {0xfce6, 0}, {0xfce7, 0},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (T[i].op == op) return T[i].n;
    return 0;
}

int script_load(Script *s, const char *path) {
    memset(s, 0, sizeof *s);
    s->file = blob_load(path);
    if (!s->file.data) { log_msg("script: cannot read %s", path); return -1; }
    s->nw = (int)(s->file.size / 4);
    s->w = malloc(sizeof *s->w * (size_t)(s->nw + 4));
    for (int i = 0; i < s->nw; i++) s->w[i] = be32(s->file.data + 4 * i);
    s->w[s->nw] = s->w[s->nw + 1] = 0;
    return 0;
}

void script_free(Script *s) { free(s->w); blob_free(&s->file); memset(s, 0, sizeof *s); }

/* operand k (0-based) of the opcode at pc */
static int tag_at(Script *s, int k) { int i = s->pc + 2 + 2 * k; return i < s->nw ? s->w[i] : 0; }
static int raw_at(Script *s, int k) { int i = s->pc + 3 + 2 * k; return i < s->nw ? s->w[i] : 0; }
static int eval_tok(Script *s, int tag, int v) {
    if (tag == 3) return (v >= 0 && v < 256) ? s->locals[v] : 0;
    if (tag == 6) return (v >= 0 && v < 256) ? g_globals[v] : 0;
    return v;
}
static int op(Script *s, int k) { return eval_tok(s, tag_at(s, k), raw_at(s, k)); }

static int *var_ref(Script *s, int tag, int v) {
    if (tag == 3 && v >= 0 && v < 256) return &s->locals[v];
    if (tag == 6 && v >= 0 && v < 256) return &g_globals[v];
    return NULL;
}

static int compare(int a, int code, int b) {
    switch (code - 0xfcd0) {            /* from the jump tables at 0x7930 / 0x7cb4 */
    case 0: return a == b;
    case 1: return a > b;
    case 2: return a < b;
    case 3: return a != b;
    case 4: return a <= b;
    case 5: return a >= b;
    }
    return 0;
}

static void game_op(Script *s, Game *g, int opc);

static void run(Script *s, Game *g) {
    s->suspended = 0;
    for (int budget = 0; budget < 200000 && !s->yield && !s->suspended; budget++) {
        if (s->pc < 0 || s->pc + 1 >= s->nw) { s->yield = 1; break; }
        int v = s->w[s->pc + 1];
        if (!s->started) {                   /* only BEGIN and DEF run before BEGIN */
            if (v == 0xfc01) { s->started = 1; s->pc += 2; }
            else if (v == 0xfc06) { int i = raw_at(s, 0); if (i >= 0 && i < 256) s->locals[i] = raw_at(s, 1); s->pc += 6; }
            else s->pc += 2;
            continue;
        }
        if (v < 0xfc02 || v > 0xfce8) { s->pc += 2; continue; }
        switch (v) {
        case 0xfc02: s->yield = 1; s->pc += 2; break;                                   /* WAIT */
        case 0xfc03: s->pc = tag_at(s, 0) == 2 ? raw_at(s, 0) * 2 : s->pc + 4; break;  /* GOTO */
        case 0xfc04: log_msg("script PRINT %d", op(s, 0)); s->pc += 4; break;
        case 0xfc05: {                                                                   /* IF a cmp b L */
            int a = op(s, 0), c = raw_at(s, 1), b = op(s, 2), l = raw_at(s, 3);
            s->pc += 10;
            if (compare(a, c, b)) s->pc = l * 2;
            break;
        }
        case 0xfc06: s->pc += 6; break;
        case 0xfc08: {                                                                   /* ON v L */
            int a = op(s, 0), l = raw_at(s, 1);
            s->pc += 6;
            if (s->event[0] == a) s->pc = l * 2;
            break;
        }
        case 0xfc09:                                                                     /* CALL */
            if (s->ncall < 32 && tag_at(s, 0) == 2) { s->call[s->ncall++] = s->pc + 4; s->pc = raw_at(s, 0) * 2; }
            else s->pc += 4;
            break;
        case 0xfc0a: if (s->ncall > 0) s->pc = s->call[--s->ncall]; else s->pc += 2; break;   /* RETURN */
        case 0xfc0d: {                                                                   /* FOR v, from, to, step */
            int t = tag_at(s, 0), idx = raw_at(s, 0), from = op(s, 1), to = op(s, 2), step = op(s, 3);
            if (s->nfor < 16) { s->for_pc[s->nfor] = s->pc; s->for_step[s->nfor] = step; s->for_to[s->nfor] = to; s->nfor++; }
            int *r = var_ref(s, t, idx); if (r) *r = from;
            s->pc += 10;
            break;
        }
        case 0xfc0e: {                                                                   /* NEXT */
            s->pc += 2;
            if (!s->nfor) break;
            int fp = s->for_pc[s->nfor - 1], step = s->for_step[s->nfor - 1], to = s->for_to[s->nfor - 1];
            int *r = var_ref(s, s->w[fp + 2], s->w[fp + 3]);
            int val = r ? *r : 0;
            if (val + step == to || step == 0) { s->nfor--; break; }
            if (r) *r = val + step;
            s->pc = fp + 10;
            break;
        }
        case 0xfc0f: s->mark = s->pc; s->pc += 2; break;                                /* MARK */
        case 0xfc10: {                                                                   /* WHILE a cmp b */
            int a = op(s, 0), c = raw_at(s, 1), b = op(s, 2);
            s->pc += 8;
            if (compare(a, c, b)) s->pc = s->mark;
            break;
        }
        case 0xfc11: {                                                                   /* ON (a, b, c) L */
            int a = op(s, 0), b = op(s, 1), c = op(s, 2), l = raw_at(s, 3);
            s->pc += 10;
            if (s->event[0] == a && s->event[1] == b && s->event[2] == c) s->pc = l * 2;
            break;
        }
        case 0xfcc1: case 0xfcc2: { int n = op(s, 0); if (n >= 0 && n < 32) g_flags[n] = v == 0xfcc1; s->pc += 4; break; }
        case 0xfcc3: { int n = op(s, 0), l = raw_at(s, 1); s->pc += 6; if (n >= 0 && n < 32 && g_flags[n] == 1) s->pc = l * 2; break; }
        case 0xfce0: case 0xfce1: case 0xfce2: case 0xfce3: case 0xfce4: case 0xfce5: {  /* var op= x */
            int *r = var_ref(s, s->w[s->pc - 2], s->w[s->pc - 1]);
            int x = op(s, 0);
            if (r) switch (v) {
                case 0xfce0: *r = x; break;
                case 0xfce1: *r += x; break;
                case 0xfce2: *r -= x; break;
                case 0xfce3: *r *= x; break;
                case 0xfce4: if (x) *r /= x; break;
                case 0xfce5: if (x) *r %= x; break;
            }
            s->pc += 4;
            break;
        }
        case 0xfce6: case 0xfce7: { int *r = var_ref(s, s->w[s->pc - 2], s->w[s->pc - 1]); if (r) *r += v == 0xfce6 ? 1 : -1; s->pc += 2; break; }
        default: game_op(s, g, v); break;
        }
        s->steps++;
    }
}

void script_event(Script *s, Game *g, int a, int b, int c) {
    if (!s->w) return;
    s->pc = 0; s->started = 0; s->yield = 0; s->suspended = 0; s->wait = 0;
    s->ncall = 0; s->nfor = 0; s->mark = 0;
    s->event[0] = a; s->event[1] = b; s->event[2] = c;
    run(s, g);
}

void script_resume(Script *s, Game *g) {
    if (!s->w || !s->suspended) return;
    if (s->wait > 0 && --s->wait > 0) return;
    run(s, g);
}

/* ---------------------------------------------------------------- game commands */
static void suspend(Script *s, int frames) { s->suspended = 1; s->wait = frames > 0 ? frames : 1; }

static void game_op(Script *s, Game *g, int opc) {
    int n = arity(opc);
    int a0 = n > 0 ? op(s, 0) : 0, a1 = n > 1 ? op(s, 1) : 0, a2 = n > 2 ? op(s, 2) : 0, a3 = n > 3 ? op(s, 3) : 0;
    int next = s->pc + 2 + 2 * (n > 0 ? n : 0);
    int *lhs = var_ref(s, s->w[s->pc - 2], s->w[s->pc - 1]);
    float p[3];
    switch (opc) {
    case 0xfca3: game_actor_set(g, a0, 0, (float)a1, (float)a2, (float)a3); break;   /* SET_POS */
    case 0xfca4: game_actor_set(g, a0, 1, (float)a1, (float)a2, (float)a3); break;   /* SET_ROT */
    case 0xfca0: game_actor_set(g, a0, 2, (float)a1, (float)a2, (float)a3); break;   /* ADD_POS */
    case 0xfca1: game_actor_set(g, a0, 3, (float)a1, (float)a2, (float)a3); break;   /* ADD_ROT */
    case 0xfcbd: game_actor_show(g, a0, 1); break;                                     /* SHOW */
    case 0xfcbe: game_actor_show(g, a0, 0); break;                                     /* HIDE */
    case 0xfcc9: game_actor_area(g, a0, a1); break;                                    /* ACTOR_AREA */
    case 0xfca2: game_set_area(g, a0); break;                                          /* AREA */
    case 0xfcca: break;                                                                /* PLAYER_AREA (player drawn in area 0 only for now) */
    case 0xfcae: game_actor_move_fwd(g, a0, (float)a1); break;                         /* MOVE_FWD */
    case 0xfcc7: game_actor_set(g, 1, 0, (float)a0, (float)a1, (float)a2); break;     /* PLAYER_POS */
    case 0xfcb7: case 0xfcb8: case 0xfcb9:                                             /* GET_X / Y / Z */
        game_actor_get(g, a0, p);
        if (lhs) *lhs = (int)p[opc - 0xfcb7];
        break;
    case 0xfc97: game_actor_look_at(g, a0, a1, NULL); break;                           /* LOOK_AT */
    case 0xfc98: { float t[3] = {(float)a1, (float)a2, (float)a3}; game_actor_look_at(g, a0, -1, t); break; }
    case 0xfc95: game_actor_no_collide(g, a0); break;                                  /* NO_COLLIDE */
    case 0xfc7e: { int l = raw_at(s, 2); s->pc = next; if (game_actor_touch(g, a0, a1)) s->pc = l * 2; return; }   /* IF_TOUCH */
    case 0xfcb1: anim_set(&g->anim, a0); break;                                        /* PLAYER_ANIM */
    case 0xfc9b: break;                                                                /* PLAYER_ANIM_CUT */
    case 0xfcb2: anim_step(&g->anim, &g->human); break;                                /* PLAYER_ANIM_STEP */
    case 0xfcaf: break;                                                                /* ATTACH_PARTS (every frame anyway) */
    case 0xfc85: g->player_hidden = 0; break;                                          /* SHOW_PLAYER */
    case 0xfc86: g->player_hidden = 1; break;                                          /* HIDE_PLAYER */
    case 0xfc78: game_camera_fixed(g, a0); break;                                      /* CAMERA */
    case 0xfcab: game_camera_reset(g); break;                                          /* CAMERA_RESET */
    case 0xfca7: case 0xfca8: case 0xfc9e: s->pc = next; suspend(s, 1); return;       /* DRAW */
    case 0xfcc8: s->pc = next; suspend(s, (a0 * g->hz + 59) / 60); return;             /* SLEEP vblanks */
    case 0xfcb4: case 0xfcb3: game_fade(g, opc == 0xfcb4, a0); s->pc = next; suspend(s, (a0 * g->hz + 59) / 60); return;   /* FADE_OUT / FADE_IN */
    case 0xfcaa: game_message(g, a0, a1, a2); break;                                   /* MESSAGE */
    case 0xfca6: log_msg("  SE %d", a0); break;
    case 0xfca5: log_msg("  BGM %d", a0); break;
    case 0xfc7d: { int l = raw_at(s, 0); s->pc = next; if (g->skip_pressed) s->pc = l * 2; return; }  /* IF_SKIP */
    case 0xfca9: g->pending_room = a0; break;                                          /* LOAD_ROOM */
    case 0xfcc6: g->pending_script = a0; break;                                        /* LOAD_SCRIPT */
    case 0xfcc5: g->exit_requested = 1; break;                                         /* EXIT */
    default: break;                                                                    /* not modelled yet */
    }
    (void)a3;
    s->pc = next;
}
