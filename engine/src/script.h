/* Room script VM - port of launchme's interpreter (dispatcher 0xa500, event entry 0x6eb4).
 *
 * A script is a stream of (tag, value) int32 pairs. Every event (room entered, every frame, floor
 * trigger, examine, use item) restarts it from the top: the DEF block re-initialises the locals,
 * BEGIN starts execution, and the ON / ON3 lines act as a switch on the event. WAIT ends the run.
 * Globals (g0..g255) and flags persist. In-script DRAW / SLEEP / FADE suspend the run for a frame so
 * cutscene loops play out in real time; events raised meanwhile are dropped, as on the 3DO where
 * the script ran synchronously. */
#pragma once
#include <stdint.h>
#include "util.h"

struct Game;

typedef struct {
    Blob file;
    int32_t *w;          /* host-endian words */
    int nw;
    int pc;              /* word index */
    int started, yield, suspended;
    int wait;            /* frames to wait before resuming */
    int event[3];
    int locals[256];
    int call[32], ncall;
    int for_pc[16], for_step[16], for_to[16], nfor;
    int mark;
    int steps;
} Script;

extern int g_globals[256];
extern int g_flags[32];

int script_load(Script *s, const char *path);
void script_free(Script *s);
void script_event(Script *s, struct Game *g, int a, int b, int c);   /* start a run */
void script_resume(Script *s, struct Game *g);                        /* continue a suspended run */
