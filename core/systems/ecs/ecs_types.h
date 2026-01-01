#pragma once

#include <stdint.h>

#include "types/vector.h"

typedef uint64_t ecs_entity_t;
typedef uint32_t ecs_component_id_t;

#define ECS_INVALID_U32 0xFFFFFFFFu

typedef int (*ecs_component_save_fn)(const void *component, vector_t *out_bytes);
typedef int (*ecs_component_load_fn)(void *component, const uint8_t *payload, uint32_t payload_size);
typedef void (*ecs_component_ctor_fn)(void *component);

typedef struct base_component_t
{
    ecs_entity_t entity;
    ecs_component_id_t type_id;
    uint32_t flags;
    ecs_component_save_fn save_fn;
    ecs_component_load_fn load_fn;
} base_component_t;
