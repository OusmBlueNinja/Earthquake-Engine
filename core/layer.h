#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct Application Application;

typedef struct layer_t
{
    uint32_t id;
    const char *name;
    Application *app;

    void (*init)(struct layer_t *layer);
    void (*update)(struct layer_t *layer, float dt);
    void (*draw)(struct layer_t *layer);
    void (*shutdown)(struct layer_t *layer);
    void *data;
    
} layer_t;
