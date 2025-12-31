#pragma once
#include <stdint.h>

typedef struct asset_image_t
{
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint8_t *pixels;
    uint32_t gl_handle;
    uint32_t is_float;
    uint32_t has_alpha;
    uint32_t has_smooth_alpha;

    uint32_t mip_count;
    uint32_t reserved0;
    uint64_t vram_bytes;
} asset_image_t;
