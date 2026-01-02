#pragma once
#include <stdint.h>

#include "asset_types/image.h"
#include "asset_types/material.h"
#include "asset_types/model.h"
#include "asset_types/scene.h"

#define ASSET_TYPE_LIST(X) \
    X(ASSET_NONE)          \
    X(ASSET_IMAGE)         \
    X(ASSET_MATERIAL)      \
    X(ASSET_MODEL)         \
    X(ASSET_SCENE)

typedef enum asset_type_t
{
#define X(e) e,
    ASSET_TYPE_LIST(X)
#undef X
        ASSET_MAX
} asset_type_t;

static const char *const g_asset_type_names[ASSET_MAX] =
    {
#define X(e) [e] = #e,
        ASSET_TYPE_LIST(X)
#undef X
};

#define ASSET_TYPE_TO_STRING(t) \
    (((unsigned)(t) < (unsigned)ASSET_MAX) ? g_asset_type_names[(t)] : "ASSET_UNKNOWN")

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
    asset_scene_t scene;
} asset_any_u;

typedef struct asset_any_t
{
    asset_type_t type;
    asset_state_t state;
    asset_any_u as;
} asset_any_t;
