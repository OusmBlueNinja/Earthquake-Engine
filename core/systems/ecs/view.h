#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "core/systems/ecs/ecs_types.h"

#define ECS_VIEW_MAX 8u

typedef struct ecs_world_t ecs_world_t;

typedef struct ecs_view_t
{
    ecs_world_t *w;
    uint8_t count;
    ecs_component_id_t ids[ECS_VIEW_MAX];
    uint32_t pool_indices[ECS_VIEW_MAX];
    uint32_t comp_sizes[ECS_VIEW_MAX];
    uint8_t primary;
    uint32_t cursor;
} ecs_view_t;

bool ecs_view_init(ecs_view_t *v, ecs_world_t *w, uint8_t count, const ecs_component_id_t *ids);
void ecs_view_reset(ecs_view_t *v);
bool ecs_view_next(ecs_view_t *v, ecs_entity_t *out_e, void **out_components);

#define ECS_VIEW2(v, w, a, b) ecs_view_init((v), (w), 2u, (ecs_component_id_t[2]){(a), (b)})
#define ECS_VIEW3(v, w, a, b, c) ecs_view_init((v), (w), 3u, (ecs_component_id_t[3]){(a), (b), (c)})
#define ECS_VIEW4(v, w, a, b, c, d) ecs_view_init((v), (w), 4u, (ecs_component_id_t[4]){(a), (b), (c), (d)})
