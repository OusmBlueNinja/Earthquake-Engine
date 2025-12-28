#pragma once
#include <stdint.h>
#include "ui_types.h"

typedef enum ui_cmd_type_t
{
    UI_CMD_NONE = 0,
    UI_CMD_CLIP,
    UI_CMD_RECT,
    UI_CMD_TEXT,
    UI_CMD_ICON,
    UI_CMD_IMAGE
} ui_cmd_type_t;

typedef struct ui_cmd_clip_t
{
    ui_vec4_t rect;
} ui_cmd_clip_t;

typedef struct ui_cmd_rect_t
{
    ui_vec4_t rect;
    ui_color_t color;
    float radius;
    float thickness;
} ui_cmd_rect_t;

typedef struct ui_cmd_text_t
{
    ui_vec2_t pos;
    ui_color_t color;
    uint32_t font_id;
    const char *text;
} ui_cmd_text_t;

typedef struct ui_cmd_icon_t
{
    ui_vec4_t rect;
    ui_color_t color;
    uint32_t font_id;
    uint32_t icon_id;
} ui_cmd_icon_t;

typedef struct ui_cmd_image_t
{
    ui_vec4_t rect;
    uint32_t gl_tex;
    ui_vec4_t uv;
    ui_color_t tint;
} ui_cmd_image_t;

typedef struct ui_cmd_t
{
    ui_cmd_type_t type;
    union
    {
        ui_cmd_clip_t clip;
        ui_cmd_rect_t rect;
        ui_cmd_text_t text;
        ui_cmd_icon_t icon;
        ui_cmd_image_t image;
    };
} ui_cmd_t;

typedef void *(*ui_realloc_fn)(void *user, void *ptr, uint32_t size);

typedef struct ui_cmd_stream_t
{
    ui_realloc_fn rfn;
    void *ruser;

    ui_cmd_t *cmds;
    uint32_t count;
    uint32_t cap;

    char *str;
    uint32_t str_len;
    uint32_t str_cap;
} ui_cmd_stream_t;

void ui_cmd_stream_init(ui_cmd_stream_t *s, ui_realloc_fn rfn, void *ruser);
void ui_cmd_stream_free(ui_cmd_stream_t *s);
void ui_cmd_stream_reset(ui_cmd_stream_t *s);

uint32_t ui_cmd_count(const ui_cmd_stream_t *s);
const ui_cmd_t *ui_cmd_data(const ui_cmd_stream_t *s);

void ui_cmd_push_clip(ui_cmd_stream_t *s, ui_vec4_t rect);
void ui_cmd_push_rect(ui_cmd_stream_t *s, ui_vec4_t rect, ui_color_t color, float radius, float thickness);
void ui_cmd_push_text(ui_cmd_stream_t *s, ui_vec2_t pos, ui_color_t color, uint32_t font_id, const char *text);
void ui_cmd_push_text_n(ui_cmd_stream_t *s, ui_vec2_t pos, ui_color_t color, uint32_t font_id, const char *text, uint32_t n);
void ui_cmd_push_icon(ui_cmd_stream_t *s, ui_vec4_t rect, ui_color_t color, uint32_t font_id, uint32_t icon_id);
void ui_cmd_push_image(ui_cmd_stream_t *s, ui_vec4_t rect, uint32_t gl_tex, ui_vec4_t uv, ui_color_t tint);

static inline ui_color_t ui_color(ui_vec3_t rgb, float a)
{
    ui_color_t c;
    c.rgb = rgb;
    c.a = a;
    return c;
}
