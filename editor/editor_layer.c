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
#include "ui/backends/opengl_backend.h"

typedef struct render_stats_t
{
    uint64_t draw_calls;
    uint64_t triangles;

    uint64_t instanced_draw_calls;
    uint64_t instances;
    uint64_t instanced_triangles;
} render_stats_t;

const render_stats_t *R_get_stats(const renderer_t *r);
uint32_t R_get_final_fbo(const renderer_t *r);
void R_resize(renderer_t *r, vec2i fb);

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

    ui_draw_text(ui, ui_v2(rk.x, ky), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 0.85f), 0, key);
    ui_draw_text(ui, ui_v2(rv.x, vy), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 0.95f), 0, val);
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

    ui_gl_backend_add_font_from_ttf_file(&d->glui, 0, "res/fonts/CascadiaMono-Regular.ttf", 16.0f);
}

void layer_update(layer_t *layer, float dt)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;

    d->dt = dt;
    d->fps = (dt > 0.000001f) ? (1.0f / dt) : 0.0f;
}

void layer_draw(layer_t *layer)
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
    {
        R_resize(r, fb);
        d->last_fb = fb;
    }

    double now = wm_get_time();

    editor_ui_flush_events(d);

    ui_begin(&d->ui, ui_v2i(fb.x, fb.y));

    ui_vec4_t dock = ui_v4(0.0f, 0.0f, (float)fb.x, (float)fb.y);
    ui_dockspace(&d->ui, "Dockspace", dock);

    if (d->first_frame)
    {
        ui_window_set_next_pos(&d->ui, ui_v2((float)fb.x - 360.0f, 20.0f));
        ui_window_set_next_size(&d->ui, ui_v2(340.0f, 320.0f));
    }

    if (ui_window_begin(&d->ui, "Stats", UI_WIN_NONE))
    {
        ui_vec4_t cr = ui_window_content_rect(&d->ui);

        ui_draw_rect(&d->ui, cr, ui_color(ui_v3(0.06f, 0.06f, 0.07f), 1.0f), 0.0f, 0.0f);
        ui_draw_rect(&d->ui, cr, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.35f), 0.0f, 1.0f);

        ui_layout_begin(&d->ui.layout, cr, 10.0f);

        float key_w = 170.0f;

        {
            char v[64];
            snprintf(v, sizeof(v), "%.3f ms", (double)d->dt * 1000.0);
            editor_draw_kv_row(&d->ui, "dt:", v, key_w);
        }
        {
            char v[64];
            snprintf(v, sizeof(v), "%.1f", (double)d->fps);
            editor_draw_kv_row(&d->ui, "fps:", v, key_w);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h, 1, 0, d->ui.style.spacing);
        {
            int vsync = cvar_get_bool_name("cl_vsync") ? 1 : 0;
            if (ui_checkbox(&d->ui, "VSync", 0, &vsync))
                cvar_set_bool_name("cl_vsync", vsync != 0);
        }

        ui_layout_row(&d->ui.layout, d->ui.style.line_h * 0.6f, 1, 0, d->ui.style.spacing);
        ui_layout_next(&d->ui.layout, d->ui.style.spacing);

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

                editor_draw_kv_row(&d->ui, "draw_calls:", v0, key_w);
                editor_draw_kv_row(&d->ui, "triangles:", v1, key_w);
                editor_draw_kv_row(&d->ui, "inst_draw_calls:", v2, key_w);
                editor_draw_kv_row(&d->ui, "instances:", v3, key_w);
                editor_draw_kv_row(&d->ui, "inst_triangles:", v4, key_w);
            }
            else
            {
                editor_draw_kv_row(&d->ui, "stats:", "null", key_w);
            }
        }

        ui_window_end(&d->ui);
    }

    if (d->first_frame)
    {
        ui_window_set_next_pos(&d->ui, ui_v2(20.0f, 20.0f));
        ui_window_set_next_size(&d->ui, ui_v2(900.0f, 600.0f));
    }

    if (ui_window_begin(&d->ui, "Scene Renderer", UI_WIN_NONE))
    {
        ui_vec4_t body = ui_window_content_rect(&d->ui);

        ui_draw_rect(&d->ui, body, ui_color(ui_v3(0.06f, 0.06f, 0.07f), 1.0f), 0.0f, 0.0f);
        ui_draw_rect(&d->ui, body, ui_color(ui_v3(0.0f, 0.0f, 0.0f), 0.35f), 0.0f, 1.0f);

        {
            uint32_t fbo_or_tex = R_get_final_fbo(r);
            uint32_t tex = editor_resolve_tex_from_fbo_or_tex(fbo_or_tex);

            if (tex)
            {
                ui_vec4_t img = ui_v4(body.x + 6.0f, body.y + 6.0f, body.z - 12.0f, body.w - 12.0f);
                ui_draw_image(&d->ui, img, tex, ui_v4(0.0f, 1.0f, 1.0f, 0.0f), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 1.0f));
            }
            else
            {
                ui_draw_text(&d->ui, ui_v2(body.x + 10.0f, body.y + 10.0f), d->ui.style.text, 0, "No final texture");
            }
        }

        {
            char buf[128];
            snprintf(buf, sizeof(buf), "t=%.3f  fb=%dx%d", now, fb.x, fb.y);
            ui_draw_text(&d->ui, ui_v2(body.x + 10.0f, body.y + body.w - 18.0f), ui_color(ui_v3(1.0f, 1.0f, 1.0f), 0.65f), 0, buf);
        }

        ui_window_end(&d->ui);
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

    return false;
}

layer_t create_editor_layer(void)
{
    layer_t layer = create_layer("Editor");
    layer.init = layer_init;
    layer.shutdown = layer_shutdown;
    layer.update = layer_update;
    layer.draw = layer_draw;
    layer.on_event = layer_on_event;
    return layer;
}
