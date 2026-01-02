#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_types/image.h"

// Builds a full mip chain from a decoded base level. Returned chain owns `data` and must be freed.
// Note: `out->data` uses tightly packed rows (no padding).
bool asset_image_mips_build_u8(asset_image_mip_chain_t **out, const uint8_t *base_rgba, uint32_t w, uint32_t h, uint32_t channels);
bool asset_image_mips_build_f32(asset_image_mip_chain_t **out, const float *base_rgb, uint32_t w, uint32_t h, uint32_t channels);

void asset_image_mips_free(asset_image_mip_chain_t *mips);

