#include "ui_popup.h"
#include "ui_widgets.h"
#include <string.h>
#include <stdlib.h>

#ifndef UI_KEY_ESCAPE
#define UI_KEY_ESCAPE 256
#endif

static float ui_popup_outline_th(ui_ctx_t *ui)
{
    float t = ui->style.outline_thickness;
    if (t <= 0.0f)
        t = 1.0f;
    return t;
}

typedef struct uiw_popup_state_t
{
    uint32_t open_id;
    uint32_t active_id;
    uint8_t open;
    ui_vec4_t rect;
} uiw_popup_state_t;

typedef struct uiw_popup_frame_t
{
    ui_layout_t saved_layout;
    int saved_clip_top;
    uint32_t cmd_start;
} uiw_popup_frame_t;

typedef struct uiw_popup_ctx_t
{
    ui_ctx_t *ui;
    uiw_popup_state_t popup;
    uiw_popup_frame_t stack[8];
    int top;
} uiw_popup_ctx_t;

static uiw_popup_ctx_t g_popup_ctxs[8];

static uiw_popup_ctx_t *uiw_popup_ctx(ui_ctx_t *ui)
{
    for (int i = 0; i < 8; ++i)
        if (g_popup_ctxs[i].ui == ui)
            return &g_popup_ctxs[i];
    for (int i = 0; i < 8; ++i)
        if (!g_popup_ctxs[i].ui)
        {
            memset(&g_popup_ctxs[i], 0, sizeof(g_popup_ctxs[i]));
            g_popup_ctxs[i].ui = ui;
            return &g_popup_ctxs[i];
        }
    return &g_popup_ctxs[0];
}

static uiw_popup_state_t *uiw_popup(ui_ctx_t *ui)
{
    return &uiw_popup_ctx(ui)->popup;
}

void ui_open_popup(ui_ctx_t *ui, const char *id)
{
    uiw_popup_state_t *p = uiw_popup(ui);
    p->open_id = ui_id_str(ui, id ? id : "##popup");
    p->active_id = p->open_id;
    p->open = 1;
    p->rect = ui_v4(0, 0, 0, 0);
}

void ui_close_popup(ui_ctx_t *ui)
{
    uiw_popup_state_t *p = uiw_popup(ui);
    p->open = 0;
    p->active_id = 0;
}

int ui_begin_popup(ui_ctx_t *ui, const char *id)
{
    uiw_popup_ctx_t *pc = uiw_popup_ctx(ui);
    uiw_popup_state_t *p = &pc->popup;
    uint32_t pid = ui_id_str(ui, id ? id : "##popup");
    if (!p->open || p->active_id != pid)
        return 0;

    ui_vec4_t anchor = ui_layout_peek_last(&ui->layout);

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
        ui_close_popup(ui);
        return 0;
    }

    if (ui->key_pressed[UI_KEY_ESCAPE])
    {
        ui_close_popup(ui);
        return 0;
    }

    if (pc->top >= 8)
        return 0;
    uiw_popup_frame_t *pf = &pc->stack[pc->top++];
    pf->saved_layout = ui->layout;
    pf->saved_clip_top = ui->clip_top;
    pf->cmd_start = ui_cmd_count(&ui->stream);

    ui_draw_rect(ui, r, ui->style.window_bg.a > 0.001f ? ui->style.window_bg : ui->style.panel_bg, ui->style.window_corner, 0.0f);
    ui_draw_rect(ui, r, ui->style.outline, ui->style.window_corner, ui_popup_outline_th(ui));

    ui_vec4_t cr = ui_v4(r.x + ui->style.padding, r.y + ui->style.padding, r.z - ui->style.padding * 2.0f, r.w - ui->style.padding * 2.0f);

    ui_push_clip(ui, cr);
    ui_layout_begin(&ui->layout, cr, 0.0f);
    ui_layout_row(&ui->layout, ui->style.line_h, 1, 0, ui->style.spacing);
    ui->window_accept_input = 1;

    return 1;
}

void ui_end_popup(ui_ctx_t *ui)
{
    uiw_popup_ctx_t *pc = uiw_popup_ctx(ui);
    if (pc->top <= 0)
        return;

    ui_pop_clip(ui);
    uiw_popup_frame_t *pf = &pc->stack[--pc->top];
    ui->layout = pf->saved_layout;
    while (ui->clip_top > pf->saved_clip_top)
        ui_pop_clip(ui);

    /* Move popup commands to the end to render on top. */
    uint32_t start = pf->cmd_start;
    uint32_t end = ui_cmd_count(&ui->stream);
    if (start < end)
    {
        uint32_t n = end - start;
        ui_cmd_t *cmds = ui->stream.cmds;
        if (cmds && n > 0)
        {
            ui_cmd_t *tmp = (ui_cmd_t *)malloc(sizeof(ui_cmd_t) * n);
            if (tmp)
            {
                memcpy(tmp, cmds + start, sizeof(ui_cmd_t) * n);
                uint32_t tail = ui->stream.count - end;
                memmove(cmds + start, cmds + end, sizeof(ui_cmd_t) * tail);
                ui->stream.count -= n;
                memcpy(cmds + ui->stream.count, tmp, sizeof(ui_cmd_t) * n);
                ui->stream.count += n;
                free(tmp);
            }
        }
    }
}
