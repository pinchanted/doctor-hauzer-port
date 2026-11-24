#include "util.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Blob blob_load(const char *path) {
    Blob b = {0};
    FILE *f = fopen(path, "rb");
    if (!f) return b;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) {
        b.data = malloc((size_t)n);
        if (b.data && fread(b.data, 1, (size_t)n, f) == (size_t)n) b.size = (size_t)n;
        else { free(b.data); b.data = NULL; }
    }
    fclose(f);
    return b;
}

void blob_free(Blob *b) { free(b->data); b->data = NULL; b->size = 0; }

void path_join(char *out, size_t cap, const char *a, const char *b) {
    size_t n = strlen(a);
    snprintf(out, cap, "%s%s%s", a, (n && a[n - 1] != '/' && a[n - 1] != '\\') ? "/" : "", b);
}

void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* --- SHA-1 (public-domain style, compact) --- */
#define ROL(v, b) (((v) << (b)) | ((v) >> (32 - (b))))
static void sha1_block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1_hex16(const uint8_t *data, size_t n, char out[17]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha1_block(h, data + i);
    uint8_t tail[128] = {0};
    size_t r = n - i;
    memcpy(tail, data + i, r);
    tail[r] = 0x80;
    size_t tl = (r + 9 <= 64) ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) tail[tl - 1 - k] = (uint8_t)(bits >> (8 * k));
    sha1_block(h, tail);
    if (tl == 128) sha1_block(h, tail + 64);
    static const char hx[] = "0123456789abcdef";
    for (int k = 0; k < 8; k++) {
        uint8_t byte = (uint8_t)(h[k / 4] >> (24 - 8 * (k % 4)));
        out[2 * k] = hx[byte >> 4];
        out[2 * k + 1] = hx[byte & 15];
    }
    out[16] = 0;
}
