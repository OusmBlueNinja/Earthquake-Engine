#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ecs_types.h"

typedef struct ecs_world_t ecs_world_t;

ecs_component_id_t ecs_register_component_type(
    ecs_world_t *w,
    const char *name,
    uint32_t size,
    uint32_t base_offset,
    ecs_component_save_fn save_fn,
    ecs_component_load_fn load_fn
);

ecs_component_id_t ecs_component_id_by_name(const ecs_world_t *w, const char *name);

#define ecs_register_component(w, T) ecs_register_component_type((w), #T, (uint32_t)sizeof(T), (uint32_t)offsetof(T, base), NULL, NULL)
#define ecs_register_component_ex(w, T, save_fn, load_fn) ecs_register_component_type((w), #T, (uint32_t)sizeof(T), (uint32_t)offsetof(T, base), (save_fn), (load_fn))
#define ecs_component_id(w, T) ecs_component_id_by_name((w), #T)
