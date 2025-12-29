#pragma once
#include <stdint.h>
#include "ui_types.h"
#include "ui_commands.h"
#include "ui_layout.h"
#include "ui_events.h"
#include "ui_backend.h"

typedef struct ui_style_t
{
    ui_color_t bg;
    ui_color_t panel_bg;
    ui_color_t text;

    ui_color_t btn;
    ui_color_t btn_hover;
    ui_color_t btn_active;

    ui_color_t outline;

    ui_color_t header_bg;
    ui_color_t header_bg_active;
    ui_color_t header_text;

    ui_color_t window_bg;
    ui_color_t window_shadow;

    ui_color_t accent;
    ui_color_t accent_dim;

    ui_color_t scroll_track;
    ui_color_t scroll_thumb;
    ui_color_t scroll_thumb_hover;

    ui_color_t separator;

    float padding;
    float spacing;
    float line_h;
    float corner;

    float window_corner;
    float header_corner;
    float shadow_size;
    float outline_thickness;
    float scroll_w;
    float scroll_pad;
} ui_style_t;

typedef struct ui_ctx_t
{
    ui_realloc_fn rfn;
    void *ruser;

    ui_text_width_fn text_width;
    ui_text_height_fn text_height;
    void *text_user;

    ui_vec2i_t fb_size;

    ui_vec2_t mouse;
    uint8_t mouse_down[8];
    uint8_t mouse_prev[8];
    uint8_t mouse_pressed[8];
    uint8_t mouse_released[8];

    uint8_t key_down[512];
    uint8_t key_prev[512];
    uint8_t key_pressed[512];
    uint8_t key_released[512];

    uint32_t char_buf[32];
    uint32_t char_count;

    uint32_t hot_id;
    uint32_t active_id;

    uint32_t id_stack[32];
    int id_top;
    uint32_t id_seed;

    ui_vec4_t clip_stack[16];
    int clip_top;

    ui_cmd_stream_t stream;
    ui_layout_t layout;
    ui_style_t style;

    ui_base_backend_t *backend;

    ui_array_t windows;

    uint32_t cur_window_id;
    ui_vec4_t cur_window_rect;

    uint8_t next_win_has_pos;
    uint8_t next_win_has_size;
    ui_vec2_t next_win_pos;
    ui_vec2_t next_win_size;

    uint32_t win_drag_id;
    uint32_t win_resize_mask;
    int win_drag_mode;
    ui_vec2_t win_drag_start_mouse;
    ui_vec4_t win_drag_start_rect;

    /* imgui-style helpers */
    float next_item_w;
    float next_item_h;
    float next_item_spacing;
    uint8_t next_same_line;
} ui_ctx_t;

void ui_init(ui_ctx_t *ui, ui_realloc_fn rfn, void *ruser);
void ui_shutdown(ui_ctx_t *ui);

void ui_input_mouse_pos(ui_ctx_t *ui, ui_vec2_t pos);
void ui_input_mouse_btn(ui_ctx_t *ui, int button, int down);

void ui_input_key(ui_ctx_t *ui, uint32_t key, int down, uint8_t repeat, uint8_t mods);
void ui_input_char(ui_ctx_t *ui, uint32_t codepoint);

void ui_begin(ui_ctx_t *ui, ui_vec2i_t fb_size);
void ui_end(ui_ctx_t *ui);

const ui_cmd_t *ui_commands(const ui_ctx_t *ui, uint32_t *out_count);

void ui_push_clip(ui_ctx_t *ui, ui_vec4_t rect);
void ui_pop_clip(ui_ctx_t *ui);

void ui_push_id_str(ui_ctx_t *ui, const char *s);
void ui_push_id_ptr(ui_ctx_t *ui, const void *p);
void ui_push_id_u32(ui_ctx_t *ui, uint32_t v);
void ui_pop_id(ui_ctx_t *ui);

uint32_t ui_id_str(ui_ctx_t *ui, const char *s);
uint32_t ui_id_ptr(ui_ctx_t *ui, const void *p);
uint32_t ui_id_u32(ui_ctx_t *ui, uint32_t v);

void ui_set_next_item_width(ui_ctx_t *ui, float width);
void ui_set_next_item_height(ui_ctx_t *ui, float height);
void ui_set_next_item_spacing(ui_ctx_t *ui, float spacing);
void ui_same_line(ui_ctx_t *ui, float spacing);
void ui_new_line(ui_ctx_t *ui);

void ui_begin_panel(ui_ctx_t *ui, ui_vec4_t rect);
void ui_end_panel(ui_ctx_t *ui);

void ui_row(ui_ctx_t *ui, float height, int cols, const float *widths);

void ui_draw_rect(ui_ctx_t *ui, ui_vec4_t rect, ui_color_t color, float radius, float thickness);
void ui_draw_text(ui_ctx_t *ui, ui_vec2_t pos, ui_color_t color, uint32_t font_id, const char *text);
void ui_draw_icon(ui_ctx_t *ui, ui_vec4_t rect, ui_color_t color, uint32_t font_id, uint32_t icon_id);
void ui_draw_image(ui_ctx_t *ui, ui_vec4_t rect, uint32_t gl_tex, ui_vec4_t uv, ui_color_t tint);

void ui_attach_backend(ui_ctx_t *ui, ui_base_backend_t *backend);
void ui_render(ui_ctx_t *ui);

void ui_on_event(struct ui_ctx_t *ui, const ui_event_t *e);
