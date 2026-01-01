#pragma once

#include <stdint.h>

typedef uint64_t ecs_entity_t;
typedef uint32_t ecs_component_id_t;

#define ECS_INVALID_U32 0xFFFFFFFFu

typedef struct base_component_t
{
    ecs_entity_t entity;
    ecs_component_id_t type_id;
    uint32_t flags;
} base_component_t;
