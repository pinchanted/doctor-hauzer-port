#include "cel.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *d; size_t n; size_t bit; } Bits;

static uint32_t bits_read(Bits *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        size_t byte = b->bit >> 3;
        int bit = byte < b->n ? (b->d[byte] >> (7 - (b->bit & 7))) & 1 : 0;
        v = (v << 1) | (uint32_t)bit;
        b->bit++;
    }
    return v;
}

typedef struct {
    uint32_t flags, pixc, pre0, pre1;
    int w, h, bpp, uncoded, packed, bgnd, amv;
    uint16_t plut[256]; int nplut;
    const uint8_t *pdat; size_t npdat;
} Cel;

static void rgb555(uint16_t p, uint8_t *o) {
    int r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
    o[0] = (uint8_t)(r << 3 | r >> 2); o[1] = (uint8_t)(g << 3 | g >> 2); o[2] = (uint8_t)(b << 3 | b >> 2);
}

static void pixel(const Cel *c, uint32_t v, uint8_t *o) {
    o[3] = 255;
    if (c->bpp == 16) {
        if (v == 0 && !c->bgnd) { memset(o, 0, 4); return; }
        rgb555(c->uncoded || !c->nplut ? (uint16_t)v : c->plut[v & 31], o);
        return;
    }
    if (c->uncoded && c->bpp == 8) {
        o[0] = (uint8_t)(((v >> 5) & 7) * 255 / 7); o[1] = (uint8_t)(((v >> 2) & 7) * 255 / 7); o[2] = (uint8_t)((v & 3) * 255 / 3);
        return;
    }
    int idx = (int)(v & 31);
    if (!c->nplut || idx >= c->nplut) { o[0] = o[1] = o[2] = (uint8_t)v; return; }
    uint16_t p = c->plut[idx];
    if (p == 0 && !c->bgnd) { memset(o, 0, 4); return; }
    rgb555(p, o);
    if (c->bpp == 8 && c->amv) {
        int m = (int)((v >> 5) & 7) + 1;
        o[0] = (uint8_t)(o[0] * m / 8); o[1] = (uint8_t)(o[1] * m / 8); o[2] = (uint8_t)(o[2] * m / 8);
    }
}

int cel_decode(const uint8_t *data, size_t size, Image *out) {
    static const int bpp_tab[8] = {0, 1, 2, 4, 6, 8, 16, 0};
    Cel c; memset(&c, 0, sizeof c);
    const uint8_t *ccb = NULL;
    size_t off = 0;
    while (off + 8 <= size) {
        uint32_t sz = (uint32_t)be32(data + off + 4);
        if (sz < 8) break;
        const uint8_t *body = data + off + 8;
        if (!memcmp(data + off, "CCB ", 4)) ccb = body;
        else if (!memcmp(data + off, "PLUT", 4)) {
            int n = be32(body); if (n > 256) n = 256;
            for (int i = 0; i < n; i++) c.plut[i] = be16(body + 4 + 2 * i);
            c.nplut = n;
        } else if (!memcmp(data + off, "PDAT", 4)) { c.pdat = body; c.npdat = sz - 8; }
        off += (sz + 3) & ~3u;
    }
    if (!ccb || !c.pdat) return -1;
    c.flags = (uint32_t)be32(ccb + 4);
    c.pixc = (uint32_t)be32(ccb + 52);
    c.pre0 = (uint32_t)be32(ccb + 56); c.pre1 = (uint32_t)be32(ccb + 60);
    c.w = be32(ccb + 64); c.h = be32(ccb + 68);
    c.bpp = bpp_tab[c.pre0 & 7];
    c.uncoded = (c.pre0 >> 4) & 1;
    c.packed = (c.flags >> 9) & 1;
    c.bgnd = (c.flags >> 5) & 1;
    if (!c.packed) { c.h = (int)((c.pre0 >> 6) & 0x3FF) + 1; c.w = (int)(c.pre1 & 0x7FF) + 1; }
    c.amv = ((c.pixc >> 13) & 3) == 1;
    if (c.w <= 0 || c.h <= 0 || c.w > 2048 || c.h > 2048 || !c.bpp) return -1;

    out->w = c.w; out->h = c.h;
    out->rgba = calloc((size_t)c.w * c.h, 4);
    if (!out->rgba) return -1;
    if (!c.packed) {
        size_t stride = (((c.pre1 >> 16) & (c.bpp >= 8 ? 0x3FF : 0xFF)) + 2) * 4;
        for (int y = 0; y < c.h; y++) {
            Bits b = {c.pdat, c.npdat, y * stride * 8};
            for (int x = 0; x < c.w; x++) pixel(&c, bits_read(&b, c.bpp), out->rgba + 4 * ((size_t)y * c.w + x));
        }
        return 0;
    }
    size_t pos = 0;
    int offw = c.bpp >= 8 ? 2 : 1;
    for (int y = 0; y < c.h; y++) {
        if (pos >= c.npdat) continue;
        size_t nxt = (size_t)(offw == 1 ? c.pdat[pos] : (c.pdat[pos] << 8 | c.pdat[pos + 1])) + 2;
        Bits b = {c.pdat, c.npdat, pos * 8};
        bits_read(&b, offw * 8);
        int x = 0;
        while (x < c.w) {
            uint32_t typ = bits_read(&b, 2);
            if (typ == 0) break;
            int cnt = (int)bits_read(&b, 6) + 1;
            if (typ == 1) {
                for (int i = 0; i < cnt; i++, x++) {
                    uint32_t v = bits_read(&b, c.bpp);
                    if (x < c.w) pixel(&c, v, out->rgba + 4 * ((size_t)y * c.w + x));
                }
            } else if (typ == 2) {
                x += cnt;
            } else {
                uint8_t px[4]; pixel(&c, bits_read(&b, c.bpp), px);
                for (int i = 0; i < cnt; i++, x++) if (x < c.w) memcpy(out->rgba + 4 * ((size_t)y * c.w + x), px, 4);
            }
        }
        pos += nxt * 4;
    }
    return 0;
}

void image_free(Image *im) { free(im->rgba); im->rgba = NULL; }

/* 3DO IMAG pictures (.img: 16 bpp, linear or LRform) and full-screen CEL stills (the English patch).
 * Port of tools/img2png.py. */
static const uint8_t *find4(const uint8_t *d, size_t n, const char *tag) {
    for (size_t i = 0; i + 4 <= n; i++) if (!memcmp(d + i, tag, 4)) return d + i;
    return NULL;
}

int img_decode(const uint8_t *d, size_t n, Image *out) {
    memset(out, 0, sizeof *out);
    const uint8_t *i = find4(d, n, "IMAG"), *p = find4(d, n, "PDAT");
    int w, h, bpr, lr;
    if (i && (size_t)(i - d) + 27 <= n) {
        w = be32(i + 8); h = be32(i + 12); bpr = be32(i + 16);
        if (i[20] != 16) return -1;
        lr = i[26] == 1;
    } else {
        const uint8_t *c = find4(d, n, "CCB ");
        if (!c || (size_t)(c - d) + 8 + 72 > n) return -1;
        const uint8_t *b = c + 8;
        uint32_t pre0 = (uint32_t)be32(b + 56), pre1 = (uint32_t)be32(b + 60);
        w = be32(b + 64); h = be32(b + 68);
        if ((pre0 & 7) != 6) return -1;
        lr = (pre1 >> 11) & 1;
        bpr = lr ? w * 4 : w * 2;
    }
    if (!p || w <= 0 || h <= 0 || w > 2048 || h > 2048) return -1;
    const uint8_t *pix = p + 8, *end = d + n;
    out->w = w; out->h = h;
    out->rgba = calloc((size_t)w * h, 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *q = lr ? pix + (size_t)(y / 2) * w * 4 + x * 4 + (y & 1) * 2 : pix + (size_t)y * bpr + x * 2;
            if (q + 2 > end) continue;
            int v = q[0] << 8 | q[1], r = (v >> 10) & 31, g = (v >> 5) & 31, bl = v & 31;
            uint8_t *o = out->rgba + 4 * ((size_t)y * w + x);
            o[0] = (uint8_t)(r << 3 | r >> 2); o[1] = (uint8_t)(g << 3 | g >> 2); o[2] = (uint8_t)(bl << 3 | bl >> 2); o[3] = 255;
        }
    return 0;
}
