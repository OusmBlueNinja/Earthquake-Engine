#include "ui.h"
#include "ui_hash.h"
#include "ui_window.h"
#include <string.h>

static int ui_default_text_width(void *user, uint32_t font_id, const char *text, int len)
{
    (void)user;
    (void)font_id;
    if (!text)
        return 0;
    if (len < 0)
    {
        int n = 0;
        while (text[n])
            n++;
        len = n;
    }
    return len * 8;
}

static float ui_default_text_height(void *user, uint32_t font_id)
{
    (void)user;
    (void)font_id;
    return 16.0f;
}

static uint32_t ui_id_base(const ui_ctx_t *ui)
{
    return ui->id_top > 0 ? ui->id_stack[ui->id_top - 1] : ui->id_seed;
}

static uint32_t ui_make_id(ui_ctx_t *ui, uint32_t x)
{
    return ui_hash_combine(ui_id_base(ui), x);
}

static ui_vec4_t ui_rect_intersect(ui_vec4_t a, ui_vec4_t b)
{
    float ax2 = a.x + a.z;
    float ay2 = a.y + a.w;
    float bx2 = b.x + b.z;
    float by2 = b.y + b.w;
    float x1 = ui_maxf(a.x, b.x);
    float y1 = ui_maxf(a.y, b.y);
    float x2 = ui_minf(ax2, bx2);
    float y2 = ui_minf(ay2, by2);
    return ui_v4(x1, y1, ui_maxf(0.0f, x2 - x1), ui_maxf(0.0f, y2 - y1));
}

static int ui_is_inside_clip(const ui_ctx_t *ui, ui_vec4_t r)
{
    if (ui->clip_top <= 0)
        return 1;
    ui_vec4_t c = ui->clip_stack[ui->clip_top - 1];
    float rx2 = r.x + r.z;
    float ry2 = r.y + r.w;
    float cx2 = c.x + c.z;
    float cy2 = c.y + c.w;
    if (rx2 <= c.x)
        return 0;
    if (ry2 <= c.y)
        return 0;
    if (r.x >= cx2)
        return 0;
    if (r.y >= cy2)
        return 0;
    return 1;
}

void ui_init(ui_ctx_t *ui, ui_realloc_fn rfn, void *ruser)
{
    memset(ui, 0, sizeof(*ui));
    ui->rfn = rfn;
    ui->ruser = ruser;

    ui_cmd_stream_init(&ui->stream, rfn, ruser);

    ui->text_width = ui_default_text_width;
    ui->text_height = ui_default_text_height;
    ui->text_user = 0;

    ui->style.bg = ui_color(ui_v3(0.08f, 0.08f, 0.09f), 1.0f);
    ui->style.panel_bg = ui_color(ui_v3(0.12f, 0.12f, 0.13f), 1.0f);
    ui->style.text = ui_color(ui_v3(0.95f, 0.95f, 0.95f), 1.0f);

    ui->style.btn = ui_color(ui_v3(0.18f, 0.18f, 0.20f), 1.0f);
    ui->style.btn_hover = ui_color(ui_v3(0.24f, 0.24f, 0.27f), 1.0f);
    ui->style.btn_active = ui_color(ui_v3(0.30f, 0.30f, 0.35f), 1.0f);

    ui->style.outline = ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.35f);

    ui->style.padding = 8.0f;
    ui->style.spacing = 6.0f;
    ui->style.line_h = 22.0f;
    ui->style.corner = 4.0f;

    ui->id_seed = 0xC001D00Du;
    ui->backend = 0;
    ui->set_cursor_state_fn = 0;
    ui->set_cursor_state_user = 0;
    ui->set_cursor_shape_fn = 0;
    ui->set_cursor_shape_user = 0;
    ui->cursor_state_req_set = 0;
    ui->cursor_state_req = 0;
    ui->cursor_state_prio = -2147483647;
    ui->cursor_shape_req_set = 0;
    ui->cursor_shape_req = 0;
    ui->cursor_shape_prio = -2147483647;

    ui->delta_time = 1.0f / 60.0f;
    ui->prev_mouse = ui_v2(0.0f, 0.0f);
    ui->scroll_delta = ui_v2(0.0f, 0.0f);
    ui->scroll_used = 0;
    ui->text_input_active = 0;
    ui->window_accept_input = 1;

    ui->clip_top = 0;
    ui->id_top = 0;

    ui->cur_window_id = 0;
    ui->cur_window_rect = ui_v4(0, 0, 0, 0);

    ui->next_win_has_pos = 0;
    ui->next_win_has_size = 0;
    ui->next_win_pos = ui_v2(0, 0);
    ui->next_win_size = ui_v2(0, 0);

    ui->win_drag_id = 0;
    ui->win_resize_mask = 0;
    ui->win_drag_mode = 0;
    ui->win_drag_start_mouse = ui_v2(0, 0);
    ui->win_drag_start_rect = ui_v4(0, 0, 0, 0);

    ui->char_count = 0;

    ui->next_item_spacing = -1.0f;
}

void ui_shutdown(ui_ctx_t *ui)
{
    ui_cmd_stream_free(&ui->stream);
    ui->backend = 0;
}

void ui_set_cursor_state_callback(ui_ctx_t *ui, void (*fn)(void *user, int state), void *user)
{
    if (!ui)
        return;
    ui->set_cursor_state_fn = fn;
    ui->set_cursor_state_user = user;
}

void ui_set_cursor_state(ui_ctx_t *ui, int state)
{
    ui_request_cursor_state(ui, state, 0);
}

void ui_request_cursor_state(ui_ctx_t *ui, int state, int priority)
{
    if (!ui)
        return;
    if (!ui->cursor_state_req_set || priority >= ui->cursor_state_prio)
    {
        ui->cursor_state_req_set = 1;
        ui->cursor_state_req = state;
        ui->cursor_state_prio = priority;
    }
}

void ui_set_cursor_shape_callback(ui_ctx_t *ui, void (*fn)(void *user, int shape), void *user)
{
    if (!ui)
        return;
    ui->set_cursor_shape_fn = fn;
    ui->set_cursor_shape_user = user;
}

void ui_set_cursor_shape(ui_ctx_t *ui, int shape)
{
    ui_request_cursor_shape(ui, shape, 0);
}

void ui_request_cursor_shape(ui_ctx_t *ui, int shape, int priority)
{
    if (!ui)
        return;
    if (!ui->cursor_shape_req_set || priority >= ui->cursor_shape_prio)
    {
        ui->cursor_shape_req_set = 1;
        ui->cursor_shape_req = shape;
        ui->cursor_shape_prio = priority;
    }
}

void ui_input_mouse_pos(ui_ctx_t *ui, ui_vec2_t pos)
{
    ui->mouse = pos;
}

void ui_input_mouse_btn(ui_ctx_t *ui, int button, int down)
{
    if (button < 0 || button >= 8)
        return;
    ui->mouse_down[button] = down ? 1 : 0;
}

void ui_set_delta_time(ui_ctx_t *ui, float dt)
{
    if (!ui)
        return;
    if (dt < 0.0f)
        dt = 0.0f;
    ui->delta_time = dt;
}

void ui_input_key(ui_ctx_t *ui, uint32_t key, int down, uint8_t repeat, uint8_t mods)
{
    (void)repeat;
    (void)mods;
    if (!ui)
        return;
    if (key >= 512)
        return;
    ui->key_down[key] = down ? 1 : 0;
    if (repeat && down)
        ui->key_pressed_accum[key] = 1;
}

void ui_input_char(ui_ctx_t *ui, uint32_t codepoint)
{
    if (!ui)
        return;
    if (ui->char_count >= 32)
        return;
    ui->char_buf[ui->char_count++] = codepoint;
}

void ui_on_event(ui_ctx_t *ui, const ui_event_t *e)
{
    if (!ui || !e)
        return;

    if (e->type == UI_EV_MOUSE_MOVE)
        ui_input_mouse_pos(ui, e->mouse_pos);
    else if (e->type == UI_EV_MOUSE_BUTTON_DOWN)
        ui_input_mouse_btn(ui, (int)e->button, 1);
    else if (e->type == UI_EV_MOUSE_BUTTON_UP)
        ui_input_mouse_btn(ui, (int)e->button, 0);
    else if (e->type == UI_EV_MOUSE_SCROLL)
    {
        ui->scroll_delta.x += e->scroll.x;
        ui->scroll_delta.y += e->scroll.y;
    }
    else if (e->type == UI_EV_KEY_DOWN)
        ui_input_key(ui, e->key, 1, e->repeat, e->mods);
    else if (e->type == UI_EV_KEY_UP)
        ui_input_key(ui, e->key, 0, 0, e->mods);
    else if (e->type == UI_EV_CHAR)
        ui_input_char(ui, e->codepoint);
}

void ui_begin(ui_ctx_t *ui, ui_vec2i_t fb_size)
{
    ui->fb_size = fb_size;
    ui->scroll_used = 0;
    ui->text_input_active = 0;
    ui->window_accept_input = 1;

    if (ui->prev_mouse.x == 0.0f && ui->prev_mouse.y == 0.0f)
        ui->prev_mouse = ui->mouse;

    for (int i = 0; i < 8; ++i)
    {
        ui->mouse_pressed[i] = (ui->mouse_down[i] && !ui->mouse_prev[i]) ? 1 : 0;
        ui->mouse_released[i] = (!ui->mouse_down[i] && ui->mouse_prev[i]) ? 1 : 0;
        ui->mouse_prev[i] = ui->mouse_down[i];
    }

    float dt = ui->delta_time;
    ui->io.mouse_pos = ui->mouse;
    ui->io.mouse_delta = ui_v2(ui->mouse.x - ui->prev_mouse.x, ui->mouse.y - ui->prev_mouse.y);
    ui->prev_mouse = ui->mouse;

    for (int i = 0; i < 8; ++i)
    {
        ui->io.mouse_down[i] = ui->mouse_down[i];
        if (ui->mouse_down[i])
            ui->io.mouse_down_duration[i] += dt;
        else
            ui->io.mouse_down_duration[i] = 0.0f;
    }

    for (uint32_t k = 0; k < 512; ++k)
    {
        ui->key_pressed[k] = (ui->key_down[k] && !ui->key_prev[k]) ? 1 : 0;
        ui->key_released[k] = (!ui->key_down[k] && ui->key_prev[k]) ? 1 : 0;
        ui->key_prev[k] = ui->key_down[k];

        if (ui->key_pressed_accum[k])
        {
            ui->key_pressed[k] = 1;
            ui->key_pressed_accum[k] = 0;
        }

        ui->io.key_down[k] = ui->key_down[k];
        if (ui->key_down[k])
            ui->io.key_down_duration[k] += dt;
        else
            ui->io.key_down_duration[k] = 0.0f;
    }

    ui->io.mouse_scroll = ui->scroll_delta;
    ui->scroll_delta = ui_v2(0.0f, 0.0f);

    ui->hot_id = 0;
    ui->id_top = 0;

    ui_cmd_stream_reset(&ui->stream);

    ui_window_begin_frame(ui);

    // Reset cursor requests for this frame (callers will set what they need).
    ui->cursor_state_req_set = 0;
    ui->cursor_state_prio = -2147483647;
    ui->cursor_shape_req_set = 0;
    ui->cursor_shape_prio = -2147483647;

    if ((ui->active_id != 0) || (ui_window_hovered_id(ui) != 0))
    {
        ui_request_cursor_state(ui, UI_CURSOR_NORMAL, 10);
        ui_request_cursor_shape(ui, UI_CURSOR_ARROW, 1);
    }

    ui->clip_top = 0;
    ui_vec4_t full = ui_v4(0.0f, 0.0f, (float)fb_size.x, (float)fb_size.y);
    ui->clip_stack[ui->clip_top++] = full;
    ui_cmd_push_clip(&ui->stream, full);

    ui_layout_begin(&ui->layout, full, 0.0f);
    ui_layout_row(&ui->layout, ui->style.line_h, 1, 0, ui->style.spacing);

    ui->next_item_w = 0.0f;
    ui->next_item_h = 0.0f;
    ui->next_item_spacing = -1.0f;
    ui->next_same_line = 0;
}

void ui_end(ui_ctx_t *ui)
{
    ui_window_end_frame(ui);

    // Apply cursor requests once per frame to avoid oscillation.
    if (ui->set_cursor_state_fn && ui->cursor_state_req_set)
        ui->set_cursor_state_fn(ui->set_cursor_state_user, ui->cursor_state_req);
    if (ui->set_cursor_shape_fn && ui->cursor_shape_req_set)
        ui->set_cursor_shape_fn(ui->set_cursor_shape_user, ui->cursor_shape_req);

    ui->io.want_capture_mouse = (ui->active_id != 0) || (ui_window_hovered_id(ui) != 0);
    ui->io.want_capture_keyboard = (ui->active_id != 0);
    ui->io.want_text_input = ui->text_input_active ? 1u : 0u;

    ui->char_count = 0;
}

const ui_cmd_t *ui_commands(const ui_ctx_t *ui, uint32_t *out_count)
{
    if (out_count)
        *out_count = ui_cmd_count(&ui->stream);
    return ui_cmd_data(&ui->stream);
}

const ui_io_t *ui_io(const ui_ctx_t *ui)
{
    return ui ? &ui->io : 0;
}

void ui_push_clip(ui_ctx_t *ui, ui_vec4_t rect)
{
    if (ui->clip_top >= 16)
        return;
    ui_vec4_t cur = ui->clip_stack[ui->clip_top - 1];
    ui_vec4_t clipped = ui_rect_intersect(cur, rect);
    ui->clip_stack[ui->clip_top++] = clipped;
    ui_cmd_push_clip(&ui->stream, clipped);
}

void ui_pop_clip(ui_ctx_t *ui)
{
    if (ui->clip_top <= 1)
        return;
    ui->clip_top--;
    ui_cmd_push_clip(&ui->stream, ui->clip_stack[ui->clip_top - 1]);
}

void ui_push_id_str(ui_ctx_t *ui, const char *s)
{
    if (ui->id_top >= 32)
        return;
    ui->id_stack[ui->id_top++] = ui_make_id(ui, ui_hash_str(s));
}

void ui_push_id_ptr(ui_ctx_t *ui, const void *p)
{
    if (ui->id_top >= 32)
        return;
    ui->id_stack[ui->id_top++] = ui_make_id(ui, ui_hash_ptr(p));
}

void ui_push_id_u32(ui_ctx_t *ui, uint32_t v)
{
    if (ui->id_top >= 32)
        return;
    ui->id_stack[ui->id_top++] = ui_make_id(ui, v);
}

void ui_pop_id(ui_ctx_t *ui)
{
    if (ui->id_top > 0)
        ui->id_top--;
}

uint32_t ui_id_str(ui_ctx_t *ui, const char *s) { return ui_make_id(ui, ui_hash_str(s)); }
uint32_t ui_id_ptr(ui_ctx_t *ui, const void *p) { return ui_make_id(ui, ui_hash_ptr(p)); }
uint32_t ui_id_u32(ui_ctx_t *ui, uint32_t v) { return ui_make_id(ui, v); }

void ui_set_next_item_width(ui_ctx_t *ui, float width)
{
    if (!ui)
        return;
    ui->next_item_w = width;
}

void ui_set_next_item_height(ui_ctx_t *ui, float height)
{
    if (!ui)
        return;
    ui->next_item_h = height;
}

void ui_set_next_item_spacing(ui_ctx_t *ui, float spacing)
{
    if (!ui)
        return;
    ui->next_item_spacing = spacing;
}

void ui_same_line(ui_ctx_t *ui, float spacing)
{
    if (!ui)
        return;
    ui->next_same_line = 1;
    if (spacing >= 0.0f)
        ui->next_item_spacing = spacing;
}

void ui_new_line(ui_ctx_t *ui)
{
    if (!ui)
        return;
    float spacing = (ui->next_item_spacing >= 0.0f) ? ui->next_item_spacing : ui->style.spacing;
    ui_layout_new_line(&ui->layout, spacing);
    ui->next_item_w = 0.0f;
    ui->next_item_h = 0.0f;
    ui->next_item_spacing = -1.0f;
    ui->next_same_line = 0;
}

void ui_row(ui_ctx_t *ui, float height, int cols, const float *widths)
{
    ui_layout_row(&ui->layout, height, cols, widths, ui->style.spacing);
}

void ui_draw_rect(ui_ctx_t *ui, ui_vec4_t rect, ui_color_t color, float radius, float thickness)
{
    if (!ui_is_inside_clip(ui, rect))
        return;
    ui_cmd_push_rect(&ui->stream, rect, color, radius, thickness);
}

void ui_draw_text(ui_ctx_t *ui, ui_vec2_t pos, ui_color_t color, uint32_t font_id, const char *text)
{
    if (!text)
        return;
    ui_cmd_push_text(&ui->stream, pos, color, font_id, text);
}

void ui_draw_icon(ui_ctx_t *ui, ui_vec4_t rect, ui_color_t color, uint32_t font_id, uint32_t icon_id)
{
    if (!ui_is_inside_clip(ui, rect))
        return;
    ui_cmd_push_icon(&ui->stream, rect, color, font_id, icon_id);
}

void ui_draw_image(ui_ctx_t *ui, ui_vec4_t rect, uint32_t gl_tex, ui_vec4_t uv, ui_color_t tint)
{
    if (!ui_is_inside_clip(ui, rect))
        return;
    ui_cmd_push_image(&ui->stream, rect, gl_tex, uv, tint);
}

void ui_begin_panel(ui_ctx_t *ui, ui_vec4_t rect)
{
    ui_draw_rect(ui, rect, ui->style.panel_bg, ui->style.corner, 0.0f);
    ui_push_clip(ui, rect);
    ui_layout_begin(&ui->layout, rect, ui->style.padding);
    ui_layout_row(&ui->layout, ui->style.line_h, 1, 0, ui->style.spacing);
}

void ui_end_panel(ui_ctx_t *ui)
{
    ui_pop_clip(ui);
}

void ui_attach_backend(ui_ctx_t *ui, ui_base_backend_t *backend)
{
    ui->backend = backend;

    if (backend && backend->text_width && backend->text_height)
    {
        ui->text_width = backend->text_width;
        ui->text_height = backend->text_height;
        ui->text_user = backend->user;
    }
    else
    {
        ui->text_width = ui_default_text_width;
        ui->text_height = ui_default_text_height;
        ui->text_user = 0;
    }
}

void ui_render(ui_ctx_t *ui)
{
    if (!ui || !ui->backend)
        return;
    if (!ui->backend->begin || !ui->backend->render || !ui->backend->end)
        return;

    uint32_t n = 0;
    const ui_cmd_t *cmds = ui_commands(ui, &n);

    ui->backend->begin(ui->backend, ui->fb_size.x, ui->fb_size.y);
    ui->backend->render(ui->backend, cmds, n);
    ui->backend->end(ui->backend);
}
