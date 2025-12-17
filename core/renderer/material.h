#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "types/vec3.h"

typedef enum material_texture_slot_t
{
    MATERIAL_TEXTURE_ALBEDO = 0,
    MATERIAL_TEXTURE_NORMAL,
    MATERIAL_TEXTURE_METALLIC,
    MATERIAL_TEXTURE_ROUGHNESS,
    MATERIAL_TEXTURE_EMISSIVE,
    MATERIAL_TEXTURE_OCCLUSION,
    MATERIAL_TEXTURE_HEIGHT,
    MATERIAL_TEXTURE_CUSTOM,
    MATERIAL_TEXTURE_MAX
} material_texture_slot_t;

typedef struct material_t
{
    uint8_t shader_id;
    uint32_t flags;

    vec3 albedo;
    vec3 emissive;
    float roughness;
    float metallic;
    float opacity;

    uint32_t textures[MATERIAL_TEXTURE_MAX];
} material_t;
