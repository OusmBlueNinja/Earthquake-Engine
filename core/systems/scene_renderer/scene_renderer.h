#pragma once

#include <stdint.h>

#include "core/systems/ecs/ecs.h"

typedef struct renderer_t renderer_t;

void scene_renderer_render(renderer_t *r, ecs_world_t *scene);
