#include "demo_layer.h"
#include "utils/logger.h"

static void demo_layer_update(layer_t *layer)
{
    (void)layer;
}

static void demo_layer_draw(layer_t *layer)
{
    (void)layer;
    LOG_DEBUG("demo_layer draw");
}

layer_t create_demo_layer()
{
    layer_t layer;
    layer.name = "DemoLayer";
    layer.update = demo_layer_update;
    layer.draw = demo_layer_draw;
    return layer;
}
