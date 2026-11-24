/* Text: the game's messages (OrgData/mes/mes.scp, Shift-JIS, one per line; ids 400+ from a table in
 * launchme) rendered at screen resolution with a TrueType font. An optional UTF-8 override file
 * (one message per line, same numbering) replaces the Japanese text, e.g. with an English translation. */
#pragma once
#include <stdint.h>
#include "cel.h"

typedef struct {
    int count;
    uint32_t **cp;   /* per message: zero-terminated code points, padding trimmed */
} Messages;

int messages_load(Messages *m, const char *mes_scp, const char *launchme, const char *override_utf8);
const uint32_t *messages_get(const Messages *m, int id);
void messages_free(Messages *m);

int text_init(const char *font_path);
/* white text with a soft shadow on a translucent rounded panel; straight-alpha RGBA */
int text_render_box(const uint32_t *cp, int px, Image *out);
