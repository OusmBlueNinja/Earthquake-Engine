#include "demo_layer.h"
#include "utils/logger.h"

static void demo_layer_init(layer_t *layer)
{
    (void)layer;
}

static void demo_layer_shutdown(layer_t *layer)
{

    (void)layer;
}

static void demo_layer_update(layer_t *layer, float dt)
{
    (void)layer;
}

static void demo_layer_draw(layer_t *layer)
{
    (void)layer;
}

layer_t create_demo_layer()
{
    layer_t layer;
    layer.name = "DemoLayer";
    layer.init = demo_layer_init;
    layer.shutdown = demo_layer_shutdown;
    layer.update = demo_layer_update;
    layer.draw = demo_layer_draw;
    return layer;
}
