#include "text.h"
#include "sjis_table.h"
#include "util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

static Blob g_font_file;
static stbtt_fontinfo g_font;
static int g_font_ok;

/* ---------------------------------------------------------------- decoding */
static uint32_t sjis_char(const uint8_t **p, const uint8_t *end) {
    uint8_t a = **p;
    (*p)++;
    if (a < 0x80) return a;
    if (a >= 0xA1 && a <= 0xDF) return 0xFF61 + (a - 0xA1);       /* half-width katakana */
    if (*p >= end) return 0;
    uint8_t b = **p;
    (*p)++;
    int li = (a >= 0x81 && a <= 0x9F) ? a - 0x81 : (a >= 0xE0 && a <= 0xFC) ? a - 0xE0 + 31 : -1;
    if (li < 0 || b < 0x40) return 0;
    return SJIS_TABLE[li * 192 + (b - 0x40)];
}

static uint32_t utf8_char(const uint8_t **p, const uint8_t *end) {
    uint8_t a = *(*p)++;
    if (a < 0x80) return a;
    int n = a >= 0xF0 ? 3 : a >= 0xE0 ? 2 : a >= 0xC0 ? 1 : 0;
    uint32_t c = a & (0x3F >> n);
    for (int i = 0; i < n && *p < end; i++) c = (c << 6) | (*(*p)++ & 0x3F);
    return c;
}

/* trim ASCII / full-width spaces, tabs and control bytes from both ends */
static uint32_t *finish(uint32_t *buf, int n) {
    int a = 0, b = n;
    while (a < b && (buf[a] <= 0x20 || buf[a] == 0x3000)) a++;
    while (b > a && (buf[b - 1] <= 0x20 || buf[b - 1] == 0x3000)) b--;
    uint32_t *o = malloc(sizeof *o * (size_t)(b - a + 1));
    int k = 0;
    for (int i = a; i < b; i++) if (buf[i] >= 0x20) o[k++] = buf[i];
    o[k] = 0;
    return o;
}

static uint32_t *decode(const uint8_t *s, size_t n, int utf8) {
    uint32_t *buf = malloc(sizeof *buf * (n + 1));
    const uint8_t *p = s, *end = s + n;
    int k = 0;
    while (p < end) { uint32_t c = utf8 ? utf8_char(&p, end) : sjis_char(&p, end); if (c) buf[k++] = c; }
    uint32_t *o = finish(buf, k);
    free(buf);
    return o;
}

int messages_load(Messages *m, const char *mes_scp, const char *launchme, const char *override_utf8) {
    memset(m, 0, sizeof *m);
    m->count = 700;
    m->cp = calloc((size_t)m->count, sizeof *m->cp);
    Blob b = blob_load(mes_scp);
    if (b.data) {                                     /* lines separated by CR */
        size_t start = 0; int id = 0;
        for (size_t i = 0; i <= b.size && id < 400; i++) {
            if (i == b.size || b.data[i] == '\r') {          /* stray LF bytes are dropped by decode() */
                m->cp[id++] = decode(b.data + start, i - start, 0);
                start = i + 1;
            }
        }
        blob_free(&b);
    }
    Blob exe = blob_load(launchme);                   /* ids 400+: 32-byte strings at 0x33fec + (id - 400) * 32 */
    if (exe.data) {
        for (int id = 400; id < m->count; id++) {
            size_t off = 0x33fec + (size_t)(id - 400) * 32;
            if (off + 32 > exe.size) break;
            size_t len = 0;
            while (len < 32 && exe.data[off + len]) len++;
            m->cp[id] = decode(exe.data + off, len, 0);
        }
        blob_free(&exe);
    }
    if (override_utf8) {
        Blob o = blob_load(override_utf8);
        if (o.data) {
            size_t start = 0; int id = 0;
            for (size_t i = 0; i <= o.size && id < m->count; i++) {
                if (i == o.size || o.data[i] == '\n') {
                    size_t e = i;
                    if (e > start && o.data[e - 1] == '\r') e--;
                    if (e > start) { free(m->cp[id]); m->cp[id] = decode(o.data + start, e - start, 1); }
                    id++;
                    start = i + 1;
                }
            }
            log_msg("text: override %s (%d lines)", override_utf8, id);
            blob_free(&o);
        }
    }
    return 0;
}

const uint32_t *messages_get(const Messages *m, int id) {
    return (id >= 0 && id < m->count && m->cp[id] && m->cp[id][0]) ? m->cp[id] : NULL;
}

void messages_free(Messages *m) {
    for (int i = 0; i < m->count; i++) free(m->cp[i]);
    free(m->cp);
    memset(m, 0, sizeof *m);
}

/* ---------------------------------------------------------------- rendering */
int text_init(const char *font_path) {
    g_font_file = blob_load(font_path);
    if (!g_font_file.data || !stbtt_InitFont(&g_font, g_font_file.data, stbtt_GetFontOffsetForIndex(g_font_file.data, 0))) {
        log_msg("text: cannot load font %s", font_path);
        return -1;
    }
    g_font_ok = 1;
    return 0;
}

static float line_width(const uint32_t *cp, int n, float scale) {
    float w = 0;
    for (int i = 0; i < n; i++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_font, (int)cp[i], &adv, &lsb);
        w += adv * scale;
        if (i + 1 < n) w += stbtt_GetCodepointKernAdvance(&g_font, (int)cp[i], (int)cp[i + 1]) * scale;
    }
    return w;
}

/* Lines split at '\n', each centred. Styles: TEXT_PANEL white on a translucent rounded panel,
 * TEXT_SHADOW white with a shadow and no panel, TEXT_INK dark ink for the paper menus. */
int text_render(const uint32_t *cp, int px, int style, Image *out) {
    memset(out, 0, sizeof *out);
    if (!g_font_ok || !cp) return -1;
    float scale = stbtt_ScaleForPixelHeight(&g_font, (float)px);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &gap);
    int base = (int)(ascent * scale), lh = (int)((ascent - descent + gap) * scale), th1 = (int)((ascent - descent) * scale);
    int start[32], len[32], nl = 0;
    float lw[32], w = 0;
    for (int i = 0, st = 0;; i++) {
        if (cp[i] == '\n' || cp[i] == 0) {
            if (nl < 32) { start[nl] = st; len[nl] = i - st; lw[nl] = line_width(cp + st, i - st, scale); if (lw[nl] > w) w = lw[nl]; nl++; }
            if (!cp[i]) break;
            st = i + 1;
        }
    }
    int padx = style == TEXT_PANEL ? px : (style & 255) == TEXT_GLOW ? px / 2 : px / 4 + 2;
    int pady = style == TEXT_PANEL ? px / 2 : (style & 255) == TEXT_GLOW ? px / 3 : px / 8 + 2;
    int tw = (int)ceilf(w), th = th1 + (nl - 1) * lh;
    out->w = tw + 2 * padx;
    out->h = th + 2 * pady;
    out->rgba = calloc((size_t)out->w * out->h, 4);
    uint8_t *alpha = calloc((size_t)out->w * out->h, 1);
    for (int l = 0; l < nl; l++) {
        float x = padx + (w - lw[l]) / 2;
        const uint32_t *c = cp + start[l];
        int y = pady + l * lh;
        for (int i = 0; i < len[l]; i++) {
            int adv, lsb, x0, y0, x1, y1;
            stbtt_GetCodepointHMetrics(&g_font, (int)c[i], &adv, &lsb);
            stbtt_GetCodepointBitmapBoxSubpixel(&g_font, (int)c[i], scale, scale, x - floorf(x), 0, &x0, &y0, &x1, &y1);
            int gx = (int)floorf(x) + x0, gy = y + base + y0, gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0 && gx >= 0 && gy >= 0 && gx + gw <= out->w && gy + gh <= out->h)
                stbtt_MakeCodepointBitmapSubpixel(&g_font, alpha + (size_t)gy * out->w + gx, gw, gh, out->w, scale, scale,
                                                  x - floorf(x), 0, (int)c[i]);
            x += adv * scale;
            if (i + 1 < len[l]) x += stbtt_GetCodepointKernAdvance(&g_font, (int)c[i], (int)c[i + 1]) * scale;
        }
    }
    /* embolden a little (the default outline instance is light): blend each pixel toward the max of
     * its neighbourhood, radius ~px/30 */
    {
        int r = px / (style == TEXT_INK ? 22 : 30) > 1 ? px / (style == TEXT_INK ? 22 : 30) : 1;
        uint8_t *src = malloc((size_t)out->w * out->h);
        memcpy(src, alpha, (size_t)out->w * out->h);
        for (int y = 0; y < out->h; y++)
            for (int xx = 0; xx < out->w; xx++) {
                int m = 0;
                for (int dy = -r; dy <= r; dy++)
                    for (int dx = -r; dx <= r; dx++) {
                        int yy = y + dy, x2 = xx + dx;
                        if (yy >= 0 && yy < out->h && x2 >= 0 && x2 < out->w && src[(size_t)yy * out->w + x2] > m) m = src[(size_t)yy * out->w + x2];
                    }
                int a = src[(size_t)y * out->w + xx];
                alpha[(size_t)y * out->w + xx] = (uint8_t)(a + (m - a) * (style == TEXT_INK ? 8 : 6) / 10);
            }
        free(src);
    }
    int rad = pady, sh = px / 16 + 1;
    float *glow = NULL;
    float gc[3] = {((style >> 24) & 255) / 255.0f, ((style >> 16) & 255) / 255.0f, ((style >> 8) & 255) / 255.0f};
    if ((style & 255) == TEXT_GLOW) {   /* soft halo in the given colour: two box blurs of the glyph coverage */
        int W = out->w, H = out->h, R = px / 6 > 1 ? px / 6 : 1;
        glow = malloc(sizeof(float) * (size_t)W * H);
        float *tmp = malloc(sizeof(float) * (size_t)W * H);
        for (int i = 0; i < W * H; i++) glow[i] = alpha[i] / 255.0f;
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < H; y++)
                for (int xx = 0; xx < W; xx++) {
                    float a = 0; int n = 0;
                    for (int d = -R; d <= R; d++) { int x2 = xx + d; if (x2 >= 0 && x2 < W) { a += glow[y * W + x2]; n++; } }
                    tmp[y * W + xx] = a / n;
                }
            for (int y = 0; y < H; y++)
                for (int xx = 0; xx < W; xx++) {
                    float a = 0; int n = 0;
                    for (int d = -R; d <= R; d++) { int y2 = y + d; if (y2 >= 0 && y2 < H) { a += tmp[y2 * W + xx]; n++; } }
                    glow[y * W + xx] = a / n;
                }
        }
        free(tmp);
    }
    for (int y = 0; y < out->h; y++)
        for (int xx = 0; xx < out->w; xx++) {
            float t = alpha[(size_t)y * out->w + xx] / 255.0f;
            uint8_t *o = out->rgba + 4 * ((size_t)y * out->w + xx);
            if (glow) {
                float ga = glow[(size_t)y * out->w + xx] * 2.2f; if (ga > 1) ga = 1;
                float na = t + ga * (1 - t);
                for (int k = 0; k < 3; k++) {
                    float fill = 1.0f * 0.55f + gc[k] * 0.45f;   /* bright core tinted by the colour */
                    float cval = na > 0 ? (fill * t + gc[k] * ga * (1 - t)) / na : 0;
                    o[k] = (uint8_t)(cval * 255);
                }
                o[3] = (uint8_t)(na * 255);
                continue;
            }
            if (style == TEXT_INK) {   /* dark sepia ink */
                o[0] = 40; o[1] = 28; o[2] = 20; o[3] = (uint8_t)(t * 255);
                continue;
            }
            float bg = 0;
            if (style == TEXT_PANEL) {
                float dx = 0, dy = 0;
                if (xx < rad) dx = (float)(rad - xx); else if (xx >= out->w - rad) dx = (float)(xx - (out->w - rad - 1));
                if (y < rad) dy = (float)(rad - y); else if (y >= out->h - rad) dy = (float)(y - (out->h - rad - 1));
                float d = sqrtf(dx * dx + dy * dy);
                float pa = d > rad ? 0.0f : d > rad - 1 ? (rad - d) : 1.0f;
                bg = 0.62f * pa;
            }
            float s = (y >= sh && xx >= sh) ? alpha[(size_t)(y - sh) * out->w + (xx - sh)] / 255.0f : 0.0f;
            if (style == TEXT_SHADOW) s *= 0.85f;
            /* composite: panel, then shadow, then white text (straight alpha) */
            float a = bg, c = 0.0f;
            a = s + a * (1 - s);
            c = (c * bg * (1 - s)) / (a > 0 ? a : 1);
            float na = t + a * (1 - t);
            float nc = na > 0 ? (1.0f * t + c * a * (1 - t)) / na : 0;
            o[0] = o[1] = (uint8_t)(nc * 255); o[2] = (uint8_t)(nc * 250);
            o[3] = (uint8_t)(na * 255);
        }
    free(alpha);
    free(glow);
    return 0;
}

int text_render_box(const uint32_t *cp, int px, Image *out) { return text_render(cp, px, TEXT_PANEL, out); }

/* UTF-8 -> zero-terminated code points (cap entries) */
int text_utf8(const char *s, uint32_t *out, int cap) {
    const uint8_t *p = (const uint8_t *)s, *end = p + strlen(s);
    int n = 0;
    while (p < end && n < cap - 1) out[n++] = utf8_char(&p, end);
    out[n] = 0;
    return n;
}

/* Shift-JIS (zero-terminated) -> code points */
int text_sjis(const char *s, uint32_t *out, int cap) {
    const uint8_t *p = (const uint8_t *)s, *end = p + strlen(s);
    int n = 0;
    while (p < end && n < cap - 1) out[n++] = sjis_char(&p, end);
    out[n] = 0;
    return n;
}
