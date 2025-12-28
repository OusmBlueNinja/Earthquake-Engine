#pragma once
#include <stdint.h>

#include "ui_types.h"

typedef struct ui_font_t
{
    ui_realloc_fn rfn;
    void *ruser;

    uint8_t *rgba;
    uint32_t rgba_size;

    int atlas_w;
    int atlas_h;

    int cell_w;
    int cell_h;
    int cols;
    int rows;

    uint32_t first_char;
    uint32_t char_count;

    float advance_x;
    float advance_y;
} ui_font_t;

int ui_font_load_ttf_file(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const char *path, float px_height, uint32_t first_char, uint32_t char_count);
int ui_font_load_ttf_memory(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const void *data, uint32_t size, float px_height, uint32_t first_char, uint32_t char_count);

void ui_font_free(ui_font_t *f);
