#pragma once
#include <stdbool.h>

#include "asset_manager/asset_manager.h"
#include "asset_image.h"
#include "asset_material.h"
#include "asset_model.h"

#define REGISTER_ASSET_MODULE(am, module_fn)                                              \
    do                                                                                    \
    {                                                                                     \
        asset_module_desc_t m = module_fn();                                              \
        if (!asset_manager_register_module((am), m))                                      \
            LOG_ERROR("Failed to register module %s", m.name ? m.name : "ASSET_UNKNOWN"); \
        else                                                                              \
            LOG_DEBUG("Registered module %s", m.name ? m.name : "ASSET_UNKNOWN");         \
    } while (0)

static inline void register_asset_modules(asset_manager_t *am)
{
    REGISTER_ASSET_MODULE(am, asset_module_image);
    REGISTER_ASSET_MODULE(am, asset_module_material);
    REGISTER_ASSET_MODULE(am, asset_module_model);
}
