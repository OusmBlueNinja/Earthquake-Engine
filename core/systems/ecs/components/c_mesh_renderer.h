#pragma once

#include <stdint.h>

#include "ecs/ecs_types.h"
#include "handle.h"

typedef struct c_mesh_renderer_t
{
    ihandle_t model;

    base_component_t base;
} c_mesh_renderer_t;

typedef struct ecs_world_t ecs_world_t;

void c_mesh_renderer_register(ecs_world_t *w);
