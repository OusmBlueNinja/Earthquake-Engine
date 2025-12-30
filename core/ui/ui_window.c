#include "ui_window.h"
#include "ui.h"
#include "ui_hash.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
    uint8_t seen;
    uint32_t id;
    char title[64];

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
    uint8_t tab_count;
    uint8_t tab_active;
    uint32_t tabs[8];
} ui_dock_node_t;

typedef struct ui_dockspace_t
{
    uint8_t used;
    uint8_t seen;
    uint32_t id;
    ui_vec4_t rect;
    uint16_t root;
} ui_dockspace_t;

struct ui_win_ctx_t;

// Forward declarations (needed because some helpers are defined later in the file).
static ui_dockspace_t *uiw_find_dockspace(struct ui_win_ctx_t *c, uint32_t ds_id);
static void uiw_dock_leaf_remove_win(struct ui_win_ctx_t *c, uint16_t leaf, uint32_t win_id);
static void uiw_dock_prune_empty_leaf(struct ui_win_ctx_t *c, ui_dockspace_t *ds, uint16_t leaf);

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

    ui_win_batch_t batches[96];
    uint32_t batch_count;

    ui_cmd_t *scratch_cmds;
    uint32_t scratch_cmds_cap;

    uint8_t *scratch_used;
    uint32_t scratch_used_cap;

    ui_dockspace_t dockspaces[32];
    uint32_t dockspace_count;
    uint32_t default_dockspace_id;

    ui_dock_node_t nodes[256];
    uint16_t node_count;

    uint8_t dock_widget_active;
    uint32_t dock_widget_win_id;
    uint32_t dock_widget_dockspace_id;
    uint16_t dock_widget_leaf;
    ui_dock_slot_t dock_widget_slot;
    ui_vec4_t dock_widget_rect;
    ui_vec4_t dock_widget_leaf_rect;

    uint8_t dock_preview_visible;
    ui_dock_slot_t dock_preview_hovered;
    ui_vec4_t dock_preview_boxes[5];
    uint8_t dock_preview_target_visible;
    ui_vec4_t dock_preview_target_rect;

    uint8_t dock_splitter_count;
    ui_vec4_t dock_splitter_rects[64];
    uint32_t dock_splitter_ids[64];

    uint8_t tab_drag_active;
    uint8_t tab_dragging;
    uint16_t tab_drag_leaf;
    uint32_t tab_drag_win_id;
    ui_vec2_t tab_drag_mouse0;

    uint8_t dock_drag_active;
    uint8_t dock_drag_armed;
    uint32_t dock_drag_win_id;
    uint32_t dock_drag_src_ds_id;
    uint16_t dock_drag_src_leaf;

    uint8_t content_clip_pushed;
    uint32_t hovered_id;
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

static int uiw_effective_z(const ui_win_t *w)
{
    // Policy: floating windows should always render above docked windows.
    // Keep raw `w->z` for intra-group ordering.
    const int bias = 1000000;
    int docked = (w && w->dock_ds_id != 0 && w->dock_leaf != 0) ? 1 : 0;
    return (w ? w->z : 0) + (docked ? 0 : bias);
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
    nw->seen = 0;
    nw->id = id;
    nw->title[0] = 0;
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

static void uiw_remove_win_from_all_tabs(ui_win_ctx_t *c, uint32_t win_id)
{
    if (!c || win_id == 0)
        return;
    for (uint16_t i = 1; i < 256; ++i)
    {
        ui_dock_node_t *n = &c->nodes[i];
        if (!n->used || n->split != UI_DOCKSPLIT_NONE || n->tab_count == 0)
            continue;
        uiw_dock_leaf_remove_win(c, i, win_id);
    }
}

static void uiw_gc_unseen_windows(ui_win_ctx_t *c)
{
    if (!c)
        return;

    for (uint32_t i = 0; i < c->win_count; ++i)
    {
        ui_win_t *w = &c->wins[i];
        if (!w->used)
            continue;
        if (w->seen)
            continue;

        // Remove from docking structures.
        if (w->dock_ds_id && w->dock_leaf)
        {
            ui_dockspace_t *ds = uiw_find_dockspace(c, w->dock_ds_id);
            uint16_t leaf = w->dock_leaf;
            uiw_dock_leaf_remove_win(c, leaf, w->id);
            if (ds)
                uiw_dock_prune_empty_leaf(c, ds, leaf);
        }
        else
        {
            uiw_remove_win_from_all_tabs(c, w->id);
        }

        // Clear any interaction state that references this window.
        if (c->drag_id == w->id)
            c->drag_id = 0;
        if (c->pick_id == w->id)
            c->pick_id = 0;
        if (c->dock_drag_win_id == w->id)
        {
            c->dock_drag_active = 0;
            c->dock_drag_armed = 0;
            c->dock_drag_win_id = 0;
            c->dock_drag_src_ds_id = 0;
            c->dock_drag_src_leaf = 0;
        }

        // Finally, free.
        w->used = 0;
        w->seen = 0;
        w->dock_ds_id = 0;
        w->dock_leaf = 0;
    }
}

static ui_dockspace_t *uiw_find_dockspace(ui_win_ctx_t *c, uint32_t ds_id)
{
    if (!c || !ds_id)
        return 0;
    for (uint32_t i = 0; i < c->dockspace_count; ++i)
        if (c->dockspaces[i].used && c->dockspaces[i].id == ds_id)
            return &c->dockspaces[i];
    return 0;
}

static ui_dock_node_t *uiw_node(ui_win_ctx_t *c, uint16_t idx)
{
    if (!c || idx >= 256)
        return 0;
    ui_dock_node_t *n = &c->nodes[idx];
    return n->used ? n : 0;
}

static float uiw_clampf(float v, float a, float b)
{
    if (v < a)
        return a;
    if (v > b)
        return b;
    return v;
}

static uint16_t uiw_node_alloc(ui_win_ctx_t *c)
{
    if (!c)
        return 0;
    // Keep index 0 as "null" so it can be used as an invalid sentinel.
    for (uint16_t i = 1; i < 256; ++i)
    {
        if (!c->nodes[i].used)
        {
            memset(&c->nodes[i], 0, sizeof(c->nodes[i]));
            c->nodes[i].used = 1;
            if (i >= c->node_count)
                c->node_count = (uint16_t)(i + 1);
            return i;
        }
    }
    return 0;
}

static ui_vec4_t uiw_dock_rect_split(ui_vec4_t r, ui_dock_split_t split, float ratio, int take_a)
{
    ratio = uiw_clampf(ratio, 0.05f, 0.95f);
    if (split == UI_DOCKSPLIT_VERT)
    {
        float w0 = r.z * ratio;
        if (take_a)
            return ui_v4(r.x, r.y, w0, r.w);
        return ui_v4(r.x + w0, r.y, r.z - w0, r.w);
    }
    if (split == UI_DOCKSPLIT_HORZ)
    {
        float h0 = r.w * ratio;
#if UI_Y_DOWN
        if (take_a)
            return ui_v4(r.x, r.y, r.z, h0);
        return ui_v4(r.x, r.y + h0, r.z, r.w - h0);
#else
        if (take_a)
            return ui_v4(r.x, r.y + (r.w - h0), r.z, h0);
        return ui_v4(r.x, r.y, r.z, r.w - h0);
#endif
    }
    return r;
}

static uint16_t uiw_dock_leaf_at(ui_win_ctx_t *c, uint16_t node_idx, ui_vec4_t r, ui_vec2_t p)
{
    ui_dock_node_t *n = uiw_node(c, node_idx);
    if (!n)
        return 0;
    if (n->split == UI_DOCKSPLIT_NONE)
        return uiw_pt_in(p, r) ? node_idx : 0;

    ui_vec4_t ra = uiw_dock_rect_split(r, (ui_dock_split_t)n->split, n->ratio, 1);
    ui_vec4_t rb = uiw_dock_rect_split(r, (ui_dock_split_t)n->split, n->ratio, 0);

    uint16_t hit = uiw_dock_leaf_at(c, n->a, ra, p);
    if (hit)
        return hit;
    return uiw_dock_leaf_at(c, n->b, rb, p);
}

static int uiw_dock_leaf_rect(ui_win_ctx_t *c, uint16_t node_idx, ui_vec4_t r, uint16_t want_leaf, ui_vec4_t *out_rect)
{
    ui_dock_node_t *n = uiw_node(c, node_idx);
    if (!n)
        return 0;

    if (n->split == UI_DOCKSPLIT_NONE)
    {
        if (node_idx == want_leaf)
        {
            if (out_rect)
                *out_rect = r;
            return 1;
        }
        return 0;
    }

    ui_vec4_t ra = uiw_dock_rect_split(r, (ui_dock_split_t)n->split, n->ratio, 1);
    ui_vec4_t rb = uiw_dock_rect_split(r, (ui_dock_split_t)n->split, n->ratio, 0);

    if (uiw_dock_leaf_rect(c, n->a, ra, want_leaf, out_rect))
        return 1;
    return uiw_dock_leaf_rect(c, n->b, rb, want_leaf, out_rect);
}

static int uiw_dock_find_parent(ui_win_ctx_t *c, uint16_t root, uint16_t child, uint16_t *out_parent, int *out_child_is_a)
{
    ui_dock_node_t *n = uiw_node(c, root);
    if (!n || n->split == UI_DOCKSPLIT_NONE)
        return 0;
    if (n->a == child)
    {
        if (out_parent)
            *out_parent = root;
        if (out_child_is_a)
            *out_child_is_a = 1;
        return 1;
    }
    if (n->b == child)
    {
        if (out_parent)
            *out_parent = root;
        if (out_child_is_a)
            *out_child_is_a = 0;
        return 1;
    }
    if (uiw_dock_find_parent(c, n->a, child, out_parent, out_child_is_a))
        return 1;
    return uiw_dock_find_parent(c, n->b, child, out_parent, out_child_is_a);
}

static void uiw_dock_leaf_remove_win(ui_win_ctx_t *c, uint16_t leaf, uint32_t win_id)
{
    ui_dock_node_t *n = uiw_node(c, leaf);
    if (!n || n->split != UI_DOCKSPLIT_NONE || n->tab_count == 0)
        return;

    uint8_t out = 0;
    for (uint8_t i = 0; i < n->tab_count; ++i)
        if (n->tabs[i] != win_id)
            n->tabs[out++] = n->tabs[i];
    n->tab_count = out;
    if (n->tab_active >= n->tab_count)
        n->tab_active = n->tab_count ? (uint8_t)(n->tab_count - 1) : 0;
}

static void uiw_dock_prune_empty_leaf(ui_win_ctx_t *c, ui_dockspace_t *ds, uint16_t leaf)
{
    if (!c || !ds || !ds->root || leaf == 0)
        return;

    uint16_t cur = leaf;
    for (;;)
    {
        ui_dock_node_t *ln = uiw_node(c, cur);
        if (!ln || ln->split != UI_DOCKSPLIT_NONE)
            break;
        if (ln->tab_count != 0)
            break;

        uint16_t parent = 0;
        int cur_is_a = 0;
        if (!uiw_dock_find_parent(c, ds->root, cur, &parent, &cur_is_a))
            break; // root leaf (or disconnected)

        ui_dock_node_t *pn = uiw_node(c, parent);
        if (!pn || pn->split == UI_DOCKSPLIT_NONE)
            break;

        uint16_t sibling = cur_is_a ? pn->b : pn->a;
        if (sibling == 0)
            break;

        uint16_t gparent = 0;
        int parent_is_a = 0;
        if (uiw_dock_find_parent(c, ds->root, parent, &gparent, &parent_is_a))
        {
            ui_dock_node_t *gn = uiw_node(c, gparent);
            if (!gn)
                break;
            if (parent_is_a)
                gn->a = sibling;
            else
                gn->b = sibling;
        }
        else
        {
            ds->root = sibling;
        }

        // Free the now-empty leaf and its split parent.
        c->nodes[cur].used = 0;
        c->nodes[parent].used = 0;

        cur = sibling;
    }
}

static void uiw_dock_leaf_add_win(ui_win_ctx_t *c, uint16_t leaf, uint32_t win_id)
{
    ui_dock_node_t *n = uiw_node(c, leaf);
    if (!n || n->split != UI_DOCKSPLIT_NONE)
        return;
    for (uint8_t i = 0; i < n->tab_count; ++i)
        if (n->tabs[i] == win_id)
        {
            return;
        }
    if (n->tab_count < 8)
    {
        n->tabs[n->tab_count++] = win_id;
        n->tab_active = (uint8_t)(n->tab_count - 1);
    }
}

static uint32_t uiw_dock_leaf_active_win(const ui_win_ctx_t *c, uint16_t leaf)
{
    if (!c || leaf >= 256)
        return 0;
    const ui_dock_node_t *n = &c->nodes[leaf];
    if (!n->used || n->split != UI_DOCKSPLIT_NONE || n->tab_count == 0)
        return 0;
    uint8_t a = n->tab_active < n->tab_count ? n->tab_active : 0;
    return n->tabs[a];
}

static ui_dock_slot_t uiw_dock_slot_from_point(ui_vec4_t leaf_r, ui_vec2_t p)
{
    if (leaf_r.z <= 1.0f || leaf_r.w <= 1.0f)
        return UI_DOCKSLOT_CENTER;

    float u = (p.x - leaf_r.x) / leaf_r.z;
#if UI_Y_DOWN
    float v = (p.y - leaf_r.y) / leaf_r.w;
#else
    float v = 1.0f - ((p.y - leaf_r.y) / leaf_r.w);
#endif

    float band = 0.25f;
    if (u < band)
        return UI_DOCKSLOT_LEFT;
    if (u > 1.0f - band)
        return UI_DOCKSLOT_RIGHT;
    if (v < band)
        return UI_DOCKSLOT_TOP;
    if (v > 1.0f - band)
        return UI_DOCKSLOT_BOTTOM;
    return UI_DOCKSLOT_CENTER;
}

static ui_vec4_t uiw_dock_slot_rect(ui_vec4_t leaf_r, ui_dock_slot_t slot)
{
    float band = 0.25f;
    if (slot == UI_DOCKSLOT_LEFT)
        return ui_v4(leaf_r.x, leaf_r.y, leaf_r.z * band, leaf_r.w);
    if (slot == UI_DOCKSLOT_RIGHT)
        return ui_v4(leaf_r.x + leaf_r.z * (1.0f - band), leaf_r.y, leaf_r.z * band, leaf_r.w);
    if (slot == UI_DOCKSLOT_TOP)
        return ui_v4(leaf_r.x, leaf_r.y, leaf_r.z, leaf_r.w * band);
    if (slot == UI_DOCKSLOT_BOTTOM)
        return ui_v4(leaf_r.x, leaf_r.y + leaf_r.w * (1.0f - band), leaf_r.z, leaf_r.w * band);
    return leaf_r;
}

// Returns hovered slot and optionally outputs the 5 target box rects:
// [0]=Left [1]=Right [2]=Top [3]=Bottom [4]=Center.
static ui_dock_slot_t uiw_dock_preview_boxes(ui_vec2_t m, ui_vec4_t leaf_r, int *out_visible, ui_vec4_t out_boxes[5])
{
    if (out_visible)
        *out_visible = 0;

    ui_vec2_t c = ui_rect_center(leaf_r);
    float dx = m.x - c.x;
    float dy = m.y - c.y;
    float dist2 = dx * dx + dy * dy;

    float min_dim = ui_minf(leaf_r.z, leaf_r.w);
    // ImGui-like: preview appears when the cursor is reasonably near the node center.
    float radius = ui_minf(200.0f, ui_maxf(70.0f, min_dim * 0.45f));
    if (dist2 > radius * radius)
        return UI_DOCKSLOT_NONE;

    if (out_visible)
        *out_visible = 1;

    float box = 22.0f;
    float gap = 7.0f;

    ui_vec4_t rc = ui_v4(c.x - box * 0.5f, c.y - box * 0.5f, box, box);
    ui_vec4_t rl = ui_v4(rc.x - (box + gap), rc.y, box, box);
    ui_vec4_t rr = ui_v4(rc.x + (box + gap), rc.y, box, box);
    ui_vec4_t rt = ui_v4(rc.x, rc.y - (box + gap), box, box);
    ui_vec4_t rb = ui_v4(rc.x, rc.y + (box + gap), box, box);

    ui_dock_slot_t hovered = UI_DOCKSLOT_NONE;
    if (uiw_pt_in(m, rc)) hovered = UI_DOCKSLOT_CENTER;
    else if (uiw_pt_in(m, rl)) hovered = UI_DOCKSLOT_LEFT;
    else if (uiw_pt_in(m, rr)) hovered = UI_DOCKSLOT_RIGHT;
    else if (uiw_pt_in(m, rt)) hovered = UI_DOCKSLOT_TOP;
    else if (uiw_pt_in(m, rb)) hovered = UI_DOCKSLOT_BOTTOM;
    else
    {
        // Small "magnet" to the center slot only when very close (prevents accidental docking).
        float inner = box * 0.75f;
        if (dist2 <= inner * inner)
            hovered = UI_DOCKSLOT_CENTER;
    }

    if (out_boxes)
    {
        out_boxes[0] = rl;
        out_boxes[1] = rr;
        out_boxes[2] = rt;
        out_boxes[3] = rb;
        out_boxes[4] = rc;
    }

    return hovered;
}

static void uiw_emit_dock_preview_overlay(ui_win_ctx_t *c)
{
    if (!c || !c->ui || (!c->dock_preview_visible && !c->dock_preview_target_visible))
        return;

    ui_ctx_t *ui = c->ui;
    ui_cmd_stream_t *st = &ui->stream;

    ui_vec4_t full = ui_v4(0.0f, 0.0f, (float)ui->fb_size.x, (float)ui->fb_size.y);
    ui_cmd_push_clip(st, full);

    if (c->dock_preview_target_visible)
    {
        ui_color_t fill2 = ui_color(ui->style.accent_dim.rgb, 0.18f);
        ui_color_t frame2 = ui_color(ui->style.accent.rgb, 0.70f);
        ui_cmd_push_rect(st, c->dock_preview_target_rect, fill2, 0.0f, 0.0f);
        ui_cmd_push_rect(st, c->dock_preview_target_rect, frame2, 0.0f, 2.0f);
    }

    if (!c->dock_preview_visible)
        return;

    // Lightweight backplate (ImGui-style) so it's obvious side docking is available.
    {
        float x0 = c->dock_preview_boxes[0].x;
        float y0 = c->dock_preview_boxes[2].y;
        float x1 = c->dock_preview_boxes[1].x + c->dock_preview_boxes[1].z;
        float y1 = c->dock_preview_boxes[3].y + c->dock_preview_boxes[3].w;
        float pad = 6.0f;
        ui_vec4_t plate = ui_v4(x0 - pad, y0 - pad, (x1 - x0) + pad * 2.0f, (y1 - y0) + pad * 2.0f);
        ui_cmd_push_rect(st, plate, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.18f), 6.0f, 0.0f);
        ui_cmd_push_rect(st, plate, ui_color(ui->style.outline.rgb, 0.20f), 6.0f, 1.0f);
    }

    ui_color_t frame = ui_color(ui->style.outline.rgb, 0.70f);
    ui_color_t fill = ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.25f);
    ui_color_t hot = ui_color(ui->style.accent.rgb, 0.92f);
    ui_color_t hot_fill = ui_color(ui->style.accent_dim.rgb, 0.22f);

    ui_dock_slot_t slots[5] = {UI_DOCKSLOT_LEFT, UI_DOCKSLOT_RIGHT, UI_DOCKSLOT_TOP, UI_DOCKSLOT_BOTTOM, UI_DOCKSLOT_CENTER};
    for (int i = 0; i < 5; ++i)
    {
        int is_hot = (c->dock_preview_hovered == slots[i]);
        ui_cmd_push_rect(st, c->dock_preview_boxes[i], is_hot ? hot_fill : fill, 3.0f, 0.0f);
        ui_cmd_push_rect(st, c->dock_preview_boxes[i], is_hot ? hot : frame, 3.0f, 1.5f);
    }
}

static void uiw_emit_dock_splitters_batch(ui_win_ctx_t *c)
{
    if (!c || !c->ui || c->dock_splitter_count == 0)
        return;

    ui_ctx_t *ui = c->ui;

    // Emit as a batch so we can sort it between docked and floating windows.
    uint32_t start = ui->stream.count;

    ui_cmd_stream_t *st = &ui->stream;
    ui_vec4_t full = ui_v4(0.0f, 0.0f, (float)ui->fb_size.x, (float)ui->fb_size.y);
    ui_cmd_push_clip(st, full);

    // Draw only subtle lines; hitboxes are larger than visuals (handled in uiw_dock_collect_splitters()).
    ui_color_t col = ui_color(ui->style.outline.rgb, 0.22f);
    ui_color_t hot = ui_color(ui->style.accent.rgb, 0.60f);

    for (uint32_t i = 0; i < c->dock_splitter_count; ++i)
    {
        uint32_t sid = c->dock_splitter_ids[i];
        int is_hot = (ui->active_id == sid) || (ui->hot_id == sid);
        ui_vec4_t r = c->dock_splitter_rects[i];

        ui_vec4_t line = r;
        if (r.z < r.w)
            line = ui_v4(r.x + r.z * 0.5f - 0.5f, r.y, 1.0f, r.w);
        else
            line = ui_v4(r.x, r.y + r.w * 0.5f - 0.5f, r.z, 1.0f);

        ui_cmd_push_rect(st, line, is_hot ? hot : col, 0.0f, 0.0f);
    }

    uint32_t end = ui->stream.count;
    if (end <= start)
        return;

    if (c->batch_count < (uint32_t)(sizeof(c->batches) / sizeof(c->batches[0])))
    {
        c->batches[c->batch_count].id = 0;
        // Between docked and floating (floating uses +1,000,000 bias).
        c->batches[c->batch_count].z = 500000;
        c->batches[c->batch_count].start = start;
        c->batches[c->batch_count].end = end;
        c->batch_count++;
    }
}

static int uiw_update_dock_hover(ui_win_ctx_t *c, uint32_t win_id, ui_vec2_t m)
{
    if (!c || !c->ui)
        return 0;

    c->dock_widget_active = 0;
    c->dock_widget_win_id = 0;
    c->dock_widget_dockspace_id = 0;
    c->dock_widget_leaf = 0;
    c->dock_widget_slot = UI_DOCKSLOT_NONE;
    c->dock_preview_visible = 0;
    c->dock_preview_hovered = UI_DOCKSLOT_NONE;
    c->dock_preview_target_visible = 0;

    for (uint32_t di = 0; di < c->dockspace_count; ++di)
    {
        ui_dockspace_t *ds = &c->dockspaces[di];
        if (!ds->used || !ds->seen || !ds->root)
            continue;
        if (!uiw_pt_in(m, ds->rect))
            continue;

        uint16_t leaf = uiw_dock_leaf_at(c, ds->root, ds->rect, m);
        if (!leaf)
            leaf = ds->root;

        ui_vec4_t leaf_r = ds->rect;
        uiw_dock_leaf_rect(c, ds->root, ds->rect, leaf, &leaf_r);

        int visible = 0;
        ui_dock_slot_t hovered_slot = uiw_dock_preview_boxes(m, leaf_r, &visible, c->dock_preview_boxes);
        // Only allow side docking when the indicators are visible (prevents accidental edge docking).
        if (!visible)
            return 0;

        c->dock_preview_visible = 1;
        c->dock_preview_hovered = hovered_slot;

        if (hovered_slot != UI_DOCKSLOT_NONE)
        {
            c->dock_widget_active = 1;
            c->dock_widget_win_id = win_id;
            c->dock_widget_dockspace_id = ds->id;
            c->dock_widget_leaf = leaf;
            c->dock_widget_slot = hovered_slot;
            c->dock_widget_leaf_rect = leaf_r;
            c->dock_widget_rect = uiw_dock_slot_rect(leaf_r, hovered_slot);
            c->dock_preview_target_visible = 1;
            c->dock_preview_target_rect = c->dock_widget_rect;
            return 1;
        }

        return 0;
    }

    return 0;
}

static void uiw_undock_window(ui_win_ctx_t *c, ui_win_t *w)
{
    if (!c || !w || !w->dock_ds_id || !w->dock_leaf)
        return;
    ui_dockspace_t *ds = uiw_find_dockspace(c, w->dock_ds_id);
    if (ds)
    {
        uint16_t leaf = w->dock_leaf;
        uiw_dock_leaf_remove_win(c, leaf, w->id);
        uiw_dock_prune_empty_leaf(c, ds, leaf);
    }
    w->dock_ds_id = 0;
    w->dock_leaf = 0;
}

static void uiw_dock_window(ui_win_ctx_t *c, ui_win_t *w, ui_dockspace_t *ds, uint16_t leaf, ui_dock_slot_t slot)
{
    if (!c || !w || !ds || !leaf)
        return;

    // Remove from previous dock location first.
    uiw_undock_window(c, w);

    if (slot == UI_DOCKSLOT_CENTER)
    {
        uiw_dock_leaf_add_win(c, leaf, w->id);
        w->dock_ds_id = ds->id;
        w->dock_leaf = leaf;
        return;
    }

    ui_dock_split_t split = (slot == UI_DOCKSLOT_LEFT || slot == UI_DOCKSLOT_RIGHT) ? UI_DOCKSPLIT_VERT : UI_DOCKSPLIT_HORZ;

    uint16_t new_leaf = uiw_node_alloc(c);
    ui_dock_node_t *nl = uiw_node(c, new_leaf);
    if (!nl)
        return;
    nl->split = UI_DOCKSPLIT_NONE;
    nl->ratio = 0.5f;
    nl->a = 0;
    nl->b = 0;
    nl->tab_count = 0;
    nl->tab_active = 0;
    uiw_dock_leaf_add_win(c, new_leaf, w->id);

    uint16_t split_node = uiw_node_alloc(c);
    ui_dock_node_t *sn = uiw_node(c, split_node);
    if (!sn)
        return;

    sn->split = (uint8_t)split;
    sn->tab_count = 0;
    sn->tab_active = 0;

    int new_is_a = (slot == UI_DOCKSLOT_LEFT || slot == UI_DOCKSLOT_TOP) ? 1 : 0;
    if (new_is_a)
    {
        sn->a = new_leaf;
        sn->b = leaf;
        sn->ratio = 0.25f;
    }
    else
    {
        sn->a = leaf;
        sn->b = new_leaf;
        sn->ratio = 0.75f;
    }

    uint16_t parent = 0;
    int child_is_a = 0;
    if (uiw_dock_find_parent(c, ds->root, leaf, &parent, &child_is_a))
    {
        ui_dock_node_t *pn = uiw_node(c, parent);
        if (pn)
        {
            if (child_is_a)
                pn->a = split_node;
            else
                pn->b = split_node;
        }
    }
    else
    {
        ds->root = split_node;
    }

    w->dock_ds_id = ds->id;
    w->dock_leaf = new_leaf;
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
    // Match ImGui feel: tighter titlebar than content padding.
    float h = c->ui->style.line_h + c->ui->style.padding * 0.35f;
    if (h < c->ui->style.line_h + 6.0f)
        h = c->ui->style.line_h + 6.0f;
    return ui_v4(r.x, r.y, r.z, h);
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

static void uiw_tab_move(ui_dock_node_t *leaf, uint8_t from, uint8_t to)
{
    if (!leaf || leaf->tab_count <= 1 || from >= leaf->tab_count || to >= leaf->tab_count || from == to)
        return;

    uint32_t v = leaf->tabs[from];
    if (from < to)
        memmove(&leaf->tabs[from], &leaf->tabs[from + 1], (size_t)(to - from) * sizeof(leaf->tabs[0]));
    else
        memmove(&leaf->tabs[to + 1], &leaf->tabs[to], (size_t)(from - to) * sizeof(leaf->tabs[0]));
    leaf->tabs[to] = v;

    if (leaf->tab_active == from)
        leaf->tab_active = to;
    else if (from < to && leaf->tab_active > from && leaf->tab_active <= to)
        leaf->tab_active -= 1;
    else if (to < from && leaf->tab_active >= to && leaf->tab_active < from)
        leaf->tab_active += 1;
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
        // If multiple windows are docked as tabs in the same leaf, only the active tab should receive input.
        if (w->dock_ds_id != 0 && w->dock_leaf != 0)
        {
            uint32_t active = uiw_dock_leaf_active_win(c, w->dock_leaf);
            if (active == 0 || active != w->id)
                continue;
        }
        if (!uiw_pt_in(m, w->rect))
            continue;
        int ez = uiw_effective_z(w);
        if (ez > bestz)
        {
            bestz = ez;
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

    ui_win_batch_t tmp[96];
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
    float slack = c->ui->style.padding * 0.5f;

    if (content_h <= view_h + 0.5f)
    {
        w->scroll_y = 0.0f;
        w->scroll_drag = 0;
        w->has_scroll = 0;
        return;
    }

    w->has_scroll = 1;

    float max_scroll = content_h - view_h + slack;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (w->scroll_y < 0.0f) w->scroll_y = 0.0f;
    if (w->scroll_y > max_scroll) w->scroll_y = max_scroll;

    ui_vec4_t sb = uiw_scrollbar_rect(cr);

    float th = 0.0f;
    float ty = 0.0f;
    uiw_scrollbar_metrics(view_h, content_h + slack, w->scroll_y, sb.w, &th, &ty);

    ui_vec4_t thumb = ui_v4(sb.x + 1.0f, sb.y + ty, sb.z - 2.0f, th);

    ui_ctx_t *ui = c->ui;
    ui_vec2_t m = ui->mouse;

    int allow_input = ui->window_accept_input ? 1 : 0;
    int over_thumb = uiw_pt_in(m, thumb);
    int pressed = (allow_input && ui->mouse_pressed[0]) ? 1 : 0;
    int down = ((allow_input || w->scroll_drag) && ui->mouse_down[0]) ? 1 : 0;

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

static void uiw_dock_collect_splitters(ui_win_ctx_t *c, ui_dockspace_t *ds, uint16_t node_idx, ui_vec4_t r)
{
    if (!c || !c->ui || !ds || !node_idx)
        return;

    ui_dock_node_t *n = uiw_node(c, node_idx);
    if (!n || n->split == UI_DOCKSPLIT_NONE)
        return;

    ui_ctx_t *ui = c->ui;
    ui_vec2_t m = ui->mouse;

    uint32_t sid = ui_hash_combine(ds->id, 0x51E17u ^ (uint32_t)node_idx);
    ui_vec4_t hr = r;
    float pad = 3.0f;

    if (n->split == UI_DOCKSPLIT_VERT)
    {
        float x = r.x + r.z * n->ratio;
        hr = ui_v4(x - pad, r.y, pad * 2.0f, r.w);
    }
    else
    {
#if UI_Y_DOWN
        float y = r.y + r.w * n->ratio;
#else
        float y = r.y + (r.w - (r.w * n->ratio));
#endif
        hr = ui_v4(r.x, y - pad, r.z, pad * 2.0f);
    }

    // Record for overlay drawing.
    if (c->dock_splitter_count < 64)
    {
        c->dock_splitter_rects[c->dock_splitter_count] = hr;
        c->dock_splitter_ids[c->dock_splitter_count] = sid;
        c->dock_splitter_count++;
    }

    int hovered = uiw_pt_in(m, hr);
    if (hovered && ui->active_id == 0)
        ui->hot_id = sid;
    if (hovered || ui->active_id == sid)
    {
        ui_request_cursor_state(ui, UI_CURSOR_NORMAL, 90);
        ui_request_cursor_shape(ui, (n->split == UI_DOCKSPLIT_VERT) ? UI_CURSOR_RESIZE_EW : UI_CURSOR_RESIZE_NS, 90);
    }

    // Start resizing if clicked.
    if (hovered && ui->mouse_pressed[0] && ui->active_id == 0 && c->drag_id == 0)
        ui->active_id = sid;

    // Update ratio while held.
    if (ui->active_id == sid && ui->mouse_down[0])
    {
        float ratio = n->ratio;
        if (n->split == UI_DOCKSPLIT_VERT)
            ratio = (r.z > 1.0f) ? ((m.x - r.x) / r.z) : ratio;
        else
        {
#if UI_Y_DOWN
            ratio = (r.w > 1.0f) ? ((m.y - r.y) / r.w) : ratio;
#else
            ratio = (r.w > 1.0f) ? (1.0f - ((m.y - r.y) / r.w)) : ratio;
#endif
        }
        n->ratio = uiw_clampf(ratio, 0.05f, 0.95f);
    }

    if (ui->active_id == sid && ui->mouse_released[0])
        ui->active_id = 0;

    // Recurse with potentially updated ratio.
    ui_vec4_t ra = uiw_dock_rect_split(r, (ui_dock_split_t)n->split, n->ratio, 1);
    ui_vec4_t rb = uiw_dock_rect_split(r, (ui_dock_split_t)n->split, n->ratio, 0);
    uiw_dock_collect_splitters(c, ds, n->a, ra);
    uiw_dock_collect_splitters(c, ds, n->b, rb);
}

void ui_window_begin_frame(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);

    c->batch_count = 0;
    c->pick_id = 0;
    c->picked_this_frame = 0;
    c->content_clip_pushed = 0;
    c->hovered_id = uiw_pick_top(c, ui->mouse);
    c->dock_widget_active = 0;
    c->dock_widget_win_id = 0;
    c->dock_widget_dockspace_id = 0;
    c->dock_widget_leaf = 0;
    c->dock_widget_slot = UI_DOCKSLOT_NONE;
    c->dock_preview_visible = 0;
    c->dock_preview_hovered = UI_DOCKSLOT_NONE;
    c->dock_preview_target_visible = 0;
    c->dock_splitter_count = 0;
    if (!ui->mouse_down[0])
    {
        c->tab_drag_active = 0;
        c->tab_dragging = 0;
        c->tab_drag_leaf = 0;
        c->tab_drag_win_id = 0;
    }
    // Don't clear dock-drag state here; ui_window_begin() needs to see `dock_drag_active` on the release frame.

    for (uint32_t i = 0; i < c->win_count; ++i)
        if (c->wins[i].used)
            c->wins[i].seen = 0;

    for (uint32_t i = 0; i < c->dockspace_count; ++i)
        if (c->dockspaces[i].used)
            c->dockspaces[i].seen = 0;

    // Keep drag state alive for the release frame so ui_window_begin() can apply docking/drop logic.
    if (!ui->mouse_down[0] && !ui->mouse_released[0] && c->drag_id != 0)
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
    uiw_gc_unseen_windows(c);
    uiw_emit_dock_splitters_batch(c);
    uiw_rebuild_stream_by_z(c);
    uiw_emit_dock_preview_overlay(c);
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

    if (c->default_dockspace_id == 0)
        c->default_dockspace_id = ds->id;

    if (ds->root == 0 || !uiw_node(c, ds->root))
    {
        uint16_t root = uiw_node_alloc(c);
        ui_dock_node_t *n = uiw_node(c, root);
        if (n)
        {
            n->split = UI_DOCKSPLIT_NONE;
            n->ratio = 0.5f;
            n->a = 0;
            n->b = 0;
            n->tab_count = 0;
            n->tab_active = 0;
        }
        ds->root = root;
    }

    // Splitter bars: allow resizing existing splits (ImGui-like).
    if (ds->seen && ds->root)
        uiw_dock_collect_splitters(c, ds, ds->root, ds->rect);
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
    w->seen = 1;

    {
        uint32_t lab_len = 0;
        const char *lab = uiw_label_display(title ? title : "", &lab_len);
        if (!lab)
            lab = "";
        if (lab_len >= sizeof(w->title))
            lab_len = (uint32_t)(sizeof(w->title) - 1u);
        memcpy(w->title, lab, lab_len);
        w->title[lab_len] = 0;
    }

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

    int docked = (w->dock_ds_id != 0 && w->dock_leaf != 0) ? 1 : 0;
    if (docked)
    {
        ui_dockspace_t *ds = uiw_find_dockspace(c, w->dock_ds_id);
        if (ds && ds->seen && ds->root)
        {
            ui_vec4_t leaf_r = ds->rect;
            if (uiw_dock_leaf_rect(c, ds->root, ds->rect, w->dock_leaf, &leaf_r))
                w->rect = leaf_r;

            uiw_dock_leaf_add_win(c, w->dock_leaf, w->id);

            uint32_t active = uiw_dock_leaf_active_win(c, w->dock_leaf);
            if (active && active != w->id)
                return 0;
        }
        else
        {
            w->dock_ds_id = 0;
            w->dock_leaf = 0;
            docked = 0;
        }
    }

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
    int released = ui->mouse_released[0] ? 1 : 0;

    float edge = 6.0f;
    float grip = 16.0f;

    ui_vec4_t header = uiw_header_rect(c, w->rect, flags);
    ui_vec4_t grip_r = uiw_grip_rect(w->rect, grip);

    int header_hit = (!(flags & UI_WIN_NO_TITLEBAR)) && uiw_pt_in(m, header) && (!(flags & UI_WIN_NO_MOVE) || docked);
    int grip_hover = (!docked && !(flags & UI_WIN_NO_RESIZE)) && uiw_pt_in(m, grip_r);

    // If this dock node has tabs, clicks on the tab bar should not start a window drag/undock.
    if (docked && header_hit)
    {
        ui_dock_node_t *leaf = uiw_node(c, w->dock_leaf);
        if (leaf && leaf->tab_count > 1)
        {
            float x = header.x + ui->style.padding;
            for (uint8_t ti = 0; ti < leaf->tab_count; ++ti)
            {
                uint32_t tid = leaf->tabs[ti];
                ui_win_t *tw = uiw_find(c, tid);
                const char *tlabel = (tw && tw->title[0]) ? tw->title : "Tab";
                float tw0 = (float)ui->text_width(ui->text_user, 0, tlabel, -1);
                float tab_w = tw0 + ui->style.padding * 1.5f;
                ui_vec4_t tr = ui_v4(x, header.y + 2.0f, tab_w, header.w - 4.0f);
                if (uiw_pt_in(m, tr))
                {
                    header_hit = 0;
                    if (pressed)
                    {
                        leaf->tab_active = ti;
                        c->tab_drag_active = 1;
                        c->tab_dragging = 0;
                        c->tab_drag_leaf = w->dock_leaf;
                        c->tab_drag_win_id = tid;
                        c->tab_drag_mouse0 = m;
                    }
                    break;
                }
                x += tab_w + ui->style.spacing * 0.5f;
            }
        }
    }

    uint8_t edge_mask = 0;
    int edge_hover = 0;

    if (!docked && !(flags & UI_WIN_NO_RESIZE))
    {
        edge_mask = uiw_hit_edges(m, w->rect, edge);
        edge_hover = edge_mask != 0;
    }

    if (grip_hover)
    {
        ui_request_cursor_state(ui, UI_CURSOR_NORMAL, 90);
        ui_request_cursor_shape(ui, UI_CURSOR_RESIZE_NWSE, 90);
    }
    else if (edge_hover)
    {
        ui_request_cursor_state(ui, UI_CURSOR_NORMAL, 90);
        ui_cursor_shape_t shape = UI_CURSOR_ARROW;
        uint8_t msk = edge_mask;
        if ((msk & (1 | 2)) && (msk & (4 | 8)))
        {
            // Corner.
            if ((msk & 1) && (msk & 4)) shape = UI_CURSOR_RESIZE_NWSE;      // TL
            else if ((msk & 2) && (msk & 8)) shape = UI_CURSOR_RESIZE_NWSE; // BR
            else shape = UI_CURSOR_RESIZE_NESW;                             // TR/BL
        }
        else if (msk & (1 | 2))
            shape = UI_CURSOR_RESIZE_EW;
        else if (msk & (4 | 8))
            shape = UI_CURSOR_RESIZE_NS;
        ui_request_cursor_shape(ui, shape, 90);
    }

    int can_interact = (c->pick_id == id) || (c->drag_id == id);
    if (c->drag_id != 0 && c->drag_id != id)
        can_interact = 0;

    // Don't start window drag/undock while another UI interaction is active (e.g. dock splitter drag).
    if (pressed && can_interact && c->drag_id == 0 && ui->active_id == 0)
    {
        if (header_hit)
        {
            // Enable docking preview during any title-bar drag (floating or docked).
            c->dock_drag_active = 1;
            c->dock_drag_armed = (w->dock_ds_id && w->dock_leaf) ? 1 : 0;
            c->dock_drag_win_id = w->id;
            c->dock_drag_src_ds_id = w->dock_ds_id;
            c->dock_drag_src_leaf = w->dock_leaf;

            if (w->dock_ds_id && w->dock_leaf)
            {
                // Don't undock until the mouse moves past a threshold (ImGui-like).
            }
            else
            {
                c->dock_drag_src_ds_id = 0;
                c->dock_drag_src_leaf = 0;
                c->dock_drag_armed = 0;
            }
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
                ui_request_cursor_state(ui, UI_CURSOR_NORMAL, 80);
                ui_request_cursor_shape(ui, UI_CURSOR_ARROW, 80);
                // If dragging a docked window, only undock after crossing a small drag threshold.
                if (c->dock_drag_active && c->dock_drag_win_id == id && c->dock_drag_src_ds_id != 0 && c->dock_drag_armed)
                {
                    float dx = m.x - c->drag_mouse0.x;
                    float dy = m.y - c->drag_mouse0.y;
                    float thr = 8.0f;
                    if ((dx * dx + dy * dy) >= thr * thr)
                    {
                        ui_win_t *dw = uiw_find(c, id);
                        ui_dockspace_t *src_ds = uiw_find_dockspace(c, c->dock_drag_src_ds_id);
                        uint16_t src_leaf = c->dock_drag_src_leaf;
                        if (dw && src_ds)
                        {
                            uiw_dock_leaf_remove_win(c, src_leaf, dw->id);
                            uiw_dock_prune_empty_leaf(c, src_ds, src_leaf);
                            dw->float_rect = dw->rect;
                            dw->dock_ds_id = 0;
                            dw->dock_leaf = 0;
                            dw->z = uiw_z_max(c) + 1;
                        }
                        docked = 0;
                        c->dock_drag_armed = 0;
                    }
                }

                // Only move floating windows; dock-dragging uses a ghost.
                if (!(c->dock_drag_active && c->dock_drag_src_ds_id != 0))
                {
                    w->float_rect.x = c->drag_rect0.x + d.x;
                    w->float_rect.y = c->drag_rect0.y + d.y;
                    uiw_apply_move_clamp(c, w);
                }

                // Update hover target (also drives overlay preview). On the drop frame, this gets recomputed too.
                if (!(c->dock_drag_active && c->dock_drag_src_ds_id != 0 && c->dock_drag_armed))
                    uiw_update_dock_hover(c, id, m);
            }
            else if (c->dragging_resize && !(flags & UI_WIN_NO_RESIZE))
            {
                ui_request_cursor_state(ui, UI_CURSOR_NORMAL, 90);
                // Use edge mask if present, otherwise fall back to diagonal for the grip.
                ui_cursor_shape_t shape = UI_CURSOR_RESIZE_NWSE;
                uint8_t msk = c->resize_edge_mask;
                if (msk)
                {
                    if ((msk & (1 | 2)) && (msk & (4 | 8)))
                    {
                        if ((msk & 1) && (msk & 4)) shape = UI_CURSOR_RESIZE_NWSE;
                        else if ((msk & 2) && (msk & 8)) shape = UI_CURSOR_RESIZE_NWSE;
                        else shape = UI_CURSOR_RESIZE_NESW;
                    }
                    else if (msk & (1 | 2))
                        shape = UI_CURSOR_RESIZE_EW;
                    else if (msk & (4 | 8))
                        shape = UI_CURSOR_RESIZE_NS;
                }
                else if (!c->resize_using_grip)
                    shape = UI_CURSOR_RESIZE_EW;
                ui_request_cursor_shape(ui, shape, 90);
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

            if (!docked)
                w->rect = w->float_rect;
        }
        else
        {
            // Apply docking on mouse release (dock-dragging), or keep it floating if dropped outside.
            if (released && c->dock_drag_active && c->dock_drag_win_id == id)
            {
                // If we never crossed the undock threshold, treat as a simple click/focus.
                if (c->dock_drag_src_ds_id != 0 && c->dock_drag_armed)
                {
                    c->dock_drag_active = 0;
                    c->dock_drag_armed = 0;
                    c->dock_drag_win_id = 0;
                    c->dock_drag_src_ds_id = 0;
                    c->dock_drag_src_leaf = 0;
                }
                else
                {
                // Recompute hover target on the release frame (begin_frame resets these each frame).
                uiw_update_dock_hover(c, id, m);
                if (c->dock_widget_active && c->dock_widget_win_id == id && c->dock_widget_dockspace_id != 0)
                {
                    ui_dockspace_t *ds = uiw_find_dockspace(c, c->dock_widget_dockspace_id);
                    if (ds && ds->root)
                    {
                        uiw_dock_window(c, w, ds, c->dock_widget_leaf, c->dock_widget_slot);
                        docked = (w->dock_ds_id != 0 && w->dock_leaf != 0) ? 1 : 0;
                        if (docked)
                        {
                            ui_vec4_t leaf_r = ds->rect;
                            if (uiw_dock_leaf_rect(c, ds->root, ds->rect, w->dock_leaf, &leaf_r))
                                w->rect = leaf_r;
                        }
                    }
                }
                else
                {
                    // Dropped outside docking target:
                    // - If it came from a docked leaf, place it near the cursor.
                    // - If it was already floating, keep the moved floating rect.
                    if (c->dock_drag_src_ds_id != 0)
                    {
                        w->float_rect.x = m.x - w->float_rect.z * 0.5f;
                        w->float_rect.y = m.y - (ui->style.line_h * 0.5f);
                        uiw_apply_move_clamp(c, w);
                    }
                }

                c->dock_drag_active = 0;
                c->dock_drag_armed = 0;
                c->dock_drag_win_id = 0;
                c->dock_drag_src_ds_id = 0;
                c->dock_drag_src_leaf = 0;
                }
            }

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
    if (c->pick_id == id)
        w->z = uiw_z_max(c) + 1;
    ui->window_accept_input = (c->hovered_id == 0) || (c->hovered_id == id) || (ui->active_id != 0);

    w->cmd_start = ui->stream.count;

    // While dock-dragging, render a lightweight "ghost" window under the cursor.
    if (c->dock_drag_active && c->dock_drag_win_id == id && c->dock_drag_src_ds_id != 0)
    {
        float gw = w->float_rect.z;
        float gh = w->float_rect.w;
        if (gw < 120.0f) gw = 120.0f;
        if (gh < 80.0f) gh = 80.0f;
        w->rect = ui_v4(m.x - gw * 0.5f, m.y - ui->style.line_h * 0.5f, gw, gh);
        ui_draw_rect(ui, w->rect, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.25f), ui->style.window_corner, 0.0f);
        ui_draw_rect(ui, w->rect, ui_color(ui->style.accent.rgb, 0.55f), ui->style.window_corner, 1.0f);

        // Don't emit content while dragging.
        c->cur_id = 0;
        c->cur_rect = ui_v4(0, 0, 0, 0);
        c->cur_flags = UI_WIN_NONE;
        return 0;
    }

    ui_begin_panel(ui, w->rect);

    if (!(flags & UI_WIN_NO_TITLEBAR))
    {
        ui_vec4_t hdr = uiw_header_rect(c, w->rect, flags);

        ui_draw_rect(ui, hdr, ui_color(ui_v3(0.10f, 0.10f, 0.11f), 1.0f), 0.0f, 0.0f);
        ui_draw_rect(ui, hdr, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.35f), 0.0f, 1.0f);

        ui_dock_node_t *leaf = docked ? uiw_node(c, w->dock_leaf) : NULL;
        if (leaf && leaf->tab_count > 1)
        {
            // Drag-to-reorder tabs (ImGui-like).
            if (c->tab_drag_active && c->tab_drag_leaf == w->dock_leaf && ui->mouse_down[0])
            {
                float dx = ui->mouse.x - c->tab_drag_mouse0.x;
                float dy = ui->mouse.y - c->tab_drag_mouse0.y;
                float thr = 6.0f;
                if (!c->tab_dragging && (dx * dx + dy * dy) >= thr * thr)
                    c->tab_dragging = 1;
            }
            if (c->tab_drag_active && c->tab_drag_leaf == w->dock_leaf && ui->mouse_released[0])
            {
                c->tab_drag_active = 0;
                c->tab_dragging = 0;
                c->tab_drag_leaf = 0;
                c->tab_drag_win_id = 0;
            }

            float x = hdr.x + ui->style.padding;
            float th = ui->text_height(ui->text_user, 0);
            float ty = hdr.y + (hdr.w - th) * 0.5f;

            uint8_t hover_index = 255;
            uint8_t first_index = 255;
            uint8_t last_index = 0;
            float first_x = 0.0f;
            float last_x1 = 0.0f;

            for (uint8_t ti = 0; ti < leaf->tab_count; ++ti)
            {
                uint32_t tid = leaf->tabs[ti];
                ui_win_t *tw = uiw_find(c, tid);
                const char *tlabel = (tw && tw->title[0]) ? tw->title : "Tab";
                int tlen = -1;
                float tw0 = (float)ui->text_width(ui->text_user, 0, tlabel, tlen);
                float tab_w = tw0 + ui->style.padding * 1.5f;
                ui_vec4_t tr = ui_v4(x, hdr.y + 2.0f, tab_w, hdr.w - 4.0f);

                int hot = uiw_pt_in(m, tr);
                int active = (ti == leaf->tab_active) ? 1 : 0;
                ui_color_t bg = active ? ui->style.header_bg_active : ui->style.header_bg;
                if (hot && ui->window_accept_input)
                    bg = ui->style.btn_hover;

                ui_draw_rect(ui, tr, bg, 6.0f, 0.0f);
                if (hot && ui->window_accept_input && pressed)
                {
                    leaf->tab_active = ti;
                    c->tab_drag_active = 1;
                    c->tab_dragging = 0;
                    c->tab_drag_leaf = w->dock_leaf;
                    c->tab_drag_win_id = tid;
                    c->tab_drag_mouse0 = m;
                }

                ui_draw_text(ui, ui_v2(tr.x + ui->style.padding * 0.6f, ty), ui->style.header_text, 0, tlabel);

                if (ti == 0)
                {
                    first_index = 0;
                    first_x = tr.x;
                }
                last_index = ti;
                last_x1 = tr.x + tr.z;

                if (c->tab_drag_active && c->tab_drag_leaf == w->dock_leaf && c->tab_dragging && uiw_pt_in(m, tr))
                    hover_index = ti;

                x += tab_w + ui->style.spacing * 0.5f;
            }

            // Tear-out: drag the active tab outside the tab bar to undock into a floating window.
            if (c->tab_drag_active && c->tab_drag_leaf == w->dock_leaf && c->tab_dragging && ui->mouse_down[0])
            {
                ui_vec4_t loose = ui_v4(hdr.x - 12.0f, hdr.y - 18.0f, hdr.z + 24.0f, hdr.w + 30.0f);
                if (!uiw_pt_in(m, loose))
                {
                    ui_win_t *dw = uiw_find(c, c->tab_drag_win_id);
                    ui_dockspace_t *ds = uiw_find_dockspace(c, w->dock_ds_id);
                    if (dw && ds)
                    {
                        uint16_t src_leaf = w->dock_leaf;
                        uiw_dock_leaf_remove_win(c, src_leaf, dw->id);
                        uiw_dock_prune_empty_leaf(c, ds, src_leaf);

                        // Convert to floating at the dock rect, then start dragging.
                        dw->float_rect = dw->rect;
                        dw->dock_ds_id = 0;
                        dw->dock_leaf = 0;
                        dw->z = uiw_z_max(c) + 1;

                        c->drag_id = dw->id;
                        c->dragging_move = 1;
                        c->dragging_resize = 0;
                        c->drag_mouse0 = m;
                        c->drag_rect0 = dw->float_rect;
                        c->resize_using_grip = 0;
                        c->resize_edge_mask = 0;

                        // Enable docking preview/drop for this drag as if it were a title-bar drag.
                        c->dock_drag_active = 1;
                        c->dock_drag_armed = 0;
                        c->dock_drag_win_id = dw->id;
                        c->dock_drag_src_ds_id = 0;
                        c->dock_drag_src_leaf = 0;

                        c->tab_drag_active = 0;
                        c->tab_dragging = 0;
                        c->tab_drag_leaf = 0;
                        c->tab_drag_win_id = 0;
                    }
                }
            }

            if (c->tab_drag_active && c->tab_drag_leaf == w->dock_leaf && c->tab_dragging && leaf->tab_count > 1)
            {
                // Clamp to ends if outside the tab row.
                if (hover_index == 255)
                {
                    if (m.x < first_x)
                        hover_index = first_index;
                    else if (m.x > last_x1)
                        hover_index = last_index;
                }

                if (hover_index != 255)
                {
                    uint8_t from = 255;
                    for (uint8_t i = 0; i < leaf->tab_count; ++i)
                        if (leaf->tabs[i] == c->tab_drag_win_id)
                        {
                            from = i;
                            break;
                        }
                    if (from != 255 && from != hover_index)
                        uiw_tab_move(leaf, from, hover_index);
                }
            }
        }
        else
        {
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

        float slack = ui->style.padding * 0.5f;
        float max_scroll = w->content_h_last - cr.w + slack;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        if (w->scroll_y < 0.0f) w->scroll_y = 0.0f;
        if (w->scroll_y > max_scroll) w->scroll_y = max_scroll;

        int mouse_over = uiw_pt_in(ui->mouse, w->rect);
        int allow_scroll = mouse_over && (ui_window_hovered_id(ui) == w->id || ui_window_hovered_id(ui) == 0);
        if (!ui->scroll_used && allow_scroll && ui->io.mouse_scroll.y != 0.0f && w->content_h_last > cr.w + 0.5f)
        {
            float step = ui->style.line_h * 2.0f;
            w->scroll_y -= ui->io.mouse_scroll.y * step;
            float max_s = ui_maxf(0.0f, w->content_h_last - cr.w + slack);
            if (w->scroll_y < 0.0f) w->scroll_y = 0.0f;
            if (w->scroll_y > max_s) w->scroll_y = max_s;
            ui->scroll_used = 1;
        }

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
            c->batches[c->batch_count].z = uiw_effective_z(w);
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
    {
        top = ui->style.line_h + ui->style.padding * 0.35f;
        if (top < ui->style.line_h + 6.0f)
            top = ui->style.line_h + 6.0f;
    }

    ui_vec4_t r = c->cur_rect;
    return ui_v4(r.x + pad, r.y + top + pad, r.z - pad * 2.0f, r.w - top - pad * 2.0f);
}

uint32_t ui_window_hovered_id(ui_ctx_t *ui)
{
    ui_win_ctx_t *c = uiw_ctx(ui);
    return c->hovered_id;
}

static void uiw_save_dock_node_v2(FILE *f, ui_win_ctx_t *c, uint16_t idx, int depth)
{
    if (!f || !c || idx == 0)
        return;
    ui_dock_node_t *n = uiw_node(c, idx);
    if (!n)
        return;

    int indent = depth * 2;

    if (n->split == UI_DOCKSPLIT_NONE)
    {
        fprintf(f, "%*sLEAF %u %u", indent, "", (unsigned)n->tab_active, (unsigned)n->tab_count);
        for (uint8_t t = 0; t < n->tab_count; ++t)
            fprintf(f, " %u", (unsigned)n->tabs[t]);
        fprintf(f, "\n");
        return;
    }

    const char *s = (n->split == UI_DOCKSPLIT_VERT) ? "V" : "H";
    fprintf(f, "%*sSPLIT %s %.6f\n", indent, "", s, n->ratio);
    fprintf(f, "%*s{\n", indent, "");
    uiw_save_dock_node_v2(f, c, n->a, depth + 1);
    uiw_save_dock_node_v2(f, c, n->b, depth + 1);
    fprintf(f, "%*s}\n", indent, "");
}

static int uiw_tok_next(FILE *f, char *out, size_t cap)
{
    if (!f || !out || cap == 0)
        return 0;

    int ch = 0;
    do
    {
        ch = fgetc(f);
        if (ch == EOF)
            return 0;
    } while (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n');

    if (ch == '{' || ch == '}')
    {
        out[0] = (char)ch;
        out[1] = 0;
        return 1;
    }

    if (ch == '"')
    {
        size_t n = 0;
        while ((ch = fgetc(f)) != EOF && ch != '"')
        {
            if (n + 1 < cap)
                out[n++] = (char)ch;
        }
        out[n] = 0;
        return 1;
    }

    size_t n = 0;
    for (;;)
    {
        if (ch == EOF)
            break;
        if (ch == '{' || ch == '}' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
        {
            if (ch == '{' || ch == '}')
                ungetc(ch, f);
            break;
        }
        if (n + 1 < cap)
            out[n++] = (char)ch;
        ch = fgetc(f);
    }
    out[n] = 0;
    return 1;
}

static int uiw_tok_expect(FILE *f, const char *want)
{
    char tok[128];
    if (!uiw_tok_next(f, tok, sizeof(tok)))
        return 0;
    return strcmp(tok, want) == 0;
}

static int uiw_tok_u32(FILE *f, uint32_t *out)
{
    char tok[128];
    if (!uiw_tok_next(f, tok, sizeof(tok)))
        return 0;
    if (out)
        *out = (uint32_t)strtoul(tok, 0, 10);
    return 1;
}

static int uiw_tok_i32(FILE *f, int *out)
{
    char tok[128];
    if (!uiw_tok_next(f, tok, sizeof(tok)))
        return 0;
    if (out)
        *out = (int)strtol(tok, 0, 10);
    return 1;
}

static int uiw_tok_f32(FILE *f, float *out)
{
    char tok[128];
    if (!uiw_tok_next(f, tok, sizeof(tok)))
        return 0;
    if (out)
        *out = (float)strtof(tok, 0);
    return 1;
}

static ui_dockspace_t *uiw_get_dockspace_id(ui_win_ctx_t *c, uint32_t id)
{
    if (!c || id == 0)
        return 0;

    for (uint32_t i = 0; i < c->dockspace_count; ++i)
        if (c->dockspaces[i].used && c->dockspaces[i].id == id)
            return &c->dockspaces[i];

    for (uint32_t i = 0; i < 32; ++i)
    {
        if (!c->dockspaces[i].used)
        {
            ui_dockspace_t *ds = &c->dockspaces[i];
            memset(ds, 0, sizeof(*ds));
            ds->used = 1;
            ds->id = id;
            ds->seen = 0;
            ds->rect = ui_v4(0, 0, 0, 0);
            ds->root = 0;
            if (i >= c->dockspace_count)
                c->dockspace_count = i + 1;
            return ds;
        }
    }

    return &c->dockspaces[0];
}

static uint16_t uiw_load_dock_node_v2(ui_win_ctx_t *c, ui_dockspace_t *ds, FILE *f)
{
    if (!c || !ds || !f)
        return 0;

    char tok[128];
    if (!uiw_tok_next(f, tok, sizeof(tok)))
        return 0;

    if (strcmp(tok, "LEAF") == 0)
    {
        uint32_t active = 0, count = 0;
        if (!uiw_tok_u32(f, &active))
            return 0;
        if (!uiw_tok_u32(f, &count))
            return 0;

        uint16_t idx = uiw_node_alloc(c);
        ui_dock_node_t *n = uiw_node(c, idx);
        if (!n)
            return 0;
        n->split = UI_DOCKSPLIT_NONE;
        n->ratio = 0.5f;
        n->a = 0;
        n->b = 0;
        n->tab_count = 0;
        n->tab_active = 0;

        uint32_t keep = count > 8 ? 8 : count;
        n->tab_count = (uint8_t)keep;
        n->tab_active = (keep > 0 && active < keep) ? (uint8_t)active : 0;

        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t tid = 0;
            if (!uiw_tok_u32(f, &tid))
                tid = 0;
            if (i < keep)
                n->tabs[i] = tid;

            if (tid != 0)
            {
                ui_win_t *w = uiw_get(c, tid);
                if (w)
                {
                    w->dock_ds_id = ds->id;
                    w->dock_leaf = idx;
                }
            }
        }

        return idx;
    }

    if (strcmp(tok, "SPLIT") == 0)
    {
        char axis[16];
        if (!uiw_tok_next(f, axis, sizeof(axis)))
            return 0;
        float ratio = 0.5f;
        if (!uiw_tok_f32(f, &ratio))
            return 0;

        uint16_t idx = uiw_node_alloc(c);
        ui_dock_node_t *n = uiw_node(c, idx);
        if (!n)
            return 0;

        n->split = (axis[0] == 'V' || axis[0] == 'v') ? UI_DOCKSPLIT_VERT : UI_DOCKSPLIT_HORZ;
        n->ratio = ratio;
        n->tab_count = 0;
        n->tab_active = 0;

        if (!uiw_tok_expect(f, "{"))
            return idx;

        n->a = uiw_load_dock_node_v2(c, ds, f);
        n->b = uiw_load_dock_node_v2(c, ds, f);
        (void)uiw_tok_expect(f, "}");
        return idx;
    }

    return 0;
}

int ui_dock_save_layout_file(ui_ctx_t *ui, const char *path)
{
    if (!ui || !path || !path[0])
        return 0;

    ui_win_ctx_t *c = uiw_ctx(ui);
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;

    // Version 2: explicit dockspace tree format.
    fprintf(f, "UI_DOCK_LAYOUT 2\n");
    fprintf(f, "DEFAULT_DS %u\n", (unsigned)c->default_dockspace_id);

    for (uint32_t i = 0; i < c->win_count; ++i)
    {
        ui_win_t *w = &c->wins[i];
        if (!w->used)
            continue;
        // Keep titles for debugging; the ID is still the key.
        char safe_title[64];
        strncpy(safe_title, w->title, sizeof(safe_title));
        safe_title[sizeof(safe_title) - 1] = 0;
        for (size_t k = 0; safe_title[k]; ++k)
            if (safe_title[k] == '"')
                safe_title[k] = '\'';

        fprintf(f, "WIN %u %.3f %.3f %.3f %.3f %d \"%s\"\n",
                (unsigned)w->id,
                w->float_rect.x, w->float_rect.y, w->float_rect.z, w->float_rect.w,
                w->z,
                safe_title);
    }

    // Dockspaces and their docking trees.
    for (uint32_t i = 0; i < c->dockspace_count; ++i)
    {
        ui_dockspace_t *ds = &c->dockspaces[i];
        if (!ds->used || !ds->root || !uiw_node(c, ds->root))
            continue;

        fprintf(f, "DOCKSPACE %u\n", (unsigned)ds->id);
        uiw_save_dock_node_v2(f, c, ds->root, 1);
    }

    fclose(f);
    return 1;
}

int ui_dock_load_layout_file(ui_ctx_t *ui, const char *path)
{
    if (!ui || !path || !path[0])
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    char hdr[32] = {0};
    int ver = 0;
    if (fscanf(f, "%31s %d", hdr, &ver) != 2 || strcmp(hdr, "UI_DOCK_LAYOUT") != 0 || (ver != 1 && ver != 2))
    {
        fclose(f);
        return 0;
    }

    ui_win_ctx_t *c = uiw_ctx(ui);
    memset(c->dockspaces, 0, sizeof(c->dockspaces));
    c->dockspace_count = 0;
    c->default_dockspace_id = 0;
    memset(c->nodes, 0, sizeof(c->nodes));
    c->node_count = 0;
    for (uint32_t i = 0; i < c->win_count; ++i)
    {
        if (c->wins[i].used)
        {
            c->wins[i].dock_ds_id = 0;
            c->wins[i].dock_leaf = 0;
        }
    }

    if (ver == 2)
    {
        // After fscanf, the file cursor is positioned after the version integer; continue with token parsing.
        char tok[128];
        while (uiw_tok_next(f, tok, sizeof(tok)))
        {
            if (strcmp(tok, "DEFAULT_DS") == 0)
            {
                uint32_t id = 0;
                if (!uiw_tok_u32(f, &id))
                    break;
                c->default_dockspace_id = id;
            }
            else if (strcmp(tok, "WIN") == 0)
            {
                uint32_t id = 0;
                float x = 0, y = 0, w0 = 0, h0 = 0;
                int z = 0;
                char title[128] = {0};
                if (!uiw_tok_u32(f, &id)) break;
                if (!uiw_tok_f32(f, &x)) break;
                if (!uiw_tok_f32(f, &y)) break;
                if (!uiw_tok_f32(f, &w0)) break;
                if (!uiw_tok_f32(f, &h0)) break;
                if (!uiw_tok_i32(f, &z)) break;
                // title is optional
                (void)uiw_tok_next(f, title, sizeof(title));

                ui_win_t *w = uiw_get(c, id);
                if (w)
                {
                    w->float_rect = ui_v4(x, y, w0, h0);
                    w->rect = w->float_rect;
                    w->z = z;
                }
            }
            else if (strcmp(tok, "DOCKSPACE") == 0)
            {
                uint32_t id = 0;
                if (!uiw_tok_u32(f, &id))
                    break;
                ui_dockspace_t *ds = uiw_get_dockspace_id(c, id);
                if (!ds)
                    break;
                ds->root = uiw_load_dock_node_v2(c, ds, f);
            }
            else
            {
                // Skip unknown tokens.
            }
        }

        fclose(f);
        return 1;
    }

    char tok[32];
    while (fscanf(f, "%31s", tok) == 1)
    {
        if (strcmp(tok, "WIN") == 0)
        {
            unsigned id = 0, dsid = 0, leaf = 0;
            float x = 0, y = 0, w0 = 0, h0 = 0;
            int z = 0;
            if (fscanf(f, "%u %f %f %f %f %u %u %d", &id, &x, &y, &w0, &h0, &dsid, &leaf, &z) != 8)
                break;

            ui_win_t *w = uiw_get(c, (uint32_t)id);
            if (w)
            {
                w->float_rect = ui_v4(x, y, w0, h0);
                w->rect = w->float_rect;
                w->dock_ds_id = (uint32_t)dsid;
                w->dock_leaf = (uint16_t)leaf;
                w->z = z;
            }
        }
        else if (strcmp(tok, "DS") == 0)
        {
            unsigned id = 0, root = 0;
            if (fscanf(f, "%u %u", &id, &root) != 2)
                break;

            ui_dockspace_t *ds = 0;
            for (uint32_t i = 0; i < 32; ++i)
            {
                if (!c->dockspaces[i].used)
                {
                    ds = &c->dockspaces[i];
                    memset(ds, 0, sizeof(*ds));
                    ds->used = 1;
                    ds->id = (uint32_t)id;
                    ds->root = (uint16_t)root;
                    ds->seen = 0;
                    ds->rect = ui_v4(0, 0, 0, 0);
                    if (i >= c->dockspace_count)
                        c->dockspace_count = i + 1;
                    break;
                }
            }
            (void)ds;
        }
        else if (strcmp(tok, "DEFAULT_DS") == 0)
        {
            unsigned id = 0;
            if (fscanf(f, "%u", &id) != 1)
                break;
            c->default_dockspace_id = (uint32_t)id;
        }
        else if (strcmp(tok, "NODE") == 0)
        {
            unsigned idx = 0, split = 0, a = 0, b = 0, tab_active = 0, tab_count = 0;
            float ratio = 0.5f;
            if (fscanf(f, "%u %u %f %u %u %u %u", &idx, &split, &ratio, &a, &b, &tab_active, &tab_count) != 7)
                break;
            if (idx == 0 || idx >= 256)
            {
                // Skip invalid index; also skip tab ids if present.
                for (unsigned t = 0; t < tab_count; ++t)
                {
                    unsigned tmp = 0;
                    fscanf(f, "%u", &tmp);
                }
                continue;
            }

            ui_dock_node_t *n = &c->nodes[idx];
            memset(n, 0, sizeof(*n));
            n->used = 1;
            n->split = (uint8_t)split;
            n->ratio = ratio;
            n->a = (uint16_t)a;
            n->b = (uint16_t)b;
            n->tab_active = (uint8_t)tab_active;
            if (tab_count > 8)
                tab_count = 8;
            n->tab_count = (uint8_t)tab_count;
            for (unsigned t = 0; t < tab_count; ++t)
            {
                unsigned tid = 0;
                if (fscanf(f, "%u", &tid) != 1)
                    tid = 0;
                n->tabs[t] = (uint32_t)tid;
            }

            if (idx >= c->node_count)
                c->node_count = (uint16_t)(idx + 1);
        }
        else
        {
            // Unknown token: skip rest of line.
            int ch = 0;
            while ((ch = fgetc(f)) != '\n' && ch != EOF) {}
        }
    }

    fclose(f);
    return 1;
}
