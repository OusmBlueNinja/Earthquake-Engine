#include "ui_window.h"
#include "ui.h"
#include "ui_hash.h"
#include <string.h>
#include <stdlib.h>

#ifndef UI_Y_DOWN
#define UI_Y_DOWN 1
#endif

typedef enum ui_dock_slot_t
{
    UI_DOCKSLOT_NONE = 0,
    UI_DOCKSLOT_LEFT,
    UI_DOCKSLOT_RIGHT,
    UI_DOCKSLOT_TOP,
    UI_DOCKSLOT_BOTTOM,
    UI_DOCKSLOT_CENTER
} ui_dock_slot_t;

typedef enum ui_dock_split_t
{
    UI_DOCKSPLIT_NONE = 0,
    UI_DOCKSPLIT_VERT,
    UI_DOCKSPLIT_HORZ
} ui_dock_split_t;

typedef struct ui_win_t
{
    uint8_t used;
    uint32_t id;

    ui_vec4_t rect;
    ui_vec4_t float_rect;

    ui_window_flags_t flags;
    int z;

    uint32_t dock_ds_id;
    uint16_t dock_leaf;

    uint32_t cmd_start;
    uint32_t cmd_end;

    float scroll_y;
    float content_h_last;
    uint8_t has_scroll;

    uint8_t scroll_drag;
    float scroll_drag_y0;
    float scroll_drag_scroll0;
} ui_win_t;

typedef struct ui_win_batch_t
{
    uint32_t id;
    int z;
    uint32_t start;
    uint32_t end;
} ui_win_batch_t;

typedef struct ui_dock_node_t
{
    uint8_t used;
    uint8_t split;
    float ratio;
    uint16_t a;
    uint16_t b;
    uint32_t win_id;
} ui_dock_node_t;

typedef struct ui_dockspace_t
{
    uint8_t used;
    uint8_t seen;
    uint32_t id;
    ui_vec4_t rect;
    uint16_t root;
} ui_dockspace_t;

typedef struct ui_win_ctx_t
{
    ui_ctx_t *ui;

    ui_win_t wins[64];
    uint32_t win_count;

    uint8_t next_has_pos;
    uint8_t next_has_size;
    ui_vec2_t next_pos;
    ui_vec2_t next_size;

    uint32_t cur_id;
    ui_vec4_t cur_rect;
    ui_window_flags_t cur_flags;

    uint32_t drag_id;
    uint8_t dragging_move;
    uint8_t dragging_resize;
    uint8_t resize_using_grip;
    uint8_t resize_edge_mask;

    ui_vec2_t drag_mouse0;
    ui_vec4_t drag_rect0;

    uint32_t pick_id;
    uint8_t picked_this_frame;

    ui_win_batch_t batches[64];
    uint32_t batch_count;

    ui_cmd_t *scratch_cmds;
    uint32_t scratch_cmds_cap;

    uint8_t *scratch_used;
    uint32_t scratch_used_cap;

    ui_dockspace_t dockspaces[32];
    uint32_t dockspace_count;

    ui_dock_node_t nodes[256];
    uint16_t node_count;

    uint8_t dock_widget_active;
    uint32_t dock_widget_dockspace_id;
    uint16_t dock_widget_leaf;
    ui_dock_slot_t dock_widget_slot;
    ui_vec4_t dock_widget_rect;
    ui_vec4_t dock_widget_leaf_rect;

    uint8_t content_clip_pushed;
} ui_win_ctx_t;

static ui_win_ctx_t g_ctxs[8];

static ui_win_ctx_t *uiw_ctx(ui_ctx_t *ui)
{
    for (uint32_t i = 0; i < 8; ++i)
        if (g_ctxs[i].ui == ui)
            return &g_ctxs[i];

    for (uint32_t i = 0; i < 8; ++i)
    {
        if (!g_ctxs[i].ui)
        {
            memset(&g_ctxs[i], 0, sizeof(g_ctxs[i]));
            g_ctxs[i].ui = ui;
            return &g_ctxs[i];
        }
    }

    return &g_ctxs[0];
}

static int uiw_pt_in(ui_vec2_t p, ui_vec4_t r)
{
    return (p.x >= r.x) && (p.y >= r.y) && (p.x < (r.x + r.z)) && (p.y < (r.y + r.w));
}

static int uiw_z_max(ui_win_ctx_t *c)
{
    int m = 0;
    for (uint32_t i = 0; i < c->win_count; ++i)
        if (c->wins[i].used && c->wins[i].z > m)
            m = c->wins[i].z;
    return m;
}

static ui_win_t *uiw_find(ui_win_ctx_t *c, uint32_t id)
{
    for (uint32_t i = 0; i < c->win_count; ++i)
        if (c->wins[i].used && c->wins[i].id == id)
            return &c->wins[i];
    return 0;
}

static ui_win_t *uiw_get(ui_win_ctx_t *c, uint32_t id)
{
    ui_win_t *w = uiw_find(c, id);
    if (w)
        return w;

    if (c->win_count >= 64)
        return &c->wins[0];

    ui_win_t *nw = &c->wins[c->win_count++];
    memset(nw, 0, sizeof(*nw));
    nw->used = 1;
    nw->id = id;
    nw->rect = ui_v4(40.0f, 40.0f, 420.0f, 320.0f);
    nw->float_rect = nw->rect;
    nw->flags = UI_WIN_NONE;
    nw->z = uiw_z_max(c) + 1;
    nw->dock_ds_id = 0;
    nw->dock_leaf = 0;
    nw->scroll_y = 0.0f;
    nw->content_h_last = 0.0f;
    nw->has_scroll = 0;
    nw->scroll_drag = 0;
    return nw;
}

static void uiw_apply_min_size(ui_win_t *w)
{
    float minw = 200.0f;
    float minh = 140.0f;

    if (w->float_rect.z < minw)
        w->float_rect.z = minw;
    if (w->float_rect.w < minh)
        w->float_rect.w = minh;
}

static void uiw_apply_resize_screen_stop(ui_win_ctx_t *c, ui_win_t *w, ui_vec4_t *rr, uint8_t edge_mask, uint8_t using_grip)
{
    float sw = (float)c->ui->fb_size.x;
    float sh = (float)c->ui->fb_size.y;

    float x = rr->x;
    float y = rr->y;
    float w0 = rr->z;
    float h0 = rr->w;

    float x1 = x + w0;
    float y1 = y + h0;

    if (using_grip)
    {
        if (x1 > sw) x1 = sw;
#if UI_Y_DOWN
        if (y1 > sh) y1 = sh;
#endif
        w0 = x1 - x;
        h0 = y1 - y;

        rr->x = x;
        rr->y = y;
        rr->z = w0;
        rr->w = h0;

        w->float_rect = *rr;
        uiw_apply_min_size(w);
        *rr = w->float_rect;
        return;
    }

    if (edge_mask & 1)
    {
        if (x < 0.0f) x = 0.0f;
        w0 = x1 - x;
    }
    if (edge_mask & 2)
    {
        if (x1 > sw) x1 = sw;
        w0 = x1 - x;
    }

#if UI_Y_DOWN
    if (edge_mask & 4)
    {
        if (y < 0.0f) y = 0.0f;
        h0 = y1 - y;
    }
    if (edge_mask & 8)
    {
        if (y1 > sh) y1 = sh;
        h0 = y1 - y;
    }
#else
    if (edge_mask & 4)
    {
        if (y1 > sh) y1 = sh;
        h0 = y1 - y;
    }
    if (edge_mask & 8)
    {
        if (y < 0.0f) y = 0.0f;
        h0 = y1 - y;
    }
#endif

    rr->x = x;
    rr->y = y;
    rr->z = w0;
    rr->w = h0;

    w->float_rect = *rr;
    uiw_apply_min_size(w);
    *rr = w->float_rect;
}

static void uiw_apply_move_clamp(ui_win_ctx_t *c, ui_win_t *w)
{
    if (w->flags & UI_WIN_NO_CLAMP)
        return;

    float sw = (float)c->ui->fb_size.x;
    float sh = (float)c->ui->fb_size.y;

    if (w->float_rect.z > sw) w->float_rect.z = sw;
    if (w->float_rect.w > sh) w->float_rect.w = sh;

    if (w->float_rect.x < 0.0f) w->float_rect.x = 0.0f;
    if (w->float_rect.y < 0.0f) w->float_rect.y = 0.0f;

    if (w->float_rect.x + w->float_rect.z > sw)
        w->float_rect.x = sw - w->float_rect.z;
    if (w->float_rect.y + w->float_rect.w > sh)
        w->float_rect.y = sh - w->float_rect.w;
}

static ui_vec4_t uiw_header_rect(ui_win_ctx_t *c, ui_vec4_t r, ui_window_flags_t flags)
{
    if (flags & UI_WIN_NO_TITLEBAR)
        return ui_v4(r.x, r.y, r.z, 0.0f);
    return ui_v4(r.x, r.y, r.z, c->ui->style.line_h + c->ui->style.padding);
}

static uint8_t uiw_hit_edges(ui_vec2_t m, ui_vec4_t r, float edge)
{
    if (!uiw_pt_in(m, r))
        return 0;

    float x0 = r.x;
    float y0 = r.y;
    float x1 = r.x + r.z;
    float y1 = r.y + r.w;

    uint8_t mask = 0;

    if (m.x >= x0 && m.x < x0 + edge)
        mask |= 1;
    if (m.x <= x1 && m.x > x1 - edge)
        mask |= 2;

#if UI_Y_DOWN
    if (m.y >= y0 && m.y < y0 + edge)
        mask |= 4;
    if (m.y <= y1 && m.y > y1 - edge)
        mask |= 8;
#else
    if (m.y <= y1 && m.y > y1 - edge)
        mask |= 4;
    if (m.y >= y0 && m.y < y0 + edge)
        mask |= 8;
#endif

    return mask;
}

static ui_vec4_t uiw_grip_rect(ui_vec4_t r, float s)
{
#if UI_Y_DOWN
    return ui_v4(r.x + r.z - s, r.y + r.w - s, s, s);
#else
    return ui_v4(r.x + r.z - s, r.y, s, s);
#endif
}

static void uiw_draw_resize_grip(ui_ctx_t *ui, ui_vec4_t win_rect, int hovered)
{
    float s = 16.0f;
    ui_vec4_t g = uiw_grip_rect(win_rect, s);

    float a = hovered ? 0.55f : 0.25f;
    ui_color_t col = ui_color(ui_v3(1.0f, 1.0f, 1.0f), a);

    float step = 2.0f;
    float x1 = g.x + g.z;
    float y0 = g.y;
    float y1 = g.y + g.w;

#if UI_Y_DOWN
    for (int i = 0; i < 8; ++i)
    {
        float w = s - (float)i * step;
        float y = y1 - (float)(i + 1) * step;
        float x = x1 - w;
        ui_draw_rect(ui, ui_v4(x, y, w, step), col, 0.0f, 0.0f);
    }
#else
    for (int i = 0; i < 8; ++i)
    {
        float w = s - (float)i * step;
        float y = y0 + (float)i * step;
        float x = x1 - w;
        ui_draw_rect(ui, ui_v4(x, y, w, step), col, 0.0f, 0.0f);
    }
#endif
}

static const char *uiw_label_display(const char *title, uint32_t *out_len)
{
    if (!title)
    {
        if (out_len)
            *out_len = 0;
        return "";
    }

    const char *p = title;
    while (*p)
    {
        if (p[0] == '#' && p[1] == '#')
        {
            if (out_len)
                *out_len = (uint32_t)(p - title);
            return title;
        }
        ++p;
    }

    if (out_len)
        *out_len = (uint32_t)strlen(title);
    return title;
}

static uint32_t uiw_pick_top(ui_win_ctx_t *c, ui_vec2_t m)
{
    uint32_t best = 0;
    int bestz = -2147483647;

    for (uint32_t i = 0; i < c->win_count; ++i)
    {
        ui_win_t *w = &c->wins[i];
        if (!w->used)
            continue;
        if (!uiw_pt_in(m, w->rect))
            continue;
        if (w->z > bestz)
        {
            bestz = w->z;
            best = w->id;
        }
    }

    return best;
}

static int uiw_batch_cmp(const void *a, const void *b)
{
    const ui_win_batch_t *A = (const ui_win_batch_t *)a;
    const ui_win_batch_t *B = (const ui_win_batch_t *)b;
    if (A->z < B->z) return -1;
    if (A->z > B->z) return 1;
    if (A->start < B->start) return -1;
    if (A->start > B->start) return 1;
    return 0;
}

static void uiw_ensure_scratch(ui_win_ctx_t *c, uint32_t cmd_cap)
{
    if (c->scratch_cmds_cap < cmd_cap)
    {
        uint32_t bytes = (uint32_t)(sizeof(ui_cmd_t) * cmd_cap);
        ui_cmd_t *np = (ui_cmd_t *)c->ui->rfn(c->ui->ruser, c->scratch_cmds, bytes);
        if (np)
        {
            c->scratch_cmds = np;
            c->scratch_cmds_cap = cmd_cap;
        }
    }

    if (c->scratch_used_cap < cmd_cap)
    {
        uint32_t bytes = cmd_cap;
        uint8_t *np = (uint8_t *)c->ui->rfn(c->ui->ruser, c->scratch_used, bytes);
        if (np)
        {
            c->scratch_used = np;
            c->scratch_used_cap = cmd_cap;
        }
    }
}

static void uiw_rebuild_stream_by_z(ui_win_ctx_t *c)
{
    ui_cmd_stream_t *st = &c->ui->stream;
    uint32_t total = st->count;

    if (total == 0) return;
    if (c->batch_count == 0) return;

    ui_win_batch_t tmp[64];
    uint32_t n = c->batch_count;
    for (uint32_t i = 0; i < n; ++i) tmp[i] = c->batches[i];
    qsort(tmp, (size_t)n, sizeof(tmp[0]), uiw_batch_cmp);

    uint32_t first = total;
    uint32_t last = 0;

    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t a = tmp[i].start;
        uint32_t b = tmp[i].end;
        if (a > total) a = total;
        if (b > total) b = total;
        if (b <= a) continue;
        if (a < first) first = a;
        if (b > last) last = b;
        tmp[i].start = a;
        tmp[i].end = b;
    }

    if (first >= last) return;

    uiw_ensure_scratch(c, total);
    if (!c->scratch_cmds || !c->scratch_used) return;

    memset(c->scratch_used, 0, total);

    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t a = tmp[i].start;
        uint32_t b = tmp[i].end;
        if (b <= a) continue;
        for (uint32_t k = a; k < b; ++k)
            c->scratch_used[k] = 1;
    }

    uint32_t out = 0;

    for (uint32_t i = 0; i < first; ++i)
        c->scratch_cmds[out++] = st->cmds[i];

    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t a = tmp[i].start;
        uint32_t b = tmp[i].end;
        if (b <= a) continue;
        memcpy(c->scratch_cmds + out, st->cmds + a, (size_t)(b - a) * sizeof(ui_cmd_t));
        out += (b - a);
    }

    for (uint32_t i = first; i < last; ++i)
        if (!c->scratch_used[i])
            c->scratch_cmds[out++] = st->cmds[i];

    for (uint32_t i = last; i < total; ++i)
        c->scratch_cmds[out++] = st->cmds[i];

    memcpy(st->cmds, c->scratch_cmds, (size_t)out * sizeof(ui_cmd_t));
    st->count = out;
}

static ui_vec4_t uiw_scrollbar_rect(ui_vec4_t cr)
{
    float w = 10.0f;
    return ui_v4(cr.x + cr.z - w, cr.y, w, cr.w);
}

static void uiw_scrollbar_metrics(float view_h, float content_h, float scroll_y, float track_h, float *out_thumb_h, float *out_thumb_y)
{
    float min_thumb = 24.0f;
    float t = view_h / content_h;
    if (t > 1.0f) t = 1.0f;
    float th = track_h * t;
    if (th < min_thumb) th = min_thumb;
    if (th > track_h) th = track_h;

    float max_scroll = content_h - view_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;

    float u = max_scroll > 0.0f ? (scroll_y / max_scroll) : 0.0f;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    float ty = (track_h - th) * u;

    if (out_thumb_h) *out_thumb_h = th;
    if (out_thumb_y) *out_thumb_y = ty;
}

static void uiw_scrollbar_draw_and_handle(ui_win_ctx_t *c, ui_win_t *w, ui_vec4_t cr)
{
    float view_h = cr.w;
    float content_h = w->content_h_last;

    if (content_h <= view_h + 0.5f)
    {
        w->scroll_y = 0.0f;
        w->scroll_drag = 0;
        w->has_scroll = 0;
        return;
    }

    w->has_scroll = 1;

    float max_scroll = content_h - view_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (w->scroll_y < 0.0f) w->scroll_y = 0.0f;
    if (w->scroll_y > max_scroll) w->scroll_y = max_scroll;

    ui_vec4_t sb = uiw_scrollbar_rect(cr);

    float th = 0.0f;
    float ty = 0.0f;
    uiw_scrollbar_metrics(view_h, content_h, w->scroll_y, sb.w, &th, &ty);

    ui_vec4_t thumb = ui_v4(sb.x + 1.0f, sb.y + ty, sb.z - 2.0f, th);

    ui_ctx_t *ui = c->ui;
    ui_vec2_t m = ui->mouse;

    int over_thumb = uiw_pt_in(m, thumb);
    int pressed = ui->mouse_pressed[0] ? 1 : 0;
    int down = ui->mouse_down[0] ? 1 : 0;

    if (pressed && over_thumb)
    {
        w->scroll_drag = 1;
        w->scroll_drag_y0 = m.y;
        w->scroll_drag_scroll0 = w->scroll_y;
    }

    if (!down)
        w->scroll_drag = 0;

    if (w->scroll_drag && down)
    {
        float dy = m.y - w->scroll_drag_y0;
        float track = sb.w - th;
        float u = track > 0.0f ? (dy / track) : 0.0f;
        w->scroll_y = w->scroll_drag_scroll0 + u * max_scroll;
        if (w->scroll_y < 0.0f) w->scroll_y = 0.0f;
        if (w->scroll_y > max_scroll) w->scroll_y = max_scroll;
    }

    ui_color_t track_col = ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.25f);
    ui_color_t thumb_col = ui_color(ui_v3(0.85f, 0.85f, 0.90f), (over_thumb || w->scroll_drag) ? 0.55f : 0.35f);

    ui_draw_rect(ui, sb, track_col, 3.0f, 0.0f);
    ui_draw_rect(ui, thumb, thumb_col, 3.0f, 0.0f);
}

void ui_window_begin_frame(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);

    c->batch_count = 0;
    c->pick_id = 0;
    c->picked_this_frame = 0;
    c->content_clip_pushed = 0;

    for (uint32_t i = 0; i < c->dockspace_count; ++i)
        if (c->dockspaces[i].used)
            c->dockspaces[i].seen = 0;

    if (!ui->mouse_down[0] && c->drag_id != 0)
    {
        c->drag_id = 0;
        c->dragging_move = 0;
        c->dragging_resize = 0;
        c->resize_using_grip = 0;
        c->resize_edge_mask = 0;
    }
}

void ui_window_end_frame(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    uiw_rebuild_stream_by_z(c);
    (void)ui;
}

void ui_dockspace(ui_ctx_t *ui, const char *id, ui_vec4_t rect)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    uint32_t ds_id = ui_hash_combine(c->cur_id ? c->cur_id : c->ui->id_seed, ui_hash_str(id ? id : "DockSpace"));
    ui_dockspace_t *ds = 0;

    for (uint32_t i = 0; i < c->dockspace_count; ++i)
        if (c->dockspaces[i].used && c->dockspaces[i].id == ds_id)
            ds = &c->dockspaces[i];

    if (!ds)
    {
        for (uint32_t i = 0; i < 32; ++i)
        {
            if (!c->dockspaces[i].used)
            {
                ds = &c->dockspaces[i];
                memset(ds, 0, sizeof(*ds));
                ds->used = 1;
                ds->id = ds_id;
                if (i >= c->dockspace_count) c->dockspace_count = i + 1;
                break;
            }
        }
    }

    if (!ds) ds = &c->dockspaces[0];

    ds->rect = rect;
    ds->seen = 1;
    (void)ui;
}

void ui_window_set_next_pos(ui_ctx_t *ui, ui_vec2_t pos)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    c->next_has_pos = 1;
    c->next_pos = pos;
}

void ui_window_set_next_size(ui_ctx_t *ui, ui_vec2_t size)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    c->next_has_size = 1;
    c->next_size = size;
}

void ui_set_next_window_pos(ui_ctx_t *ui, ui_vec2_t pos)
{
    ui_window_set_next_pos(ui, pos);
}

void ui_set_next_window_size(ui_ctx_t *ui, ui_vec2_t size)
{
    ui_window_set_next_size(ui, size);
}

int ui_window_begin(ui_ctx_t *ui, const char *title, ui_window_flags_t flags)
{
    ui_win_ctx_t *c = uiw_ctx(ui);

    uint32_t id = ui_id_str(ui, title ? title : "");
    ui_win_t *w = uiw_get(c, id);
    if (!w)
        return 0;

    if (c->next_has_pos)
    {
        w->float_rect.x = c->next_pos.x;
        w->float_rect.y = c->next_pos.y;
        c->next_has_pos = 0;
    }
    if (c->next_has_size)
    {
        w->float_rect.z = c->next_size.x;
        w->float_rect.w = c->next_size.y;
        c->next_has_size = 0;
    }

    w->flags = flags;
    w->rect = w->float_rect;

    if (ui->mouse_pressed[0] && !c->picked_this_frame)
    {
        c->pick_id = uiw_pick_top(c, ui->mouse);
        if (c->pick_id)
        {
            ui_win_t *pw = uiw_find(c, c->pick_id);
            if (pw)
                pw->z = uiw_z_max(c) + 1;
        }
        c->picked_this_frame = 1;
    }

    ui_vec2_t m = ui->mouse;
    int pressed = ui->mouse_pressed[0] ? 1 : 0;
    int down = ui->mouse_down[0] ? 1 : 0;

    float edge = 6.0f;
    float grip = 16.0f;

    ui_vec4_t header = uiw_header_rect(c, w->rect, flags);
    ui_vec4_t grip_r = uiw_grip_rect(w->rect, grip);

    int header_hit = (!(flags & UI_WIN_NO_TITLEBAR)) && uiw_pt_in(m, header) && !(flags & UI_WIN_NO_MOVE);
    int grip_hover = (!(flags & UI_WIN_NO_RESIZE)) && uiw_pt_in(m, grip_r);

    uint8_t edge_mask = 0;
    int edge_hover = 0;

    if (!(flags & UI_WIN_NO_RESIZE))
    {
        edge_mask = uiw_hit_edges(m, w->rect, edge);
        edge_hover = edge_mask != 0;
    }

    int can_interact = (c->pick_id == id) || (c->drag_id == id);
    if (c->drag_id != 0 && c->drag_id != id)
        can_interact = 0;

    if (pressed && can_interact && c->drag_id == 0)
    {
        if (header_hit)
        {
            c->dragging_move = 1;
            c->dragging_resize = 0;
            c->drag_id = id;
            c->drag_mouse0 = m;
            c->drag_rect0 = w->float_rect;
            c->resize_using_grip = 0;
            c->resize_edge_mask = 0;
        }
        else if (grip_hover)
        {
            c->dragging_resize = 1;
            c->dragging_move = 0;
            c->drag_id = id;
            c->drag_mouse0 = m;
            c->drag_rect0 = w->float_rect;
            c->resize_using_grip = 1;
            c->resize_edge_mask = 0;
        }
        else if (edge_hover)
        {
            c->dragging_resize = 1;
            c->dragging_move = 0;
            c->drag_id = id;
            c->drag_mouse0 = m;
            c->drag_rect0 = w->float_rect;
            c->resize_using_grip = 0;
            c->resize_edge_mask = edge_mask;
        }
    }

    if (c->drag_id == id)
    {
        if (down)
        {
            ui_vec2_t d = ui_v2(m.x - c->drag_mouse0.x, m.y - c->drag_mouse0.y);

            if (c->dragging_move)
            {
                w->float_rect.x = c->drag_rect0.x + d.x;
                w->float_rect.y = c->drag_rect0.y + d.y;
                uiw_apply_move_clamp(c, w);
            }
            else if (c->dragging_resize && !(flags & UI_WIN_NO_RESIZE))
            {
                ui_vec4_t rr = c->drag_rect0;

                if (c->resize_using_grip)
                {
                    rr.z = c->drag_rect0.z + d.x;
#if UI_Y_DOWN
                    rr.w = c->drag_rect0.w + d.y;
#else
                    rr.w = c->drag_rect0.w - d.y;
                    rr.y = c->drag_rect0.y + d.y;
#endif
                    uiw_apply_resize_screen_stop(c, w, &rr, 0, 1);
                }
                else
                {
                    if (c->resize_edge_mask & 1)
                    {
                        rr.x = c->drag_rect0.x + d.x;
                        rr.z = c->drag_rect0.z - d.x;
                    }
                    if (c->resize_edge_mask & 2)
                    {
                        rr.z = c->drag_rect0.z + d.x;
                    }

#if UI_Y_DOWN
                    if (c->resize_edge_mask & 4)
                    {
                        rr.y = c->drag_rect0.y + d.y;
                        rr.w = c->drag_rect0.w - d.y;
                    }
                    if (c->resize_edge_mask & 8)
                    {
                        rr.w = c->drag_rect0.w + d.y;
                    }
#else
                    if (c->resize_edge_mask & 4)
                    {
                        rr.w = c->drag_rect0.w + d.y;
                    }
                    if (c->resize_edge_mask & 8)
                    {
                        rr.y = c->drag_rect0.y + d.y;
                        rr.w = c->drag_rect0.w - d.y;
                    }
#endif
                    uiw_apply_resize_screen_stop(c, w, &rr, c->resize_edge_mask, 0);
                }

                w->float_rect = rr;
                uiw_apply_min_size(w);
            }

            w->rect = w->float_rect;
        }
        else
        {
            c->dragging_move = 0;
            c->dragging_resize = 0;
            c->drag_id = 0;
            c->resize_using_grip = 0;
            c->resize_edge_mask = 0;
        }
    }

    c->cur_id = id;
    c->cur_rect = w->rect;
    c->cur_flags = flags;

    w->cmd_start = ui->stream.count;

    ui_begin_panel(ui, w->rect);

    if (!(flags & UI_WIN_NO_TITLEBAR))
    {
        ui_vec4_t hdr = uiw_header_rect(c, w->rect, flags);

        ui_draw_rect(ui, hdr, ui_color(ui_v3(0.10f, 0.10f, 0.11f), 1.0f), 0.0f, 0.0f);
        ui_draw_rect(ui, hdr, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.35f), 0.0f, 1.0f);

        uint32_t disp_len = 0;
        const char *disp = uiw_label_display(title, &disp_len);

        if (disp_len > 0)
        {
            char tmp2[256];
            uint32_t nn = disp_len;
            if (nn > 255u)
                nn = 255u;
            memcpy(tmp2, disp, (size_t)nn);
            tmp2[nn] = 0;

            ui_draw_text(
                ui,
                ui_v2(hdr.x + ui->style.padding, hdr.y + (hdr.w - ui->text_height(ui->text_user, 0)) * 0.5f),
                ui->style.text,
                0,
                tmp2);
        }
    }

    if (!(flags & UI_WIN_NO_RESIZE))
        uiw_draw_resize_grip(ui, w->rect, grip_hover);

    ui_vec4_t cr = ui_window_content_rect(ui);

    ui_push_clip(ui, cr);
    c->content_clip_pushed = 1;

    float scroll_bar_w = w->has_scroll ? 10.0f : 0.0f;
    ui_vec4_t layout_body = ui_v4(cr.x, cr.y, cr.z - scroll_bar_w, cr.w);

    ui->layout.scroll_y = w->scroll_y;
    ui_layout_begin(&ui->layout, layout_body, 0.0f);
    ui_layout_row(&ui->layout, ui->style.line_h, 1, 0, ui->style.spacing);

    return 1;
}

void ui_window_end(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);

    ui_win_t *w = uiw_find(c, c->cur_id);

    ui_vec4_t cr = ui_window_content_rect(ui);

    float ch = ui_layout_content_h(&ui->layout);

    if (w)
    {
        w->content_h_last = ch;

        float max_scroll = w->content_h_last - cr.w;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        if (w->scroll_y < 0.0f) w->scroll_y = 0.0f;
        if (w->scroll_y > max_scroll) w->scroll_y = max_scroll;

        uiw_scrollbar_draw_and_handle(c, w, cr);
    }

    if (c->content_clip_pushed)
    {
        ui_pop_clip(ui);
        c->content_clip_pushed = 0;
    }

    ui_end_panel(ui);

    if (w)
    {
        w->cmd_end = ui->stream.count;

        if (c->batch_count < 64)
        {
            c->batches[c->batch_count].id = w->id;
            c->batches[c->batch_count].z = w->z;
            c->batches[c->batch_count].start = w->cmd_start;
            c->batches[c->batch_count].end = w->cmd_end;
            c->batch_count++;
        }
    }

    c->cur_id = 0;
    c->cur_rect = ui_v4(0, 0, 0, 0);
    c->cur_flags = UI_WIN_NONE;
}

int ui_begin_window(ui_ctx_t *ui, const char *title, ui_window_flags_t flags)
{
    return ui_window_begin(ui, title, flags);
}

void ui_end_window(ui_ctx_t *ui)
{
    ui_window_end(ui);
}

ui_vec4_t ui_window_rect(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    return c->cur_rect;
}

ui_vec4_t ui_window_content_rect(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    if (c->cur_id == 0)
        return ui_v4(0, 0, 0, 0);

    float pad = ui->style.padding;
    float top = 0.0f;

    if (!(c->cur_flags & UI_WIN_NO_TITLEBAR))
        top = ui->style.line_h + ui->style.padding;

    ui_vec4_t r = c->cur_rect;
    return ui_v4(r.x + pad, r.y + top + pad, r.z - pad * 2.0f, r.w - top - pad * 2.0f);
}
