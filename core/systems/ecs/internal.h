#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ecs/world.h"
#include "ecs/entity.h"

static inline char *ecs_strdup_local(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static inline uint32_t ecs_vec_u32_get(const vector_t *v, uint32_t i)
{
    return *(uint32_t *)((uint8_t *)v->data + (size_t)i * v->element_size);
}

static inline void ecs_vec_u32_set(vector_t *v, uint32_t i, uint32_t x)
{
    *(uint32_t *)((uint8_t *)v->data + (size_t)i * v->element_size) = x;
}

static inline uint8_t ecs_vec_u8_get(const vector_t *v, uint32_t i)
{
    return *(uint8_t *)((uint8_t *)v->data + (size_t)i * v->element_size);
}

static inline void ecs_vec_u8_set(vector_t *v, uint32_t i, uint8_t x)
{
    *(uint8_t *)((uint8_t *)v->data + (size_t)i * v->element_size) = x;
}

ecs_pool_t *ecs_pool_get_or_create(ecs_world_t *w, ecs_component_id_t type_id);
void ecs_world_ensure_entity_capacity_for_index(ecs_world_t *w, uint32_t new_entity_count);
