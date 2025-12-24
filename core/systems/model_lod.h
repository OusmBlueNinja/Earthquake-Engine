#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_types/model.h"

#define MODEL_LOD_MAX 8

typedef struct model_lod_settings_t
{
    uint8_t lod_count;
    float triangle_ratio[MODEL_LOD_MAX];
} model_lod_settings_t;

bool model_raw_generate_lods(model_raw_t *raw, const model_lod_settings_t *s);
