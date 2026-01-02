#pragma once

#include <stdint.h>

#include "core/types/vector.h"

// Scene assets are stored as text (YAML-ish) and owned in CPU memory.
typedef struct asset_scene_t
{
    vector_t text; // element_size=1 (bytes), null-terminated for convenience
} asset_scene_t;

