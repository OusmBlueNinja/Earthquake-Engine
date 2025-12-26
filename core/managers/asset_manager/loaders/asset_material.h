/* asset_module_material.h */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/material.h"

asset_module_desc_t asset_module_material(void);

bool material_save_file_ikv1(const char *path, const asset_material_t *mat);
bool material_load_file_ikv1(asset_manager_t *am, const char *path, asset_material_t *out_mat);
