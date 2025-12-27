#include "editor_layer.h"
#include "core.h"
#include <stdlib.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "nuklear.h"
#include "nuklear_glfw_gl3.h"

GLFWwindow *wm_get_glfw_window(window_manager *wm);

typedef struct editor_layer_data_t
{
    struct nk_glfw glfw;
    struct nk_context *ctx;
    int inited;

    int show_demo;
    int show_stats;

    float value;
    int check;
    struct nk_colorf clear;
} editor_layer_data_t;

static void editor_ui(editor_layer_data_t *d)
{
    struct nk_context *ctx = d->ctx;

    if (d->show_demo)
    {
        if (nk_begin(ctx, "Editor", nk_rect(20, 20, 420, 360),
                    NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE))
        {
            nk_layout_row_dynamic(ctx, 28, 1);
            nk_label(ctx, "Nuklear in your Editor layer", NK_TEXT_LEFT);

            nk_layout_row_dynamic(ctx, 28, 2);
            nk_checkbox_label(ctx, "Stats", &d->show_stats);
            nk_checkbox_label(ctx, "Check", &d->check);

            nk_layout_row_dynamic(ctx, 28, 1);
            nk_property_float(ctx, "Value", 0.0f, &d->value, 1.0f, 0.01f, 0.005f);

            nk_layout_row_dynamic(ctx, 180, 1);
            d->clear = nk_color_picker(ctx, d->clear, NK_RGBA);
        }
        nk_end(ctx);
    }

    if (d->show_stats)
    {
        if (nk_begin(ctx, "Renderer Stats", nk_rect(460, 20, 360, 160),
                    NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE))
        {
            renderer_t *r = get_renderer();

            nk_layout_row_dynamic(ctx, 24, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "fb: %d x %d", r->fb_size.x, r->fb_size.y);

            nk_layout_row_dynamic(ctx, 24, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "value: %.3f", d->value);
        }
        nk_end(ctx);
    }
}

void layer_init(layer_t *layer)
{
    if (!layer) return;

    editor_layer_data_t *d = (editor_layer_data_t *)calloc(1, sizeof(editor_layer_data_t));
    layer->data = d;

    d->show_demo = 1;
    d->show_stats = 1;
    d->value = 0.25f;
    d->check = 0;
    d->clear.r = 0.10f;
    d->clear.g = 0.12f;
    d->clear.b = 0.14f;
    d->clear.a = 1.0f;

    Application *app = get_application();
    GLFWwindow *win = wm_get_glfw_window(&app->window_manager);
    if (!win) return;

    nk_glfw3_init(&d->glfw, win, NK_GLFW3_INSTALL_CALLBACKS);
    d->ctx = &d->glfw.ctx;

    struct nk_font_atlas *atlas = 0;
    nk_glfw3_font_stash_begin(&d->glfw, &atlas);
    nk_glfw3_font_stash_end(&d->glfw);

    d->inited = 1;
}

void layer_update(layer_t *layer, float dt)
{
    (void)dt;
    if (!layer || !layer->data) return;

    editor_layer_data_t *d = (editor_layer_data_t *)layer->data;
    if (!d->inited) return;

    nk_glfw3_new_frame(&d->glfw);
    editor_ui(d);
}

void layer_draw(layer_t *layer)
{
    if (!layer || !layer->data) return;

    editor_layer_data_t *d = (editor_layer_data_t *)layer->data;
    if (!d->inited) return;

    nk_glfw3_render(&d->glfw, NK_ANTI_ALIASING_ON, 1024 * 1024, 512 * 1024);
}

void layer_shutdown(layer_t *layer)
{
    if (!layer) return;

    if (layer->data)
    {
        editor_layer_data_t *d = (editor_layer_data_t *)layer->data;
        if (d->inited)
            nk_glfw3_shutdown(&d->glfw);

        free(layer->data);
        layer->data = NULL;
    }
}

bool layer_on_event(layer_t *layer, event_t *e)
{
    (void)layer;
    (void)e;
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
