#pragma once

#include <stdint.h>

#include "ecs/ecs_types.h"
#include "types/vec3.h"

typedef struct c_transform_t
{
    vec3 position;
    vec3 rotation;
    vec3 scale;

    base_component_t base;
} c_transform_t;

typedef struct ecs_world_t ecs_world_t;

void c_transform_register(ecs_world_t *w);
