#pragma once
#include <stdint.h>

typedef void *(*ui_realloc_fn)(void *user, void *ptr, uint32_t size);

typedef struct ui_glyph_t
{
    uint32_t cp;
    float u0, v0, u1, v1;
    int w, h;
    int xoff, yoff;
    float xadvance;
    uint8_t used;
} ui_glyph_t;

typedef struct ui_font_t
{
    ui_realloc_fn rfn;
    void *ruser;

    uint8_t *ttf;
    uint32_t ttf_size;

    void *stb_info;

    float px_height;
    float scale;
    int ascent;
    int descent;
    int linegap;

    uint8_t *rgba;
    uint32_t rgba_size;
    int atlas_w;
    int atlas_h;

    int pen_x;
    int pen_y;
    int row_h;

    ui_glyph_t *glyphs;
    uint32_t glyph_cap;
    uint32_t glyph_count;

    uint8_t dirty;
} ui_font_t;

int ui_font_load_ttf_file(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const char *path, float px_height);
int ui_font_load_ttf_memory(ui_font_t *f, ui_realloc_fn rfn, void *ruser, const void *data, uint32_t size, float px_height);
void ui_font_preload_common(ui_font_t *f);

void ui_font_free(ui_font_t *f);

const ui_glyph_t *ui_font_get_glyph(ui_font_t *f, uint32_t codepoint);

void ui_font_measure_utf8(ui_font_t *f, const char *text, float *out_w, float *out_h);
void ui_font_draw_prepare_utf8(ui_font_t *f, const char *text);
