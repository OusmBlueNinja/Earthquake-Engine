#pragma once

#include <stdint.h>
#include "core/systems/ecs/ecs_types.h"

typedef struct ecs_world_t ecs_world_t;

static inline uint32_t ecs_entity_index(ecs_entity_t e)
{
    return (uint32_t)(e & 0xFFFFFFFFu);
}

static inline uint32_t ecs_entity_gen(ecs_entity_t e)
{
    return (uint32_t)(e >> 32);
}

static inline ecs_entity_t ecs_entity_pack(uint32_t index, uint32_t gen)
{
    return ((uint64_t)gen << 32) | (uint64_t)index;
}

ecs_entity_t ecs_world_root(const ecs_world_t *w);

ecs_entity_t ecs_entity_create(ecs_world_t *w);
void ecs_entity_destroy(ecs_world_t *w, ecs_entity_t e);
int ecs_entity_is_alive(const ecs_world_t *w, ecs_entity_t e);

ecs_entity_t ecs_entity_get_parent(const ecs_world_t *w, ecs_entity_t e);
int ecs_entity_set_parent(ecs_world_t *w, ecs_entity_t e, ecs_entity_t parent);
int ecs_entity_is_root(const ecs_world_t *w, ecs_entity_t e);
