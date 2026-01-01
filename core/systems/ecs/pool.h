#pragma once

#include <stdint.h>
#include "ecs_types.h"

typedef struct ecs_world_t ecs_world_t;

void *ecs_add_raw(ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id);
void *ecs_get_raw(ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id);
int ecs_has_raw(const ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id);
int ecs_remove_raw(ecs_world_t *w, ecs_entity_t e, ecs_component_id_t type_id);

uint32_t ecs_count_raw(const ecs_world_t *w, ecs_component_id_t type_id);
void *ecs_dense_raw(ecs_world_t *w, ecs_component_id_t type_id);
ecs_entity_t ecs_entity_at_raw(const ecs_world_t *w, ecs_component_id_t type_id, uint32_t dense_index);

#define ecs_add(w, e, T) ((T *)ecs_add_raw((w), (e), ecs_register_component((w), T)))
#define ecs_get(w, e, T) ((T *)ecs_get_raw((w), (e), ecs_register_component((w), T)))
#define ecs_has(w, e, T) (ecs_has_raw((w), (e), ecs_register_component((w), T)))
#define ecs_remove(w, e, T) (ecs_remove_raw((w), (e), ecs_register_component((w), T)))

#define ecs_count(w, T) ecs_count_raw((w), ecs_register_component((w), T))
#define ecs_dense(w, T) ((T *)ecs_dense_raw((w), ecs_register_component((w), T)))
#define ecs_entity_at(w, T, i) ecs_entity_at_raw((w), ecs_register_component((w), T), (i))
