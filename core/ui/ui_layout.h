#pragma once
#include "ui_types.h"

typedef struct ui_layout_t
{
    ui_vec4_t body;

    float cursor_x;
    float cursor_y;

    float row_h;
    int cols;
    int col_i;
    float col_w[16];

    float max_y;
    float scroll_y;

    float row_spacing;
    ui_vec4_t last;
    float start_y;

    /* flow-style layout helpers (imgui-like) */
    float flow_line_max_h;
    uint8_t flow_line_started;
} ui_layout_t;

void ui_layout_begin(ui_layout_t *l, ui_vec4_t body, float pad);
void ui_layout_row(ui_layout_t *l, float height, int cols, const float *widths, float spacing);
ui_vec4_t ui_layout_next(ui_layout_t *l, float spacing);
ui_vec4_t ui_layout_next_flow(ui_layout_t *l, ui_vec2_t size, int same_line, float spacing);
void ui_layout_new_line(ui_layout_t *l, float spacing);
ui_vec4_t ui_layout_peek_last(const ui_layout_t *l);
float ui_layout_content_h(const ui_layout_t *l);
