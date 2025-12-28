#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct Application Application;
typedef struct event_t event_t;

typedef enum layer_flags_t
{
    LAYER_FLAG_NONE = 0,
    LAYER_FLAG_HIDDEN_IN_TITLE = 1u << 0,
    LAYER_FLAG_BLOCK_UPDATE = 1u << 1,
    LAYER_FLAG_BLOCK_DRAW = 1u << 2,
    LAYER_FLAG_BLOCK_EVENTS = 1u << 3,
} layer_flags_t;

typedef struct layer_t
{
    uint32_t id;
    layer_flags_t flags;
    char *name;
    Application *app;

    void (*init)(struct layer_t *layer);
    void (*update)(struct layer_t *layer, float dt);
    void (*draw)(struct layer_t *layer);
    void (*post_update)(struct layer_t *layer, float dt);
    void (*shutdown)(struct layer_t *layer);

    bool (*on_event)(struct layer_t *layer, event_t *e);

    void *data;
} layer_t;

static inline void layer_set_flags(layer_t *layer, layer_flags_t mask, bool value)
{
    layer->flags = value
                       ? (layer_flags_t)(layer->flags | mask)
                       : (layer_flags_t)(layer->flags & (layer_flags_t)(~mask));
}

static inline bool layer_get_flags(const layer_t *layer, layer_flags_t mask)
{
    return (layer->flags & mask) == mask;
}