#include "ui_layout.h"

static float ui_max2(float a, float b) { return a > b ? a : b; }

void ui_layout_begin(ui_layout_t *l, ui_vec4_t body, float pad)
{
    l->body.x = body.x + pad;
    l->body.y = body.y + pad;
    l->body.z = ui_maxf(0.0f, body.z - pad * 2.0f);
    l->body.w = ui_maxf(0.0f, body.w - pad * 2.0f);

    l->cursor_x = l->body.x;
    l->cursor_y = l->body.y - l->scroll_y;

    l->row_h = 0.0f;
    l->cols = 0;
    l->col_i = 0;

    for (int i = 0; i < 16; ++i)
        l->col_w[i] = 0.0f;

    l->row_spacing = 0.0f;
    l->start_y = l->cursor_y;
    l->max_y = l->cursor_y;

    l->last = ui_v4(l->body.x, l->body.y, 0.0f, 0.0f);
}

void ui_layout_row(ui_layout_t *l, float height, int cols, const float *widths, float spacing)
{
    if (cols < 1)
        cols = 1;
    if (cols > 16)
        cols = 16;

    if (height < 0.0f)
        height = 0.0f;

    l->row_h = height;
    l->cols = cols;
    l->col_i = 0;
    l->row_spacing = spacing;

    float fixed = 0.0f;
    int auto_count = 0;

    for (int i = 0; i < cols; ++i)
    {
        float w = widths ? widths[i] : 0.0f;
        if (w > 0.0f)
            fixed += w;
        else
            auto_count++;
        l->col_w[i] = w;
    }

    float gaps = (cols > 1) ? (float)(cols - 1) * spacing : 0.0f;
    float avail = l->body.z - fixed - gaps;
    if (avail < 0.0f)
        avail = 0.0f;

    float auto_w = (auto_count > 0) ? (avail / (float)auto_count) : 0.0f;
    if (auto_w < 0.0f)
        auto_w = 0.0f;

    for (int i = 0; i < cols; ++i)
        if (l->col_w[i] <= 0.0f)
            l->col_w[i] = auto_w;

    l->cursor_x = l->body.x;
}

ui_vec4_t ui_layout_next(ui_layout_t *l, float spacing)
{
    if (l->cols < 1)
        l->cols = 1;

    float use_spacing = (spacing != 0.0f) ? spacing : l->row_spacing;

    float w = l->col_w[l->col_i];
    if (w <= 0.0f)
        w = l->body.z;

    ui_vec4_t r = ui_v4(l->cursor_x, l->cursor_y, w, l->row_h);
    l->last = r;

    l->col_i++;
    if (l->col_i >= l->cols)
    {
        l->col_i = 0;
        l->cursor_x = l->body.x;
        l->cursor_y += l->row_h + use_spacing;
    }
    else
    {
        l->cursor_x += w + use_spacing;
    }

    l->max_y = ui_max2(l->max_y, r.y + r.w);
    return r;
}

ui_vec4_t ui_layout_peek_last(const ui_layout_t *l)
{
    return l->last;
}

float ui_layout_content_h(const ui_layout_t *l)
{
    float top = l->start_y;
    float h = l->max_y - top;
    if (h < 0.0f)
        h = 0.0f;
    return h;
}
