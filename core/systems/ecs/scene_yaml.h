#pragma once

#include <stdint.h>

#include "core/systems/ecs/world.h"

typedef struct asset_manager_t asset_manager_t;

// YAML (MiniYAML subset) scene IO used by the editor.
// NOTE: This is separate from ecs_scene_save_file/load_file (binary).
int ecs_scene_save_yaml_file(const ecs_world_t *w, const char *path, const asset_manager_t *am);
int ecs_scene_load_yaml_file(ecs_world_t *w, const char *path, asset_manager_t *am);

