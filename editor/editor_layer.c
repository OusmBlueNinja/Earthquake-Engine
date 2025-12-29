#include "editor_layer.h"
#include "core/core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <GL/glew.h>

#include "cvar.h"

#include "ui/ui.h"
#include "ui/ui_widgets.h"
#include "ui/ui_events.h"
#include "ui/ui_window.h"
#include "ui/ui_demo.h"
#include "ui/backends/opengl_backend.h"

#include "asset_manager/asset_manager.h"
#include "vector.h"

static void *ui_editor_realloc(void *user, void *ptr, uint32_t size)
{
    (void)user;
    if (size == 0)
    {
        free(ptr);
        return 0;
    }
    return realloc(ptr, (size_t)size);
}

typedef struct editor_layer_data_t
{
    ui_ctx_t ui;
    ui_gl_backend_t glui;

    int inited;
    vec2i last_fb;

    int first_frame;

    float dt;
    float fps;

    ui_event_t queued[128];
    uint32_t queued_count;

    vec2i last_scene_size;

    int am_show_modules;
    int am_show_slots;
    int am_show_queues;

    int asset_slot_pick;

    int show_ui_demo;
} editor_layer_data_t;

static editor_layer_data_t *layer_data(layer_t *layer)
{
    return (editor_layer_data_t *)layer->data;
}

static uint32_t editor_resolve_tex_from_fbo_or_tex(uint32_t id)
{
    if (id == 0)
        return 0;

    if (glIsFramebuffer((GLuint)id))
    {
        GLint prev = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)id);

        GLint obj_type = 0;
        GLint obj_name = 0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &obj_type);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &obj_name);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev);

        if (obj_type == GL_TEXTURE)
            return (uint32_t)obj_name;
        return 0;
    }

    if (glIsTexture((GLuint)id))
        return id;

    return 0;
}

static void editor_draw_kv_row(ui_ctx_t *ui, const char *key, const char *val, float key_w)
{
    float lh = ui->text_height(ui->text_user, 0);
    float row_h = ui->style.line_h;
    if (row_h < lh)
        row_h = lh;

    float widths[2];
    widths[0] = key_w;
    widths[1] = 0.0f;

    ui_layout_row(&ui->layout, row_h, 2, widths, ui->style.spacing);

    ui_vec4_t rk = ui_layout_next(&ui->layout, ui->style.spacing);
    ui_vec4_t rv = ui_layout_next(&ui->layout, ui->style.spacing);

    float ky = rk.y + (rk.w - lh) * 0.5f;
    float vy = rv.y + (rv.w - lh) * 0.5f;

    ui_draw_text(ui, ui_v2(rk.x, ky), ui_color(ui->style.text.rgb, 0.78f), 0, key);
    ui_draw_text(ui, ui_v2(rv.x, vy), ui_color(ui->style.text.rgb, 0.95f), 0, val);
}

static void editor_ui_flush_events(editor_layer_data_t *d)
{
    for (uint32_t i = 0; i < d->queued_count; ++i)
        ui_on_event(&d->ui, &d->queued[i]);
    d->queued_count = 0;
}

static void editor_ui_queue_event(editor_layer_data_t *d, const ui_event_t *e)
{
    if (d->queued_count >= (uint32_t)(sizeof(d->queued) / sizeof(d->queued[0])))
        return;
    d->queued[d->queued_count++] = *e;
}

static uint32_t editor_asset_used_slots(const asset_manager_t *am)
{
    if (!am)
        return 0;

    uint32_t used = 0;

    asset_slot_t *it = 0;
    VECTOR_FOR_EACH(am->slots, asset_slot_t, it)
    {
        if (it && it->generation != 0)
            used += 1;
    }
    return used;
}

static uint32_t editor_guess_asset_handle_u32(uint32_t slot_index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (slot_index & 0xFFFFu);
}

static void editor_am_draw_overview(editor_layer_data_t *d, const asset_manager_t *am, float key_w)
{
    ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
    ui_separator_text(&d->ui, "Asset Manager Overview", 0);

    char v[128];

    snprintf(v, sizeof(v), "%p", (void *)am);
    editor_draw_kv_row(&d->ui, "ptr", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)am->worker_count);
    editor_draw_kv_row(&d->ui, "worker_count", v, key_w);

    snprintf(v, sizeof(v), "%p", (void *)am->workers);
    editor_draw_kv_row(&d->ui, "workers_ptr", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)am->handle_type);
    editor_draw_kv_row(&d->ui, "handle_type", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)am->shutting_down);
    editor_draw_kv_row(&d->ui, "shutting_down", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)(uint32_t)am->modules.size);
    editor_draw_kv_row(&d->ui, "modules", v, key_w);

    {
        uint32_t total = (uint32_t)am->slots.size;
        uint32_t used = editor_asset_used_slots(am);
        snprintf(v, sizeof(v), "%u / %u", (unsigned)used, (unsigned)total);
        editor_draw_kv_row(&d->ui, "loaded_slots", v, key_w);
    }
}

static void editor_am_draw_queues(editor_layer_data_t *d, const asset_manager_t *am, float key_w)
{
    char v[128];

    snprintf(v, sizeof(v), "%u", (unsigned)am->jobs.count);
    editor_draw_kv_row(&d->ui, "jobs.count", v, key_w);
    snprintf(v, sizeof(v), "%u", (unsigned)am->jobs.cap);
    editor_draw_kv_row(&d->ui, "jobs.cap", v, key_w);
    snprintf(v, sizeof(v), "%u", (unsigned)am->jobs.head);
    editor_draw_kv_row(&d->ui, "jobs.head", v, key_w);
    snprintf(v, sizeof(v), "%u", (unsigned)am->jobs.tail);
    editor_draw_kv_row(&d->ui, "jobs.tail", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)am->done.count);
    editor_draw_kv_row(&d->ui, "done.count", v, key_w);
    snprintf(v, sizeof(v), "%u", (unsigned)am->done.cap);
    editor_draw_kv_row(&d->ui, "done.cap", v, key_w);
    snprintf(v, sizeof(v), "%u", (unsigned)am->done.head);
    editor_draw_kv_row(&d->ui, "done.head", v, key_w);
    snprintf(v, sizeof(v), "%u", (unsigned)am->done.tail);
    editor_draw_kv_row(&d->ui, "done.tail", v, key_w);
}

static void editor_am_draw_modules(editor_layer_data_t *d, const asset_manager_t *am, float key_w)
{
    uint32_t mcount = (uint32_t)am->modules.size;
    if (mcount == 0)
    {
        editor_draw_kv_row(&d->ui, "modules", "(none)", key_w);
        return;
    }

    for (uint32_t i = 0; i < mcount; ++i)
    {
        const asset_module_desc_t *m = (const asset_module_desc_t *)((uint8_t *)am->modules.data + (size_t)i * (size_t)am->modules.element_size);
        if (!m)
            continue;

        const char *nm = m->name ? m->name : "(null)";

        char key[64];
        char val[256];

        snprintf(key, sizeof(key), "%u", (unsigned)i);
        snprintf(val, sizeof(val), "name=%s  type=%d", nm, (int)m->type);

        editor_draw_kv_row(&d->ui, key, val, key_w);
    }
}

static void editor_assets_draw_picker_and_preview(editor_layer_data_t *d, const asset_manager_t *am, float key_w)
{
    uint32_t scount = (uint32_t)am->slots.size;

    ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
    ui_separator_text(&d->ui, "Asset Inspector", 0);

    if (scount == 0)
    {
        editor_draw_kv_row(&d->ui, "slots", "(none)", key_w);
        return;
    }

    int maxv = (int)scount - 1;
    if (d->asset_slot_pick < 0)
        d->asset_slot_pick = 0;
    if (d->asset_slot_pick > maxv)
        d->asset_slot_pick = maxv;

    ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
    ui_slider_int(&d->ui, "slot_index", 0, &d->asset_slot_pick, 0, maxv);

    uint32_t idx = (uint32_t)d->asset_slot_pick;
    const asset_slot_t *s = (const asset_slot_t *)((uint8_t *)am->slots.data + (size_t)idx * (size_t)am->slots.element_size);
    if (!s)
    {
        editor_draw_kv_row(&d->ui, "slot", "null", key_w);
        return;
    }

    char v[128];

    snprintf(v, sizeof(v), "%u", (unsigned)idx);
    editor_draw_kv_row(&d->ui, "id", v, key_w);

    snprintf(v, sizeof(v), "0x%08X", (unsigned)editor_guess_asset_handle_u32(idx, s->generation));
    editor_draw_kv_row(&d->ui, "asset_handle", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)s->generation);
    editor_draw_kv_row(&d->ui, "generation", v, key_w);

    snprintf(v, sizeof(v), "%u", (unsigned)s->module_index);
    editor_draw_kv_row(&d->ui, "module_index", v, key_w);

    snprintf(v, sizeof(v), "%d", (int)s->asset.type);
    editor_draw_kv_row(&d->ui, "asset.type", v, key_w);

    snprintf(v, sizeof(v), "%d", (int)s->asset.state);
    editor_draw_kv_row(&d->ui, "asset.state", v, key_w);

    ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
    ui_separator(&d->ui);

    ui_vec4_t cr = ui_window_content_rect(&d->ui);
    ui_vec4_t img_area = ui_v4(cr.x + 10.0f, cr.y + (d->ui.layout.cursor_y - cr.y) + 8.0f, cr.z - 20.0f, (cr.y + cr.w) - (d->ui.layout.cursor_y + 18.0f) - 10.0f);
    if (img_area.z < 1.0f)
        img_area.z = 1.0f;
    if (img_area.w < 1.0f)
        img_area.w = 1.0f;

    ui_draw_rect(&d->ui, img_area, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.20f), d->ui.style.corner, 0.0f);
    ui_draw_rect(&d->ui, img_area, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.55f), d->ui.style.corner, 1.0f);

    if (s->asset.type == ASSET_IMAGE && s->asset.state == ASSET_STATE_READY)
    {
        uint32_t tex = s->asset.as.image.gl_handle;
        if (tex)
        {
            ui_vec4_t img = ui_v4(img_area.x + 6.0f, img_area.y + 6.0f, img_area.z - 12.0f, img_area.w - 12.0f);
            ui_draw_image(&d->ui, img, tex, ui_v4(0.0f, 1.0f, 1.0f, 0.0f), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 1.0f));
        }
        else
        {
            ui_draw_text(&d->ui, ui_v2(img_area.x + 10.0f, img_area.y + 10.0f), ui_color(d->ui.style.text.rgb, 0.85f), 0, "READY image, but gl_handle=0");
        }
    }
    else
    {
        ui_draw_text(&d->ui, ui_v2(img_area.x + 10.0f, img_area.y + 10.0f), ui_color(d->ui.style.text.rgb, 0.85f), 0, "Selected slot is not a READY image");
    }
}

static void editor_assets_draw_all_images(editor_layer_data_t *d, const asset_manager_t *am, float key_w)
{
    uint32_t scount = (uint32_t)am->slots.size;

    ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
    ui_separator_text(&d->ui, "Final Assets (READY images)", 0);

    if (scount == 0)
    {
        editor_draw_kv_row(&d->ui, "images", "(none)", key_w);
        return;
    }

    uint32_t shown = 0;
    uint32_t max_show = 256;

    float lh = d->ui.text_height(d->ui.text_user, 0);
    float row_h = d->ui.style.line_h;
    if (row_h < lh)
        row_h = lh;
    if (row_h < 80.0f)
        row_h = 80.0f;

    for (uint32_t i = 0; i < scount; ++i)
    {
        const asset_slot_t *s = (const asset_slot_t *)((uint8_t *)am->slots.data + (size_t)i * (size_t)am->slots.element_size);
        if (!s)
            continue;
        if (s->generation == 0)
            continue;
        if (s->asset.type != ASSET_IMAGE)
            continue;
        if (s->asset.state != ASSET_STATE_READY)
            continue;

        char key[64];
        char val[256];

        snprintf(key, sizeof(key), "#%u", (unsigned)i);
        snprintf(val, sizeof(val), "handle=0x%08X  tex=%u",
                 (unsigned)editor_guess_asset_handle_u32(i, s->generation),
                 (unsigned)s->asset.as.image.gl_handle);

        float widths[3];
        widths[0] = key_w;
        widths[1] = 0.0f;
        widths[2] = 80.0f;

        ui_layout_row(&d->ui.layout, row_h, 3, widths, d->ui.style.spacing);

        ui_vec4_t rk = ui_layout_next(&d->ui.layout, d->ui.style.spacing);
        ui_vec4_t rv = ui_layout_next(&d->ui.layout, d->ui.style.spacing);
        ui_vec4_t rt = ui_layout_next(&d->ui.layout, d->ui.style.spacing);

        float ky = rk.y + (rk.w - lh) * 0.5f;
        float vy = rv.y + (rv.w - lh) * 0.5f;

        ui_draw_text(&d->ui, ui_v2(rk.x, ky), ui_color(d->ui.style.text.rgb, 0.78f), 0, key);
        ui_draw_text(&d->ui, ui_v2(rv.x, vy), ui_color(d->ui.style.text.rgb, 0.95f), 0, val);

        ui_draw_rect(&d->ui, rt, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.22f), d->ui.style.corner, 0.0f);
        ui_draw_rect(&d->ui, rt, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.55f), d->ui.style.corner, 1.0f);

        if (s->asset.as.image.gl_handle)
        {
            ui_vec4_t img = ui_v4(rt.x + 4.0f, rt.y + 4.0f, rt.z - 8.0f, rt.w - 8.0f);
            ui_draw_image(&d->ui, img, s->asset.as.image.gl_handle, ui_v4(0.0f, 1.0f, 1.0f, 0.0f), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 1.0f));
        }

        shown += 1;
        if (shown >= max_show)
        {
            char v[64];
            snprintf(v, sizeof(v), "showing first %u", (unsigned)max_show);
            editor_draw_kv_row(&d->ui, "images", v, key_w);
            break;
        }
    }

    if (shown == 0)
        editor_draw_kv_row(&d->ui, "images", "(none)", key_w);
}

void layer_init(layer_t *layer)
{
    editor_layer_data_t *d = (editor_layer_data_t *)calloc(1, sizeof(editor_layer_data_t));
    layer->data = d;
    if (!d)
        return;

    ui_init(&d->ui, ui_editor_realloc, 0);

    if (!ui_gl_backend_init(&d->glui, ui_editor_realloc, 0))
        return;

    ui_attach_backend(&d->ui, ui_gl_backend_base(&d->glui));

    d->last_fb.x = 0;
    d->last_fb.y = 0;
    d->first_frame = 1;
    d->inited = 1;

    d->dt = 0.0f;
    d->fps = 0.0f;

    d->queued_count = 0;

    d->last_scene_size.x = 0;
    d->last_scene_size.y = 0;

    d->am_show_modules = 1;
    d->am_show_slots = 0;
    d->am_show_queues = 1;

    d->asset_slot_pick = 0;
    d->show_ui_demo = 1;

    ui_gl_backend_add_font_from_ttf_file(&d->glui, 0, "res/fonts/0xProtoNerdFontPropo-Regular.ttf", 16.0f);

    d->ui.style.bg = ui_color(ui_v3(0.05f, 0.055f, 0.065f), 1.0f);
    d->ui.style.panel_bg = ui_color(ui_v3(0.075f, 0.082f, 0.095f), 1.0f);
    d->ui.style.text = ui_color(ui_v3(0.92f, 0.93f, 0.95f), 1.0f);

    d->ui.style.btn = ui_color(ui_v3(0.12f, 0.13f, 0.15f), 1.0f);
    d->ui.style.btn_hover = ui_color(ui_v3(0.16f, 0.175f, 0.205f), 1.0f);
    d->ui.style.btn_active = ui_color(ui_v3(0.10f, 0.11f, 0.13f), 1.0f);

    d->ui.style.outline = ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.55f);

    d->ui.style.header_bg = ui_color(ui_v3(0.095f, 0.105f, 0.125f), 1.0f);
    d->ui.style.header_bg_active = ui_color(ui_v3(0.11f, 0.125f, 0.155f), 1.0f);
    d->ui.style.header_text = ui_color(ui_v3(0.96f, 0.97f, 0.99f), 1.0f);

    d->ui.style.window_bg = ui_color(ui_v3(0.075f, 0.082f, 0.095f), 1.0f);
    d->ui.style.window_shadow = ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.42f);

    d->ui.style.accent = ui_color(ui_v3(0.95f, 0.52f, 0.16f), 1.0f);
    d->ui.style.accent_dim = ui_color(ui_v3(0.95f, 0.52f, 0.16f), 0.18f);

    d->ui.style.scroll_track = ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.25f);
    d->ui.style.scroll_thumb = ui_color(ui_v3(0.88f, 0.90f, 0.94f), 0.28f);
    d->ui.style.scroll_thumb_hover = ui_color(ui_v3(0.88f, 0.90f, 0.94f), 0.52f);

    d->ui.style.separator = ui_color(ui_v3(1.0f, 1.0f, 1.0f), 0.10f);

    d->ui.style.padding = 10.0f;
    d->ui.style.spacing = 8.0f;
    d->ui.style.line_h = 24.0f;
    d->ui.style.corner = 7.0f;

    d->ui.style.window_corner = 12.0f;
    d->ui.style.header_corner = 12.0f;
    d->ui.style.shadow_size = 16.0f;
    d->ui.style.outline_thickness = 1.0f;
    d->ui.style.scroll_w = 12.0f;
    d->ui.style.scroll_pad = 2.0f;
}

void layer_update(layer_t *layer, float dt)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;

    d->dt = dt;
    d->fps = (dt > 0.000001f) ? (1.0f / dt) : 0.0f;
}

void layer_post_update(layer_t *layer, float dt)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;

    Application *app = get_application();
    if (!app)
        return;

    vec2i fb = wm_get_framebuffer_size(&app->window_manager);
    if (fb.x <= 0 || fb.y <= 0)
        return;

    renderer_t *r = &app->renderer;

    if (fb.x != d->last_fb.x || fb.y != d->last_fb.y)
        d->last_fb = fb;

    double now = wm_get_time();

    editor_ui_flush_events(d);

    ui_set_delta_time(&d->ui, dt);
    ui_begin(&d->ui, ui_v2i(fb.x, fb.y));

    ui_vec4_t dock = ui_v4(0.0f, 0.0f, (float)fb.x, (float)fb.y);
    ui_dockspace(&d->ui, "Dockspace", dock);

    if (d->first_frame)
    {
        ui_window_set_next_pos(&d->ui, ui_v2((float)fb.x - 360.0f, 20.0f));
        ui_window_set_next_size(&d->ui, ui_v2(340.0f, 620.0f));
    }

    if (ui_window_begin(&d->ui, "Stats", UI_WIN_NONE))
    {
        ui_vec4_t cr = ui_window_content_rect(&d->ui);

        ui_draw_rect(&d->ui, cr, d->ui.style.panel_bg, d->ui.style.corner, 0.0f);
        ui_draw_rect(&d->ui, cr, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.40f), d->ui.style.corner, 1.0f);

        ui_vec4_t inner = ui_v4(cr.x + 10.0f, cr.y + 10.0f, cr.z - 20.0f, cr.w - 20.0f);
        ui_layout_begin(&d->ui.layout, inner, 0.0f);

        float key_w = 160.0f;

        ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
        ui_separator_text(&d->ui, "Performance", 0);

        {
            char v[64];
            snprintf(v, sizeof(v), "%.3f ms", (double)d->dt * 1000.0);
            editor_draw_kv_row(&d->ui, "dt", v, key_w);
        }
        {
            char v[64];
            snprintf(v, sizeof(v), "%.1f", (double)d->fps);
            editor_draw_kv_row(&d->ui, "fps", v, key_w);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        {
            float dt_ms = d->dt * 1000.0f;
            float t01 = dt_ms / 16.0f;
            if (t01 < 0.0f)
                t01 = 0.0f;
            if (t01 > 1.0f)
                t01 = 1.0f;
            ui_progress_bar(&d->ui, t01, "Frame Time", 0);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        ui_separator(&d->ui);

        ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
        ui_separator_text(&d->ui, "Toggles", 0);

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        {
            int vsync = cvar_get_bool_name("cl_vsync") ? 1 : 0;
            if (ui_checkbox(&d->ui, "VSync", 0, &vsync))
                cvar_set_bool_name("cl_vsync", vsync != 0);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        {
            int wf = cvar_get_bool_name("cl_r_wireframe") ? 1 : 0;
            if (ui_checkbox(&d->ui, "Wireframe", 0, &wf))
                cvar_set_bool_name("cl_r_wireframe", wf != 0);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        {
            int demo = d->show_ui_demo;
            if (ui_checkbox(&d->ui, "Show UI Demo", 0, &demo))
                d->show_ui_demo = demo;
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        ui_separator(&d->ui);

        ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
        ui_separator_text(&d->ui, "Render Debug", 0);

        {
            int dbg = cvar_get_int_name("cl_render_debug");

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "0 None", 0, &dbg, 0))
                cvar_set_int_name("cl_render_debug", dbg);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "1 LODs", 0, &dbg, 1))
                cvar_set_int_name("cl_render_debug", dbg);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "2 Light Clusters", 0, &dbg, 2))
                cvar_set_int_name("cl_render_debug", dbg);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "3 Normals", 0, &dbg, 3))
                cvar_set_int_name("cl_render_debug", dbg);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "4 Alpha", 0, &dbg, 4))
                cvar_set_int_name("cl_render_debug", dbg);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "5 Albedo", 0, &dbg, 5))
                cvar_set_int_name("cl_render_debug", dbg);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            if (ui_radio(&d->ui, "6 LOD Dither", 0, &dbg, 6))
                cvar_set_int_name("cl_render_debug", dbg);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        ui_separator(&d->ui);

        ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.9f, 1, 0, d->ui.style.spacing);
        ui_separator_text(&d->ui, "Renderer Stats", 0);

        {
            const render_stats_t *s = R_get_stats(r);
            if (s)
            {
                char v0[64], v1[64], v2[64], v3[64], v4[64];
                snprintf(v0, sizeof(v0), "%llu", (uint64_t)s->draw_calls);
                snprintf(v1, sizeof(v1), "%llu", (uint64_t)s->triangles);
                snprintf(v2, sizeof(v2), "%llu", (uint64_t)s->instanced_draw_calls);
                snprintf(v3, sizeof(v3), "%llu", (uint64_t)s->instances);
                snprintf(v4, sizeof(v4), "%llu", (uint64_t)s->instanced_triangles);

                editor_draw_kv_row(&d->ui, "draw_calls", v0, key_w);
                editor_draw_kv_row(&d->ui, "triangles", v1, key_w);
                editor_draw_kv_row(&d->ui, "inst_draw_calls", v2, key_w);
                editor_draw_kv_row(&d->ui, "instances", v3, key_w);
                editor_draw_kv_row(&d->ui, "inst_triangles", v4, key_w);
            }
            else
            {
                editor_draw_kv_row(&d->ui, "stats", "null", key_w);
            }
        }

        ui_window_end(&d->ui);
    }

    if (d->first_frame)
    {
        ui_window_set_next_pos(&d->ui, ui_v2((float)fb.x - 740.0f, 20.0f));
        ui_window_set_next_size(&d->ui, ui_v2(360.0f, 620.0f));
    }

    if (ui_window_begin(&d->ui, "Assets", UI_WIN_NONE))
    {
        ui_vec4_t cr = ui_window_content_rect(&d->ui);

        ui_draw_rect(&d->ui, cr, d->ui.style.panel_bg, d->ui.style.corner, 0.0f);
        ui_draw_rect(&d->ui, cr, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.40f), d->ui.style.corner, 1.0f);

        ui_vec4_t inner = ui_v4(cr.x + 10.0f, cr.y + 10.0f, cr.z - 20.0f, cr.w - 20.0f);
        ui_layout_begin(&d->ui.layout, inner, 0.0f);

        float key_w = 170.0f;

        asset_manager_t *am = r ? r->assets : 0;

        if (!am)
        {
            editor_draw_kv_row(&d->ui, "asset_manager", "null", key_w);
        }
        else
        {
            editor_am_draw_overview(d, am, key_w);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            ui_separator(&d->ui);

            d->am_show_queues = ui_collapsing_header(&d->ui, "Queues", 0, d->am_show_queues);
            if (d->am_show_queues)
                editor_am_draw_queues(d, am, key_w);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            ui_separator(&d->ui);

            d->am_show_modules = ui_collapsing_header(&d->ui, "Supported Modules", 0, d->am_show_modules);
            if (d->am_show_modules)
                editor_am_draw_modules(d, am, key_w);

            ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
            ui_separator(&d->ui);

            editor_assets_draw_all_images(d, am, key_w);
        }

        ui_window_end(&d->ui);
    }

    if (d->first_frame)
    {
        ui_window_set_next_pos(&d->ui, ui_v2((float)fb.x - 1120.0f, 20.0f));
        ui_window_set_next_size(&d->ui, ui_v2(380.0f, 620.0f));
    }

    if (ui_window_begin(&d->ui, "Asset Inspector", UI_WIN_NONE))
    {
        ui_vec4_t cr = ui_window_content_rect(&d->ui);

        ui_draw_rect(&d->ui, cr, d->ui.style.panel_bg, d->ui.style.corner, 0.0f);
        ui_draw_rect(&d->ui, cr, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.40f), d->ui.style.corner, 1.0f);

        ui_vec4_t inner = ui_v4(cr.x + 10.0f, cr.y + 10.0f, cr.z - 20.0f, cr.w - 20.0f);
        ui_layout_begin(&d->ui.layout, inner, 0.0f);

        float key_w = 170.0f;
        asset_manager_t *am = r ? r->assets : 0;

        if (!am)
        {
            editor_draw_kv_row(&d->ui, "asset_manager", "null", key_w);
        }
        else
        {
            editor_assets_draw_picker_and_preview(d, am, key_w);
        }

        ui_window_end(&d->ui);
    }

    if (d->first_frame)
    {
        ui_window_set_next_pos(&d->ui, ui_v2(20.0f, 20.0f));
        ui_window_set_next_size(&d->ui, ui_v2(900.0f, 600.0f));
    }

    if (d->show_ui_demo)
        ui_render_demo_window(&d->ui);

    ui_vec2_t scene_target = ui_v2((float)fb.x, (float)fb.y);

    if (ui_window_begin(&d->ui, "Scene Renderer", UI_WIN_NONE))
    {
        ui_vec4_t body = ui_window_content_rect(&d->ui);

        ui_draw_rect(&d->ui, body, ui_color(ui_v3(0.03f, 0.035f, 0.045f), 1.0f), d->ui.style.corner, 0.0f);
        ui_draw_rect(&d->ui, body, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.55f), d->ui.style.corner, 1.0f);

        ui_vec4_t frame = ui_v4(body.x + 8.0f, body.y + 8.0f, body.z - 16.0f, body.w - 16.0f);
        ui_draw_rect(&d->ui, frame, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.20f), d->ui.style.corner, 0.0f);
        ui_draw_rect(&d->ui, frame, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.60f), d->ui.style.corner, 1.0f);

        float target_w = frame.z - 12.0f;
        float target_h = frame.w - 12.0f;
        if (target_w < 1.0f)
            target_w = 1.0f;
        if (target_h < 1.0f)
            target_h = 1.0f;

        scene_target = ui_v2(target_w, target_h);

        {
            uint32_t fbo_or_tex = R_get_final_fbo(r);
            uint32_t tex = editor_resolve_tex_from_fbo_or_tex(fbo_or_tex);

            if (tex)
            {
                ui_vec4_t img = ui_v4(frame.x + 6.0f, frame.y + 6.0f, frame.z - 12.0f, frame.w - 12.0f);
                ui_draw_image(&d->ui, img, tex, ui_v4(0.0f, 1.0f, 1.0f, 0.0f), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 1.0f));
            }
            else
            {
                ui_draw_text(&d->ui, ui_v2(frame.x + 10.0f, frame.y + 10.0f), d->ui.style.text, 0, "No final texture");
            }
        }

        {
            char buf[128];
            snprintf(buf, sizeof(buf), "t=%.3f   fb=%dx%d", now, r->fb_size.x, r->fb_size.y);

            float pill_h = 22.0f;
            float pad = 10.0f;
            ui_vec4_t pill = ui_v4(frame.x + 10.0f, frame.y + 10.0f, 240.0f, pill_h);
            ui_draw_rect(&d->ui, pill, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.35f), pill_h * 0.5f, 0.0f);
            ui_draw_rect(&d->ui, pill, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.60f), pill_h * 0.5f, 1.0f);

            ui_draw_rect(&d->ui, ui_v4(pill.x + 2.0f, pill.y + 2.0f, 4.0f, pill_h - 4.0f), d->ui.style.accent, 2.0f, 0.0f);

            ui_draw_text(&d->ui, ui_v2(pill.x + pad, pill.y + 3.0f), ui_color(d->ui.style.text.rgb, 0.85f), 0, buf);
        }

        ui_window_end(&d->ui);
    }

    vec2i scene_fb;
    scene_fb.x = (int)(scene_target.x + 0.5f);
    scene_fb.y = (int)(scene_target.y + 0.5f);
    if (scene_fb.x < 1)
        scene_fb.x = 1;
    if (scene_fb.y < 1)
        scene_fb.y = 1;

    if (scene_fb.x != d->last_scene_size.x || scene_fb.y != d->last_scene_size.y)
    {
        R_resize(r, scene_fb);
        d->last_scene_size = scene_fb;
    }

    if (d->first_frame)
        d->first_frame = 0;

    ui_end(&d->ui);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb.x, fb.y);
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    ui_render(&d->ui);
}

void layer_draw(layer_t *layer)
{
    (void)layer;
}

void layer_shutdown(layer_t *layer)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d)
        return;

    if (d->inited)
    {
        ui_gl_backend_shutdown(&d->glui);
        ui_shutdown(&d->ui);
    }

    free(d);
    layer->data = 0;
}

bool layer_on_event(layer_t *layer, event_t *e)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited || !e)
        return false;

    ui_event_t ue;
    memset(&ue, 0, sizeof(ue));
    ue.type = UI_EV_NONE;

    if (e->type == EV_MOUSE_MOVE)
    {
        ue.type = UI_EV_MOUSE_MOVE;
        ue.mouse_pos = ui_v2((float)e->as.mouse_move.x, (float)e->as.mouse_move.y);
    }
    else if (e->type == EV_MOUSE_BUTTON_DOWN)
    {
        ue.type = UI_EV_MOUSE_BUTTON_DOWN;
        ue.button = (uint8_t)e->as.mouse_button.button;
        ue.mods = (uint8_t)e->as.mouse_button.mods;
        ue.mouse_pos = ui_v2((float)e->as.mouse_button.x, (float)e->as.mouse_button.y);
    }
    else if (e->type == EV_MOUSE_BUTTON_UP)
    {
        ue.type = UI_EV_MOUSE_BUTTON_UP;
        ue.button = (uint8_t)e->as.mouse_button.button;
        ue.mods = (uint8_t)e->as.mouse_button.mods;
        ue.mouse_pos = ui_v2((float)e->as.mouse_button.x, (float)e->as.mouse_button.y);
    }
    else if (e->type == EV_MOUSE_SCROLL)
    {
        ue.type = UI_EV_MOUSE_SCROLL;
        ue.scroll = ui_v2((float)e->as.mouse_scroll.dx, (float)e->as.mouse_scroll.dy);
    }
    else if (e->type == EV_KEY_DOWN)
    {
        ue.type = UI_EV_KEY_DOWN;
        ue.key = (uint32_t)e->as.key.key;
        ue.repeat = (uint8_t)e->as.key.repeat;
        ue.mods = (uint8_t)e->as.key.mods;
    }
    else if (e->type == EV_KEY_UP)
    {
        ue.type = UI_EV_KEY_UP;
        ue.key = (uint32_t)e->as.key.key;
        ue.repeat = 0;
        ue.mods = (uint8_t)e->as.key.mods;
    }
    else if (e->type == EV_CHAR)
    {
        ue.type = UI_EV_CHAR;
        ue.codepoint = (uint32_t)e->as.ch.codepoint;
    }

    if (ue.type != UI_EV_NONE)
        editor_ui_queue_event(d, &ue);

    const ui_io_t *io = ui_io(&d->ui);
    if (!io)
        return false;

    int wants_mouse = io->want_capture_mouse || io->want_text_input;
    int wants_keyboard = io->want_capture_keyboard || io->want_text_input;

    if (e->type == EV_MOUSE_MOVE || e->type == EV_MOUSE_BUTTON_DOWN || e->type == EV_MOUSE_BUTTON_UP || e->type == EV_MOUSE_SCROLL)
        return wants_mouse ? true : false;

    if (e->type == EV_KEY_DOWN || e->type == EV_KEY_UP || e->type == EV_CHAR)
        return wants_keyboard ? true : false;

    return false;
}

layer_t create_editor_layer(void)
{
    layer_t layer = create_layer("Editor");
    layer.init = layer_init;
    layer.shutdown = layer_shutdown;
    layer.update = layer_update;
    layer.post_update = layer_post_update;
    layer.draw = layer_draw;
    layer.on_event = layer_on_event;
    return layer;
}
