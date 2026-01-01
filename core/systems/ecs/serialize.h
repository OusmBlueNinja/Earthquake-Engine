#pragma once

#include <stdint.h>

#include "ecs_types.h"
#include "types/vector.h"

typedef struct ecs_world_t ecs_world_t;

int ecs_scene_save_to_memory(const ecs_world_t *w, vector_t *out_bytes);
int ecs_scene_load_from_memory(ecs_world_t *w, const uint8_t *bytes, uint32_t size);

int ecs_scene_save_file(const ecs_world_t *w, const char *path);
int ecs_scene_load_file(ecs_world_t *w, const char *path);
