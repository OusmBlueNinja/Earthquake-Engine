#include "ui_widgets.h"

ui_vec4_t ui_next_rect(ui_ctx_t *ui)
{
    return ui_layout_next(&ui->layout, ui->style.spacing);
}

static int ui_text_w(ui_ctx_t *ui, uint32_t font_id, const char *text)
{
    return ui->text_width(ui->text_user, font_id, text, -1);
}

static float ui_text_h(ui_ctx_t *ui, uint32_t font_id)
{
    return ui->text_height(ui->text_user, font_id);
}

ui_widget_result_t ui_button_ex(ui_ctx_t *ui, uint32_t id, ui_vec4_t rect)
{
    ui_widget_result_t r;
    r.hovered = 0;
    r.held = 0;
    r.pressed = 0;
    r.released = 0;

    int inside = ui_pt_in_rect(ui->mouse, rect);
    if (inside)
        ui->hot_id = id;

    if (inside)
        r.hovered = 1;

    if (inside && ui->mouse_pressed[0])
        ui->active_id = id;

    if (ui->active_id == id && ui->mouse_down[0])
        r.held = 1;

    if (ui->active_id == id && ui->mouse_released[0])
    {
        r.released = 1;
        if (inside)
            r.pressed = 1;
        ui->active_id = 0;
    }

    return r;
}

int ui_button(ui_ctx_t *ui, const char *label, uint32_t font_id)
{
    ui_vec4_t rect = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##btn");
    ui_widget_result_t st = ui_button_ex(ui, id, rect);

    ui_color_t col = ui->style.btn;
    if (ui->active_id == id && st.held)
        col = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        col = ui->style.btn_hover;

    ui_draw_rect(ui, rect, col, ui->style.corner, 0.0f);
    ui_draw_rect(ui, rect, ui->style.outline, ui->style.corner, 1.0f);

    if (label)
    {
        int tw = ui_text_w(ui, font_id, label);
        float th = ui_text_h(ui, font_id);
        ui_vec2_t c = ui_rect_center(rect);
        ui_vec2_t pos = ui_v2(c.x - (float)tw * 0.5f, c.y - th * 0.5f);
        ui_draw_text(ui, pos, ui->style.text, font_id, label);
    }

    return st.pressed ? 1 : 0;
}

void ui_label(ui_ctx_t *ui, const char *text, uint32_t font_id)
{
    ui_vec4_t rect = ui_next_rect(ui);
    if (!text)
        return;
    float th = ui_text_h(ui, font_id);
    ui_vec2_t pos = ui_v2(rect.x, rect.y + (rect.w - th) * 0.5f);
    ui_draw_text(ui, pos, ui->style.text, font_id, text);
}

int ui_checkbox(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value)
{
    ui_vec4_t rect = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##chk");
    ui_widget_result_t st = ui_button_ex(ui, id, rect);

    float box = ui_minf(rect.w, rect.z);
    box = ui_minf(box, ui->style.line_h);
    ui_vec4_t b = ui_v4(rect.x, rect.y + (rect.w - box) * 0.5f, box, box);

    ui_color_t col = ui->style.btn;
    if (ui->active_id == id && st.held)
        col = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        col = ui->style.btn_hover;

    ui_draw_rect(ui, b, col, ui->style.corner, 0.0f);
    ui_draw_rect(ui, b, ui->style.outline, ui->style.corner, 1.0f);

    if (value && *value)
    {
        ui_vec4_t inner = ui_v4(b.x + 4.0f, b.y + 4.0f, ui_maxf(0.0f, b.z - 8.0f), ui_maxf(0.0f, b.w - 8.0f));
        ui_draw_rect(ui, inner, ui->style.text, ui->style.corner, 0.0f);
    }

    if (label)
    {
        ui_vec2_t pos = ui_v2(b.x + b.z + ui->style.spacing, rect.y + (rect.w - ui_text_h(ui, font_id)) * 0.5f);
        ui_draw_text(ui, pos, ui->style.text, font_id, label);
    }

    if (st.pressed && value)
        *value = !*value;
    return st.pressed ? 1 : 0;
}

static float ui_clampf(float v, float a, float b)
{
    if (v < a)
        return a;
    if (v > b)
        return b;
    return v;
}

int ui_slider_float(ui_ctx_t *ui, const char *label, uint32_t font_id, float *value, float minv, float maxv)
{
    ui_vec4_t rect = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##sld");

    int inside = ui_pt_in_rect(ui->mouse, rect);
    if (inside)
        ui->hot_id = id;

    if (inside && ui->mouse_pressed[0])
        ui->active_id = id;

    int changed = 0;

    if (ui->active_id == id && ui->mouse_down[0] && value)
    {
        float t = (ui->mouse.x - rect.x) / (rect.z > 1e-6f ? rect.z : 1.0f);
        t = ui_clampf(t, 0.0f, 1.0f);
        float nv = minv + (maxv - minv) * t;
        if (nv != *value)
        {
            *value = nv;
            changed = 1;
        }
    }

    ui_color_t col = ui->style.btn;
    if (ui->active_id == id && ui->mouse_down[0])
        col = ui->style.btn_active;
    else if (ui->hot_id == id && inside)
        col = ui->style.btn_hover;

    ui_draw_rect(ui, rect, col, ui->style.corner, 0.0f);
    ui_draw_rect(ui, rect, ui->style.outline, ui->style.corner, 1.0f);

    float t = 0.0f;
    if (value && (maxv - minv) != 0.0f)
        t = (*value - minv) / (maxv - minv);
    t = ui_clampf(t, 0.0f, 1.0f);

    ui_vec4_t fill = ui_v4(rect.x, rect.y, rect.z * t, rect.w);
    ui_color_t fcol = ui_color(ui->style.text.rgb, 0.25f);
    ui_draw_rect(ui, fill, fcol, ui->style.corner, 0.0f);

    if (label)
    {
        int tw = ui_text_w(ui, font_id, label);
        float th = ui_text_h(ui, font_id);
        ui_vec2_t c = ui_rect_center(rect);
        ui_vec2_t pos = ui_v2(c.x - (float)tw * 0.5f, c.y - th * 0.5f);
        ui_draw_text(ui, pos, ui->style.text, font_id, label);
    }

    if (ui->active_id == id && ui->mouse_released[0])
        ui->active_id = 0;
    return changed;
}
