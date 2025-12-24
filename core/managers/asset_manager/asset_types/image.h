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
} asset_image_t;