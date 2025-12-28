#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "utils/logger.h"

#include "asset_manager/asset_types/model.h"

#define MODEL_LOD_MAX 0

bool model_raw_generate_lods(model_raw_t *raw);
