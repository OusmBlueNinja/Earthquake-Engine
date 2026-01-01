#pragma once

#include <stdint.h>

#include "core/systems/ecs/ecs_types.h"
#include "core/types/vector.h"

typedef struct ecs_type_info_t
{
    char *name;
    uint32_t size;
    uint32_t base_offset;
    ecs_component_save_fn save_fn;
    ecs_component_load_fn load_fn;
    ecs_component_ctor_fn ctor_fn;
} ecs_type_info_t;

typedef struct ecs_pool_t
{
    ecs_component_id_t type_id;
    vector_t dense_entities;
    vector_t dense_data;
    vector_t sparse;
} ecs_pool_t;

typedef struct ecs_world_t
{
    vector_t entity_gen;
    vector_t entity_alive;
    vector_t entity_parent;
    vector_t free_list;

    vector_t types;
    vector_t pools;

    ecs_component_id_t required_tag_id;
    ecs_entity_t root_entity;
} ecs_world_t;

typedef struct ecs_world_desc_t
{
    uint32_t initial_entity_capacity;
} ecs_world_desc_t;

void ecs_world_init(ecs_world_t *w, ecs_world_desc_t desc);
void ecs_world_destroy(ecs_world_t *w);

void ecs_world_set_required_tag(ecs_world_t *w, ecs_component_id_t tag_type_id);
ecs_component_id_t ecs_world_required_tag(const ecs_world_t *w);
