#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct Application Application;
typedef struct event_t event_t;

typedef struct layer_t
{
    uint32_t id;
    char *name;
    Application *app;

    void (*init)(struct layer_t *layer);
    void (*update)(struct layer_t *layer, float dt);
    void (*draw)(struct layer_t *layer);
    void (*shutdown)(struct layer_t *layer);

    bool (*on_event)(struct layer_t *layer, event_t *e);

    void *data;
} layer_t;
