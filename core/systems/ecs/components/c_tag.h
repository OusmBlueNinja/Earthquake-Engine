#pragma once

#include <stdint.h>

#include "ecs/ecs_types.h"

#define C_TAG_NAME_MAX 64u

typedef struct c_tag_t
{
    char name[C_TAG_NAME_MAX];

    uint16_t layer;
    uint8_t visible;

    base_component_t base;
} c_tag_t;

typedef struct ecs_world_t ecs_world_t;

void c_tag_register(ecs_world_t *w);
