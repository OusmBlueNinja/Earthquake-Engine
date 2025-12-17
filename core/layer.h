#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct layer_t
{
    uint32_t id;
    const char *name;
    void (*update)(struct layer_t *layer, float dt);
    void (*draw)(struct layer_t *layer);
    void *user_data;
} layer_t;

static inline layer_t create_layer(uint32_t id,
                                   const char *name,
                                   void (*update)(layer_t *, float),
                                   void (*draw)(layer_t *),
                                   void *user_data)
{
    layer_t l;
    l.id = id;
    l.name = name;
    l.update = update;
    l.draw = draw;
    l.user_data = user_data;

    
    return l;
}
