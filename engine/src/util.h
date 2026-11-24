/* Doctor Hauzer engine - small shared helpers. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *data; size_t size; } Blob;

Blob blob_load(const char *path);          /* data == NULL on failure */
void blob_free(Blob *b);

static inline int32_t be32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]); }
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* first 16 hex chars of SHA-1, the key the HD texture packs use */
void sha1_hex16(const uint8_t *data, size_t n, char out[17]);

void path_join(char *out, size_t cap, const char *a, const char *b);
void log_msg(const char *fmt, ...);
