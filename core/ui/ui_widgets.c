#include "ui_widgets.h"
#include <string.h>
#include <stdio.h>

#ifndef UI_KEY_BACKSPACE
#define UI_KEY_BACKSPACE 8
#endif
#ifndef UI_KEY_DELETE
#define UI_KEY_DELETE 127
#endif
#ifndef UI_KEY_LEFT
#define UI_KEY_LEFT 256
#endif
#ifndef UI_KEY_RIGHT
#define UI_KEY_RIGHT 257
#endif
#ifndef UI_KEY_ENTER
#define UI_KEY_ENTER 13
#endif
#ifndef UI_KEY_ESCAPE
#define UI_KEY_ESCAPE 27
#endif

typedef struct uiw_kv_i_t
{
    uint32_t k;
    int v;
    uint8_t used;
} uiw_kv_i_t;

typedef struct uiw_input_state_t
{
    uint32_t id;
    int cursor_byte;
    int sel0_byte;
    int sel1_byte;
} uiw_input_state_t;

typedef struct uiw_popup_state_t
{
    ui_ctx_t *ui;
    uint32_t open_id;
    uint32_t active_id;
    uint8_t open;
    ui_vec4_t rect;
} uiw_popup_state_t;

typedef struct uiw_child_state_t
{
    uint32_t id;
    float scroll_y;
    float content_h_last;
    uint8_t has_scroll;

    uint8_t scroll_drag;
    float scroll_drag_y0;
    float scroll_drag_scroll0;

    ui_vec4_t rect;
    ui_vec4_t content_rect;

    ui_layout_t saved_layout;
    float saved_layout_scroll_y;
} uiw_child_state_t;

static uiw_kv_i_t g_open_headers[256];
static uiw_input_state_t g_inputs[64];
static uiw_popup_state_t g_popups[8];

static uiw_child_state_t g_child_stack[32];
static int g_child_top = 0;

static ui_vec4_t g_last_item_rects[8];
static ui_ctx_t *g_last_item_uis[8];

static int ui_text_w(ui_ctx_t *ui, uint32_t font_id, const char *text)
{
    return ui->text_width(ui->text_user, font_id, text, -1);
}

static float ui_text_h(ui_ctx_t *ui, uint32_t font_id)
{
    return ui->text_height(ui->text_user, font_id);
}

static float ui_clampf(float v, float a, float b)
{
    if (v < a)
        return a;
    if (v > b)
        return b;
    return v;
}

static float uiw_outline_th(ui_ctx_t *ui)
{
    float t = ui->style.outline_thickness;
    if (t <= 0.0f)
        t = 1.0f;
    return t;
}

static void uiw_draw_outline(ui_ctx_t *ui, ui_vec4_t r, float radius)
{
    float t = uiw_outline_th(ui);
    ui_draw_rect(ui, r, ui->style.outline, radius, t);
}

static int uiw_ctx_slot(ui_ctx_t *ui)
{
    for (int i = 0; i < 8; ++i)
        if (g_last_item_uis[i] == ui)
            return i;
    for (int i = 0; i < 8; ++i)
        if (!g_last_item_uis[i])
        {
            g_last_item_uis[i] = ui;
            g_last_item_rects[i] = ui_v4(0, 0, 0, 0);
            return i;
        }
    return 0;
}

static void uiw_set_last_item(ui_ctx_t *ui, ui_vec4_t r)
{
    int s = uiw_ctx_slot(ui);
    g_last_item_rects[s] = r;
}

static ui_vec4_t uiw_last_item(ui_ctx_t *ui)
{
    int s = uiw_ctx_slot(ui);
    return g_last_item_rects[s];
}

ui_vec4_t ui_next_rect(ui_ctx_t *ui)
{
    ui_vec4_t r = ui_layout_next(&ui->layout, ui->style.spacing);
    uiw_set_last_item(ui, r);
    return r;
}

static uint32_t uiw_key_hash(uint32_t k)
{
    k ^= k >> 16;
    k *= 0x7feb352dU;
    k ^= k >> 15;
    k *= 0x846ca68bU;
    k ^= k >> 16;
    return k;
}

static int uiw_kv_get_i(uiw_kv_i_t *tab, int cap, uint32_t k, int defv)
{
    uint32_t h = uiw_key_hash(k);
    uint32_t i = h % (uint32_t)cap;
    for (int step = 0; step < cap; ++step)
    {
        uiw_kv_i_t *e = &tab[(int)i];
        if (!e->used)
            return defv;
        if (e->k == k)
            return e->v;
        i = (i + 1) % (uint32_t)cap;
    }
    return defv;
}

static void uiw_kv_set_i(uiw_kv_i_t *tab, int cap, uint32_t k, int v)
{
    uint32_t h = uiw_key_hash(k);
    uint32_t i = h % (uint32_t)cap;
    for (int step = 0; step < cap; ++step)
    {
        uiw_kv_i_t *e = &tab[(int)i];
        if (!e->used || e->k == k)
        {
            e->used = 1;
            e->k = k;
            e->v = v;
            return;
        }
        i = (i + 1) % (uint32_t)cap;
    }
    tab[0].used = 1;
    tab[0].k = k;
    tab[0].v = v;
}

static uiw_input_state_t *uiw_input_get(uint32_t id)
{
    for (int i = 0; i < 64; ++i)
        if (g_inputs[i].id == id)
            return &g_inputs[i];

    for (int i = 0; i < 64; ++i)
        if (g_inputs[i].id == 0)
        {
            g_inputs[i].id = id;
            g_inputs[i].cursor_byte = 0;
            g_inputs[i].sel0_byte = 0;
            g_inputs[i].sel1_byte = 0;
            return &g_inputs[i];
        }

    return &g_inputs[0];
}

static int uiw_strlen(const char *s)
{
    int n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static int uiw_min_i(int a, int b) { return a < b ? a : b; }
static int uiw_max_i(int a, int b) { return a > b ? a : b; }

static int uiw_insert_bytes(char *buf, int cap, int pos, const char *ins, int ins_len)
{
    int len = uiw_strlen(buf);
    if (pos < 0)
        pos = 0;
    if (pos > len)
        pos = len;

    if (ins_len <= 0)
        return 0;

    int room = cap - 1 - len;
    if (room <= 0)
        return 0;

    int n = ins_len;
    if (n > room)
        n = room;

    memmove(buf + pos + n, buf + pos, (size_t)(len - pos + 1));
    memcpy(buf + pos, ins, (size_t)n);
    return n;
}

static int uiw_erase_range(char *buf, int pos0, int pos1)
{
    int len = uiw_strlen(buf);
    if (pos0 < 0)
        pos0 = 0;
    if (pos1 < 0)
        pos1 = 0;
    if (pos0 > len)
        pos0 = len;
    if (pos1 > len)
        pos1 = len;
    if (pos1 < pos0)
    {
        int t = pos0;
        pos0 = pos1;
        pos1 = t;
    }
    if (pos1 <= pos0)
        return 0;
    memmove(buf + pos0, buf + pos1, (size_t)(len - pos1 + 1));
    return pos1 - pos0;
}

static int uiw_utf8_encode(uint32_t cp, char out[4])
{
    if (cp <= 0x7F)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FF)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF)
    {
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return 0;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF)
    {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

ui_widget_result_t ui_button_ex(ui_ctx_t *ui, uint32_t id, ui_vec4_t rect)
{
    ui_widget_result_t r;
    memset(&r, 0, sizeof(r));

    int hovered = ui_pt_in_rect(ui->mouse, rect) ? 1 : 0;
    r.hovered = (uint8_t)hovered;

    if (hovered)
        ui->hot_id = id;

    if (hovered && ui->mouse_pressed[0])
        ui->active_id = id;

    int held = (ui->active_id == id && ui->mouse_down[0]) ? 1 : 0;
    r.held = (uint8_t)held;

    if (ui->active_id == id)
    {
#if defined(__GNUC__) || defined(__clang__)
        (void)0;
#endif
    }

    if (ui->active_id == id && ui->mouse_released[0])
    {
        r.released = 1;
        if (hovered)
            r.pressed = 1;
        ui->active_id = 0;
    }

    uiw_set_last_item(ui, rect);
    return r;
}

int ui_button(ui_ctx_t *ui, const char *label, uint32_t font_id)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##btn");
    ui_widget_result_t st = ui_button_ex(ui, id, r);

    ui_color_t bg = ui->style.btn;
    if (ui->active_id == id && st.held)
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    if (label)
    {
        float th = ui_text_h(ui, font_id);
        float tw = (float)ui_text_w(ui, font_id, label);
        float tx = r.x + (r.z - tw) * 0.5f;
        float ty = r.y + (r.w - th) * 0.5f;
        ui_draw_text(ui, ui_v2(tx, ty), ui->style.text, font_id, label);
    }

    return st.pressed ? 1 : 0;
}

void ui_label(ui_ctx_t *ui, const char *text, uint32_t font_id)
{
    ui_vec4_t r = ui_next_rect(ui);
    float th = ui_text_h(ui, font_id);
    float ty = r.y + (r.w - th) * 0.5f;
    ui_draw_text(ui, ui_v2(r.x + ui->style.padding, ty), ui->style.text, font_id, text ? text : "");
}

int ui_checkbox(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##chk");
    ui_widget_result_t st = ui_button_ex(ui, id, r);

    int v = value ? (*value != 0) : 0;
    if (st.pressed && value)
    {
        v = !v;
        *value = v ? 1 : 0;
    }

    ui_color_t bg = ui->style.btn;
    if (ui->active_id == id && st.held)
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    float box = r.w - 6.0f;
    if (box < 12.0f)
        box = 12.0f;

    ui_vec4_t b = ui_v4(r.x + 3.0f, r.y + 3.0f, box, r.w - 6.0f);
    ui_draw_rect(ui, b, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.20f), ui->style.corner * 0.6f, 0.0f);
    uiw_draw_outline(ui, b, ui->style.corner * 0.6f);

    if (v)
    {
        ui_vec4_t fill = ui_v4(b.x + 3.0f, b.y + 3.0f, b.z - 6.0f, b.w - 6.0f);
        ui_draw_rect(ui, fill, ui->style.accent_dim.a > 0.001f ? ui->style.accent_dim : ui_color(ui->style.accent.rgb, 0.35f), ui->style.corner * 0.5f, 0.0f);
        ui_draw_rect(ui, fill, ui->style.accent, ui->style.corner * 0.5f, 1.0f);
    }

    if (label)
    {
        float th = ui_text_h(ui, font_id);
        float tx = b.x + b.z + ui->style.padding;
        float ty = r.y + (r.w - th) * 0.5f;
        ui_draw_text(ui, ui_v2(tx, ty), ui->style.text, font_id, label);
    }

    return st.pressed ? 1 : 0;
}

static float uiw_slider_do(ui_ctx_t *ui, uint32_t id, ui_vec4_t r, float t01)
{
    int hovered = ui_pt_in_rect(ui->mouse, r) ? 1 : 0;
    if (hovered)
        ui->hot_id = id;

    if (hovered && ui->mouse_pressed[0])
        ui->active_id = id;

    if (ui->active_id == id && !ui->mouse_down[0])
        ui->active_id = 0;

    if (ui->active_id == id && ui->mouse_down[0])
    {
        float x = ui->mouse.x;
        float u = (x - r.x) / (r.z > 1.0f ? r.z : 1.0f);
        t01 = ui_clampf(u, 0.0f, 1.0f);
    }

    ui_color_t bg = ui->style.btn;
    if (ui->active_id == id && ui->mouse_down[0])
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && hovered)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    float fillw = r.z * t01;
    if (fillw < 0.0f)
        fillw = 0.0f;
    if (fillw > r.z)
        fillw = r.z;

    ui_vec4_t fill = ui_v4(r.x, r.y, fillw, r.w);
    ui_draw_rect(ui, fill, ui->style.accent_dim.a > 0.001f ? ui->style.accent_dim : ui_color(ui->style.accent.rgb, 0.22f), ui->style.corner, 0.0f);

    float knobw = 10.0f;
    float kx = r.x + fillw - knobw * 0.5f;
    if (kx < r.x + 2.0f)
        kx = r.x + 2.0f;
    if (kx > r.x + r.z - knobw - 2.0f)
        kx = r.x + r.z - knobw - 2.0f;

    ui_vec4_t knob = ui_v4(kx, r.y + 2.0f, knobw, r.w - 4.0f);
    ui_draw_rect(ui, knob, ui->style.accent, ui->style.corner * 0.6f, 0.0f);
    uiw_draw_outline(ui, knob, ui->style.corner * 0.6f);

    uiw_set_last_item(ui, r);
    return t01;
}

int ui_slider_float(ui_ctx_t *ui, const char *label, uint32_t font_id, float *value, float minv, float maxv)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##sldf");

    float v = value ? *value : minv;
    float t01 = 0.0f;
    if (maxv != minv)
        t01 = (v - minv) / (maxv - minv);
    t01 = ui_clampf(t01, 0.0f, 1.0f);

    float old = t01;
    t01 = uiw_slider_do(ui, id, r, t01);

    float nv = minv + (maxv - minv) * t01;
    int changed = (t01 != old) ? 1 : 0;
    if (value)
        *value = nv;

    if (label)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s: %.3f", label, (double)nv);

        float th = ui_text_h(ui, font_id);
        float ty = r.y + (r.w - th) * 0.5f;
        ui_draw_text(ui, ui_v2(r.x + ui->style.padding, ty), ui->style.text, font_id, buf);
    }

    return changed;
}

int ui_slider_int(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value, int minv, int maxv)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##sldi");

    int v = value ? *value : minv;
    float t01 = 0.0f;
    if (maxv != minv)
        t01 = ((float)(v - minv)) / ((float)(maxv - minv));
    t01 = ui_clampf(t01, 0.0f, 1.0f);

    float old = t01;
    t01 = uiw_slider_do(ui, id, r, t01);

    int nv = minv;
    if (maxv != minv)
        nv = minv + (int)((float)(maxv - minv) * t01 + 0.5f);

    int changed = (nv != v) ? 1 : 0;
    if (value)
        *value = nv;

    if (label)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s: %d", label, nv);

        float th = ui_text_h(ui, font_id);
        float ty = r.y + (r.w - th) * 0.5f;
        ui_draw_text(ui, ui_v2(r.x + ui->style.padding, ty), ui->style.text, font_id, buf);
    }

    return changed;
}

int ui_toggle(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value)
{
    return ui_checkbox(ui, label, font_id, value);
}

int ui_radio(ui_ctx_t *ui, const char *label, uint32_t font_id, int *value, int my_value)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##radio");
    ui_widget_result_t st = ui_button_ex(ui, id, r);

    int sel = (value && *value == my_value) ? 1 : 0;
    if (st.pressed && value)
        *value = my_value;

    ui_color_t bg = ui->style.btn;
    if (sel)
        bg = ui->style.accent_dim.a > 0.001f ? ui->style.accent_dim : ui_color(ui->style.accent.rgb, 0.28f);
    else if (ui->active_id == id && st.held)
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    float dot = r.w - 10.0f;
    if (dot < 10.0f)
        dot = 10.0f;

    ui_vec4_t c = ui_v4(r.x + 5.0f, r.y + 5.0f, dot, r.w - 10.0f);
    ui_draw_rect(ui, c, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.18f), c.w * 0.5f, 0.0f);
    uiw_draw_outline(ui, c, c.w * 0.5f);

    if (sel)
    {
        ui_vec4_t d = ui_v4(c.x + c.z * 0.25f, c.y + c.w * 0.25f, c.z * 0.5f, c.w * 0.5f);
        ui_draw_rect(ui, d, ui->style.accent, d.w * 0.5f, 0.0f);
    }

    if (label)
    {
        float th = ui_text_h(ui, font_id);
        float tx = c.x + c.z + ui->style.padding;
        float ty = r.y + (r.w - th) * 0.5f;
        ui_draw_text(ui, ui_v2(tx, ty), ui->style.text, font_id, label);
    }

    return st.pressed ? 1 : 0;
}

void ui_progress_bar(ui_ctx_t *ui, float t01, const char *label, uint32_t font_id)
{
    ui_vec4_t r = ui_next_rect(ui);
    t01 = ui_clampf(t01, 0.0f, 1.0f);

    ui_draw_rect(ui, r, ui->style.btn, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    ui_vec4_t fill = ui_v4(r.x, r.y, r.z * t01, r.w);
    ui_draw_rect(ui, fill, ui->style.accent_dim.a > 0.001f ? ui->style.accent_dim : ui_color(ui->style.accent.rgb, 0.22f), ui->style.corner, 0.0f);

    if (label)
    {
        float th = ui_text_h(ui, font_id);
        float tw = (float)ui_text_w(ui, font_id, label);
        float tx = r.x + (r.z - tw) * 0.5f;
        float ty = r.y + (r.w - th) * 0.5f;
        ui_draw_text(ui, ui_v2(tx, ty), ui->style.text, font_id, label);
    }
}

void ui_separator(ui_ctx_t *ui)
{
    ui_vec4_t r = ui_next_rect(ui);
    float t = uiw_outline_th(ui);
    float y = r.y + r.w * 0.5f - t * 0.5f;
    ui_draw_rect(ui, ui_v4(r.x, y, r.z, t), ui->style.separator, 0.0f, 0.0f);
}

void ui_separator_text(ui_ctx_t *ui, const char *text, uint32_t font_id)
{
    ui_vec4_t r = ui_next_rect(ui);

    float th = ui_text_h(ui, font_id);
    float ty = r.y + (r.w - th) * 0.5f;

    float pad = ui->style.padding;
    float tx = r.x + pad;
    float tw = (float)ui_text_w(ui, font_id, text ? text : "");

    float t = uiw_outline_th(ui);
    float line_y = r.y + r.w * 0.5f - t * 0.5f;

    float left_x0 = r.x;
    float left_x1 = tx - pad * 0.5f;
    if (left_x1 < left_x0)
        left_x1 = left_x0;

    float right_x0 = tx + tw + pad * 0.5f;
    float right_x1 = r.x + r.z;
    if (right_x0 > right_x1)
        right_x0 = right_x1;

    ui_draw_rect(ui, ui_v4(left_x0, line_y, left_x1 - left_x0, t), ui->style.separator, 0.0f, 0.0f);
    ui_draw_rect(ui, ui_v4(right_x0, line_y, right_x1 - right_x0, t), ui->style.separator, 0.0f, 0.0f);

    ui_draw_text(ui, ui_v2(tx, ty), ui_color(ui->style.text.rgb, 0.88f), font_id, text ? text : "");
}

int ui_input_text(ui_ctx_t *ui, const char *label, uint32_t font_id, char *buf, int buf_cap)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##input");
    uiw_input_state_t *st = uiw_input_get(id);

    int inside = ui_pt_in_rect(ui->mouse, r);
    if (inside)
        ui->hot_id = id;

    if (inside && ui->mouse_pressed[0])
    {
        ui->active_id = id;
        int len = uiw_strlen(buf);
        st->cursor_byte = len;
        st->sel0_byte = len;
        st->sel1_byte = len;
    }

    ui_color_t bg = ui->style.btn;
    if (ui->active_id == id)
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && inside)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    if (!buf || buf_cap <= 0)
        return 0;
    buf[buf_cap - 1] = 0;

    int changed = 0;
    int len = uiw_strlen(buf);

    if (st->cursor_byte < 0)
        st->cursor_byte = 0;
    if (st->cursor_byte > len)
        st->cursor_byte = len;
    if (st->sel0_byte < 0)
        st->sel0_byte = 0;
    if (st->sel0_byte > len)
        st->sel0_byte = len;
    if (st->sel1_byte < 0)
        st->sel1_byte = 0;
    if (st->sel1_byte > len)
        st->sel1_byte = len;

    if (ui->active_id == id)
    {
        if (ui->key_pressed[UI_KEY_ESCAPE])
            ui->active_id = 0;

        if (ui->key_pressed[UI_KEY_LEFT])
        {
            int nc = st->cursor_byte > 0 ? st->cursor_byte - 1 : 0;
            st->cursor_byte = nc;
            st->sel0_byte = nc;
            st->sel1_byte = nc;
        }

        if (ui->key_pressed[UI_KEY_RIGHT])
        {
            int nc = st->cursor_byte < len ? st->cursor_byte + 1 : len;
            st->cursor_byte = nc;
            st->sel0_byte = nc;
            st->sel1_byte = nc;
        }

        if (ui->key_pressed[UI_KEY_BACKSPACE])
        {
            int a = uiw_min_i(st->sel0_byte, st->sel1_byte);
            int b = uiw_max_i(st->sel0_byte, st->sel1_byte);
            if (b > a)
            {
                uiw_erase_range(buf, a, b);
                st->cursor_byte = a;
                st->sel0_byte = a;
                st->sel1_byte = a;
                changed = 1;
            }
            else if (st->cursor_byte > 0)
            {
                uiw_erase_range(buf, st->cursor_byte - 1, st->cursor_byte);
                st->cursor_byte -= 1;
                st->sel0_byte = st->cursor_byte;
                st->sel1_byte = st->cursor_byte;
                changed = 1;
            }
        }

        if (ui->key_pressed[UI_KEY_DELETE])
        {
            int a = uiw_min_i(st->sel0_byte, st->sel1_byte);
            int b = uiw_max_i(st->sel0_byte, st->sel1_byte);
            if (b > a)
            {
                uiw_erase_range(buf, a, b);
                st->cursor_byte = a;
                st->sel0_byte = a;
                st->sel1_byte = a;
                changed = 1;
            }
            else if (st->cursor_byte < uiw_strlen(buf))
            {
                uiw_erase_range(buf, st->cursor_byte, st->cursor_byte + 1);
                changed = 1;
            }
        }

        len = uiw_strlen(buf);

        for (uint32_t i = 0; i < ui->char_count; ++i)
        {
            uint32_t cp = ui->char_buf[i];
            if (!cp)
                continue;
            if (cp == '\n' || cp == '\r')
                continue;

            int a = uiw_min_i(st->sel0_byte, st->sel1_byte);
            int b = uiw_max_i(st->sel0_byte, st->sel1_byte);
            if (b > a)
            {
                uiw_erase_range(buf, a, b);
                st->cursor_byte = a;
                st->sel0_byte = a;
                st->sel1_byte = a;
                len = uiw_strlen(buf);
            }

            char tmp[4];
            int n = uiw_utf8_encode(cp, tmp);
            if (n <= 0)
                continue;

            int ins = uiw_insert_bytes(buf, buf_cap, st->cursor_byte, tmp, n);
            if (ins > 0)
            {
                st->cursor_byte += ins;
                st->sel0_byte = st->cursor_byte;
                st->sel1_byte = st->cursor_byte;
                changed = 1;
                len = uiw_strlen(buf);
            }
        }

        if (st->cursor_byte > len)
            st->cursor_byte = len;
        if (st->sel0_byte > len)
            st->sel0_byte = len;
        if (st->sel1_byte > len)
            st->sel1_byte = len;
    }

    float pad = ui->style.padding;
    float th = ui_text_h(ui, font_id);
    float ty = r.y + (r.w - th) * 0.5f;

    ui_vec4_t clip = ui_v4(r.x + pad, r.y, r.z - pad * 2.0f, r.w);
    ui_push_clip(ui, clip);

    if (ui->active_id == id)
    {
        int a = uiw_min_i(st->sel0_byte, st->sel1_byte);
        int b = uiw_max_i(st->sel0_byte, st->sel1_byte);
        if (b > a)
        {
            char left[512];
            char mid[512];

            int n = uiw_strlen(buf);
            int na = a;
            if (na > n)
                na = n;
            int nb = b;
            if (nb > n)
                nb = n;

            int L = na;
            if (L > (int)sizeof(left) - 1)
                L = (int)sizeof(left) - 1;
            memcpy(left, buf, (size_t)L);
            left[L] = 0;

            int M = nb - na;
            if (M < 0)
                M = 0;
            if (M > (int)sizeof(mid) - 1)
                M = (int)sizeof(mid) - 1;
            memcpy(mid, buf + na, (size_t)M);
            mid[M] = 0;

            float x0 = r.x + pad + (float)ui_text_w(ui, font_id, left);
            float wsel = (float)ui_text_w(ui, font_id, mid);

            ui_color_t selcol = ui->style.accent_dim.a > 0.001f ? ui->style.accent_dim : ui_color(ui->style.accent.rgb, 0.35f);
            ui_draw_rect(ui, ui_v4(x0, r.y + 2.0f, wsel, r.w - 4.0f), selcol, ui->style.corner, 0.0f);
        }
    }

    ui_draw_text(ui, ui_v2(r.x + pad, ty), ui->style.text, font_id, buf);

    if (ui->active_id == id)
    {
        char left[512];
        int n = st->cursor_byte;
        int L = uiw_strlen(buf);
        if (n < 0)
            n = 0;
        if (n > L)
            n = L;
        if (n > (int)sizeof(left) - 1)
            n = (int)sizeof(left) - 1;
        memcpy(left, buf, (size_t)n);
        left[n] = 0;

        float cx = r.x + pad + (float)ui_text_w(ui, font_id, left);
        ui_draw_rect(ui, ui_v4(cx, r.y + 3.0f, 1.0f, r.w - 6.0f), ui->style.text, 0.0f, 0.0f);
    }

    ui_pop_clip(ui);

    if (label)
    {
        ui_vec2_t lp = ui_v2(r.x + r.z + ui->style.spacing, r.y + (r.w - ui_text_h(ui, font_id)) * 0.5f);
        ui_draw_text(ui, lp, ui->style.text, font_id, label);
    }

    return changed;
}

int ui_selectable(ui_ctx_t *ui, const char *label, uint32_t font_id, int *selected)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##sel");
    ui_widget_result_t st = ui_button_ex(ui, id, r);

    int sel = selected ? (*selected != 0) : 0;

    ui_color_t bg = ui->style.panel_bg.a > 0.001f ? ui->style.panel_bg : ui->style.btn;
    if (sel)
        bg = ui->style.accent_dim.a > 0.001f ? ui->style.accent_dim : ui_color(ui->style.accent.rgb, 0.35f);
    else if (ui->active_id == id && st.held)
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    if (label)
    {
        float th = ui_text_h(ui, font_id);
        ui_draw_text(ui, ui_v2(r.x + ui->style.padding, r.y + (r.w - th) * 0.5f), ui->style.text, font_id, label);
    }

    if (st.pressed && selected)
        *selected = !*selected;

    return st.pressed ? 1 : 0;
}

static uiw_popup_state_t *uiw_popup(ui_ctx_t *ui)
{
    for (int i = 0; i < 8; ++i)
        if (g_popups[i].ui == ui)
            return &g_popups[i];

    for (int i = 0; i < 8; ++i)
        if (!g_popups[i].ui)
        {
            memset(&g_popups[i], 0, sizeof(g_popups[i]));
            g_popups[i].ui = ui;
            return &g_popups[i];
        }

    return &g_popups[0];
}

static void uiw_close_popup(ui_ctx_t *ui)
{
    uiw_popup_state_t *p = uiw_popup(ui);
    p->open = 0;
    p->active_id = 0;
}

void ui_open_popup(ui_ctx_t *ui, const char *id)
{
    uiw_popup_state_t *p = uiw_popup(ui);
    p->open_id = ui_id_str(ui, id ? id : "##popup");
    p->active_id = p->open_id;
    p->open = 1;
}

int ui_begin_popup(ui_ctx_t *ui, const char *id)
{
    uiw_popup_state_t *p = uiw_popup(ui);
    uint32_t pid = ui_id_str(ui, id ? id : "##popup");
    if (!p->open || p->active_id != pid)
        return 0;

    ui_vec4_t anchor = uiw_last_item(ui);

    float w = ui_maxf(160.0f, anchor.z);
    float h = ui->style.line_h * 8.0f + ui->style.padding * 2.0f;

    ui_vec4_t r = ui_v4(anchor.x, anchor.y + anchor.w + 2.0f, w, h);

    float sw = (float)ui->fb_size.x;
    float sh = (float)ui->fb_size.y;

    if (r.x + r.z > sw)
        r.x = sw - r.z;
    if (r.x < 0.0f)
        r.x = 0.0f;

    if (r.y + r.w > sh)
        r.y = anchor.y - r.w - 2.0f;
    if (r.y < 0.0f)
        r.y = 0.0f;

    p->rect = r;

    if (ui->mouse_pressed[0] && !ui_pt_in_rect(ui->mouse, r))
    {
        uiw_close_popup(ui);
        return 0;
    }

    if (ui->key_pressed[UI_KEY_ESCAPE])
    {
        uiw_close_popup(ui);
        return 0;
    }

    ui_draw_rect(ui, r, ui->style.window_bg.a > 0.001f ? ui->style.window_bg : ui->style.panel_bg, ui->style.window_corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.window_corner);

    ui_vec4_t cr = ui_v4(r.x + ui->style.padding, r.y + ui->style.padding, r.z - ui->style.padding * 2.0f, r.w - ui->style.padding * 2.0f);

    ui_push_clip(ui, cr);
    ui_layout_begin(&ui->layout, cr, 0.0f);
    ui_layout_row(&ui->layout, ui->style.line_h, 1, 0, ui->style.spacing);

    return 1;
}

void ui_end_popup(ui_ctx_t *ui)
{
    ui_pop_clip(ui);
}

int ui_combo(ui_ctx_t *ui, const char *label, uint32_t font_id, const char **items, int items_count, int *current)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t id = ui_id_str(ui, label ? label : "##combo");
    ui_widget_result_t st = ui_button_ex(ui, id, r);

    ui_color_t bg = ui->style.btn;
    if (ui->active_id == id && st.held)
        bg = ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        bg = ui->style.btn_hover;

    ui_draw_rect(ui, r, bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    const char *show = "";
    if (items && current && *current >= 0 && *current < items_count)
        show = items[*current] ? items[*current] : "";

    float th = ui_text_h(ui, font_id);
    ui_draw_text(ui, ui_v2(r.x + ui->style.padding, r.y + (r.w - th) * 0.5f), ui->style.text, font_id, show);

    if (st.pressed)
        ui_open_popup(ui, label ? label : "##combo_popup");

    int changed = 0;

    if (ui_begin_popup(ui, label ? label : "##combo_popup"))
    {
        for (int i = 0; i < items_count; ++i)
        {
            int sel = (current && *current == i) ? 1 : 0;
            int tmp = sel;
            if (ui_selectable(ui, items && items[i] ? items[i] : "##item", font_id, &tmp))
            {
                if (current && *current != i)
                {
                    *current = i;
                    changed = 1;
                }
                uiw_close_popup(ui);
                break;
            }
        }
        ui_end_popup(ui);
    }

    if (label)
    {
        ui_vec2_t lp = ui_v2(r.x + r.z + ui->style.spacing, r.y + (r.w - ui_text_h(ui, font_id)) * 0.5f);
        ui_draw_text(ui, lp, ui->style.text, font_id, label);
    }

    return changed;
}

int ui_collapsing_header(ui_ctx_t *ui, const char *label, uint32_t font_id, int default_open)
{
    ui_vec4_t r = ui_next_rect(ui);
    if (r.w < ui->style.line_h)
        r.w = ui->style.line_h;

    uint32_t id = ui_id_str(ui, label ? label : "##hdr");
    int open = uiw_kv_get_i(g_open_headers, 256, id, default_open ? 1 : 0);

    ui_widget_result_t st = ui_button_ex(ui, id, r);
    if (st.pressed)
    {
        open = !open;
        uiw_kv_set_i(g_open_headers, 256, id, open);
    }

    ui_color_t bg = ui->style.header_bg.a > 0.001f ? ui->style.header_bg : ui->style.btn;
    if (ui->active_id == id && st.held)
        bg = ui->style.header_bg_active.a > 0.001f ? ui->style.header_bg_active : ui->style.btn_active;
    else if (ui->hot_id == id && st.hovered)
        bg = ui->style.btn_hover;

    ui_push_clip(ui, r);

    ui_draw_rect(ui, r, bg, ui->style.header_corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.header_corner);

    float th = ui_text_h(ui, font_id);
    float tx = r.x + ui->style.padding;
    float ty = r.y + (r.w - th) * 0.5f;

    ui_draw_text(ui, ui_v2(tx, ty),
                 ui->style.header_text.a > 0.001f ? ui->style.header_text : ui->style.text,
                 font_id,
                 label ? label : "");

    ui_pop_clip(ui);

    return open;
}

static uiw_child_state_t *uiw_child_push(uint32_t id)
{
    if (g_child_top >= 32)
        return &g_child_stack[31];
    uiw_child_state_t *cs = &g_child_stack[g_child_top++];
    memset(cs, 0, sizeof(*cs));
    cs->id = id;
    return cs;
}

static uiw_child_state_t *uiw_child_top_state(void)
{
    if (g_child_top <= 0)
        return 0;
    return &g_child_stack[g_child_top - 1];
}

static void uiw_child_pop(void)
{
    if (g_child_top > 0)
        g_child_top--;
}

static void uiw_scrollbar_metrics(float view_h, float content_h, float scroll_y, float track_h, float *out_thumb_h, float *out_thumb_y)
{
    float min_thumb = 24.0f;
    float t = view_h / content_h;
    if (t > 1.0f)
        t = 1.0f;
    float th = track_h * t;
    if (th < min_thumb)
        th = min_thumb;
    if (th > track_h)
        th = track_h;

    float max_scroll = content_h - view_h;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;

    float u = max_scroll > 0.0f ? (scroll_y / max_scroll) : 0.0f;
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;

    float ty = (track_h - th) * u;

    if (out_thumb_h)
        *out_thumb_h = th;
    if (out_thumb_y)
        *out_thumb_y = ty;
}

static void uiw_child_scrollbar(ui_ctx_t *ui, uiw_child_state_t *cs, ui_vec4_t cr)
{
    float view_h = cr.w;
    float content_h = cs->content_h_last;

    if (content_h <= view_h + 0.5f)
    {
        cs->scroll_y = 0.0f;
        cs->scroll_drag = 0;
        cs->has_scroll = 0;
        return;
    }

    cs->has_scroll = 1;

    float max_scroll = content_h - view_h;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (cs->scroll_y < 0.0f)
        cs->scroll_y = 0.0f;
    if (cs->scroll_y > max_scroll)
        cs->scroll_y = max_scroll;

    float w = ui->style.scroll_w;
    ui_vec4_t sb = ui_v4(cr.x + cr.z - w, cr.y, w, cr.w);

    float th = 0.0f;
    float ty = 0.0f;
    uiw_scrollbar_metrics(view_h, content_h, cs->scroll_y, sb.w, &th, &ty);

    float pad = ui->style.scroll_pad;
    ui_vec4_t thumb = ui_v4(sb.x + pad, sb.y + ty, sb.z - pad * 2.0f, th);

    ui_vec2_t m = ui->mouse;

    int over_thumb = ui_pt_in_rect(m, thumb);
    int pressed = ui->mouse_pressed[0] ? 1 : 0;
    int down = ui->mouse_down[0] ? 1 : 0;

    if (pressed && over_thumb)
    {
        cs->scroll_drag = 1;
        cs->scroll_drag_y0 = m.y;
        cs->scroll_drag_scroll0 = cs->scroll_y;
    }

    if (!down)
        cs->scroll_drag = 0;

    if (cs->scroll_drag && down)
    {
        float dy = m.y - cs->scroll_drag_y0;
        float track = sb.w - th;
        float u = track > 0.0f ? (dy / track) : 0.0f;
        cs->scroll_y = cs->scroll_drag_scroll0 + u * max_scroll;
        if (cs->scroll_y < 0.0f)
            cs->scroll_y = 0.0f;
        if (cs->scroll_y > max_scroll)
            cs->scroll_y = max_scroll;
    }

    ui_color_t track_col = ui->style.scroll_track;
    ui_color_t thumb_col = (over_thumb || cs->scroll_drag) ? ui->style.scroll_thumb_hover : ui->style.scroll_thumb;

    ui_draw_rect(ui, sb, track_col, 3.0f, 0.0f);
    ui_draw_rect(ui, thumb, thumb_col, 3.0f, 0.0f);
}

int ui_begin_child(ui_ctx_t *ui, const char *id, ui_vec2_t size)
{
    ui_vec4_t r = ui_next_rect(ui);
    uint32_t cid = ui_id_str(ui, id ? id : "##child");

    if (size.x > 1.0f)
        r.z = size.x;
    if (size.y > 1.0f)
        r.w = size.y;

    if (r.z < 1.0f)
        r.z = 1.0f;
    if (r.w < 1.0f)
        r.w = 1.0f;

    uiw_child_state_t *cs = uiw_child_push(cid);
    cs->id = cid;
    cs->rect = r;

    cs->saved_layout = ui->layout;
    cs->saved_layout_scroll_y = ui->layout.scroll_y;

    ui_draw_rect(ui, r, ui->style.panel_bg, ui->style.corner, 0.0f);
    uiw_draw_outline(ui, r, ui->style.corner);

    ui_vec4_t cr = ui_v4(
        r.x + ui->style.padding,
        r.y + ui->style.padding,
        r.z - ui->style.padding * 2.0f,
        r.w - ui->style.padding * 2.0f);

    if (cr.z < 1.0f)
        cr.z = 1.0f;
    if (cr.w < 1.0f)
        cr.w = 1.0f;

    cs->content_rect = cr;

    float max_scroll = cs->content_h_last - cr.w;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (cs->scroll_y < 0.0f)
        cs->scroll_y = 0.0f;
    if (cs->scroll_y > max_scroll)
        cs->scroll_y = max_scroll;

    ui_push_clip(ui, cr);

    float scroll_bar_w = cs->has_scroll ? ui->style.scroll_w : 0.0f;
    ui_vec4_t body = ui_v4(cr.x, cr.y, cr.z - scroll_bar_w, cr.w);
    if (body.z < 1.0f)
        body.z = 1.0f;

    ui->layout.scroll_y = cs->scroll_y;
    ui_layout_begin(&ui->layout, body, 0.0f);
    ui_layout_row(&ui->layout, ui->style.line_h, 1, 0, ui->style.spacing);

    return 1;
}

void ui_end_child(ui_ctx_t *ui)
{
    uiw_child_state_t *cs = uiw_child_top_state();
    if (!cs)
        return;

    float saved = ui->layout.scroll_y;
    ui->layout.scroll_y = 0.0f;
    float ch = ui_layout_content_h(&ui->layout);
    ui->layout.scroll_y = saved;

    if (ch < 0.0f)
        ch = 0.0f;
    cs->content_h_last = ch;

    float max_scroll = cs->content_h_last - cs->content_rect.w;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (cs->scroll_y < 0.0f)
        cs->scroll_y = 0.0f;
    if (cs->scroll_y > max_scroll)
        cs->scroll_y = max_scroll;

    uiw_child_scrollbar(ui, cs, cs->content_rect);

    ui_pop_clip(ui);

    ui->layout = cs->saved_layout;
    ui->layout.scroll_y = cs->saved_layout_scroll_y;

    uiw_child_pop();
}

int ui_begin_context_menu(ui_ctx_t *ui, const char *id)
{
    ui_vec4_t a = uiw_last_item(ui);
    if (ui->mouse_pressed[1] && ui_pt_in_rect(ui->mouse, a))
        ui_open_popup(ui, id ? id : "##ctx");
    return ui_begin_popup(ui, id ? id : "##ctx");
}
