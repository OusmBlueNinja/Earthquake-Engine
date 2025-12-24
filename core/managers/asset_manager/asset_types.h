#pragma once
#include <stdint.h>

#include "asset_types/image.h"
#include "asset_types/material.h"
#include "asset_types/model.h"

typedef enum asset_type_t
{
    ASSET_NONE = 0,
    ASSET_IMAGE,
    ASSET_MATERIAL,
    ASSET_MODEL,

} asset_type_t;

typedef enum asset_state_t
{
    ASSET_STATE_EMPTY = 0,
    ASSET_STATE_LOADING = 1,
    ASSET_STATE_READY = 2,
    ASSET_STATE_FAILED = 3
} asset_state_t;

typedef union asset_any_u
{
    asset_image_t image;
    asset_material_t material;
    asset_model_t model;
    model_raw_t model_raw;
} asset_any_u;

typedef struct asset_any_t
{
    asset_type_t type;
    asset_state_t state;
    asset_any_u as;
} asset_any_t;
