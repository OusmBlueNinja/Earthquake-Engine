#pragma once

#include <stdint.h>

#include "ecs/ecs_types.h"

#define C_TAG_NAME_MAX 64u

typedef enum c_tag_flags_t
{
    C_TAG_FLAG_NONE = 0u,
    C_TAG_FLAG_EDITOR_ONLY = 1u << 0,
    C_TAG_FLAG_HIDDEN_IN_GAME = 1u << 1,
    C_TAG_FLAG_HIDDEN_IN_EDITOR = 1u << 2,
    C_TAG_FLAG_LOCKED = 1u << 3,
    C_TAG_FLAG_STATIC = 1u << 4,
    C_TAG_FLAG_NO_SAVE = 1u << 5,
    C_TAG_FLAG_NO_SELECT = 1u << 6
} c_tag_flags_t;

typedef struct c_tag_t
{
    char name[C_TAG_NAME_MAX];

    uint32_t layer;
    uint32_t visibility_mask;

    uint32_t flags;

    base_component_t base;
} c_tag_t;

typedef struct ecs_world_t ecs_world_t;

void c_tag_register(ecs_world_t *w);
