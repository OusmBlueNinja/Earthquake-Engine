#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/material.h"

static bool asset_material_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset)
{
    (void)am;
    if (path_is_ptr)
        return false;

    asset_material_t m;
    if (!material_load_file(path, &m))
        return false;

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MATERIAL;
    out_asset->state = ASSET_STATE_LOADING;

    out_asset->as.material = m;

    return true;
}

static bool asset_material_init(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MATERIAL)
        return false;

    return true;
}

static void asset_material_cleanup(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MATERIAL)
        return;

    memset(&asset->as.material, 0, sizeof(asset->as.material));
}

static asset_module_desc_t asset_module_material(void)
{
    asset_module_desc_t m;
    m.type = ASSET_MATERIAL;
    m.name = "ASSET_MATERIAL";
    m.load_fn = asset_material_load;
    m.init_fn = asset_material_init;
    m.cleanup_fn = asset_material_cleanup;
    return m;
}
