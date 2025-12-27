#include "editor_layer.h"
#include "core.h"
#include <stdlib.h>
#include <string.h>

typedef struct editor_layer_data_t
{
} editor_layer_data_t;

void layer_init(layer_t *layer)
{
    if (!layer)
        return;

    editor_layer_data_t *d = (editor_layer_data_t *)calloc(1, sizeof(editor_layer_data_t));
    layer->data = d;
}

void layer_update(layer_t *layer, float dt)
{
    if (!layer || !layer->data)
        return;

    editor_layer_data_t *d = (editor_layer_data_t *)layer->data;
}

void layer_draw(layer_t *layer)
{
    if (!layer || !layer->data)
        return;

    editor_layer_data_t *d = (editor_layer_data_t *)layer->data;
    (void)d;
}

void layer_shutdown(layer_t *layer)
{
    if (!layer)
        return;

    if (layer->data)
    {
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
