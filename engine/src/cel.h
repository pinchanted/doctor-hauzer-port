/* 3DO CEL decoder (CCB / PLUT / PDAT chunks) -> RGBA8. Port of tools/cel.py. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *rgba;   /* w*h*4, straight alpha; caller frees */
} Image;

int cel_decode(const uint8_t *data, size_t size, Image *out);   /* 0 on success */
void image_free(Image *im);
