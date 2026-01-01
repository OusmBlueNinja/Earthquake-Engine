#pragma once

#include <stdint.h>

#include "ecs/ecs_types.h"
#include "types/vec3.h"
#include "renderer/light.h"

typedef struct c_light_t
{
    light_type_t type;

    vec3 color;
    float intensity;

    float radius;
    float range;

    base_component_t base;
} c_light_t;

typedef struct ecs_world_t ecs_world_t;

void c_light_register(ecs_world_t *w);
