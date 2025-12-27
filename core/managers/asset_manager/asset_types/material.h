#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "types/vec3.h"
#include "handle.h"
#include "ikv1.h"

typedef struct asset_manager_t asset_manager_t;

typedef enum material_flags_t
{
    MAT_FLAG_DOUBLE_SIDED = 1u << 0,
    MAT_FLAG_ALPHA_CUTOUT = 1u << 1,
    MAT_FLAG_ALPHA_BLEND = 1u << 2,
    MAT_FLAG_UNLIT = 1u << 3,
} material_flags_t;

typedef struct asset_material_t
{
    uint8_t shader_id;
    material_flags_t flags;
    char *name;

    vec3 albedo;
    vec3 emissive;
    float roughness;
    float metallic;
    float opacity;

    float alpha_cutoff;

    float normal_strength;
    float height_scale;
    int height_steps;

    ihandle_t albedo_tex;
    ihandle_t normal_tex;
    ihandle_t metallic_tex;
    ihandle_t roughness_tex;
    ihandle_t emissive_tex;
    ihandle_t occlusion_tex;
    ihandle_t height_tex;
    ihandle_t arm_tex;

} asset_material_t;

asset_material_t material_make_default(uint8_t shader_id);

ikv_node_t *material_to_ikv(const asset_material_t *m, const char *key);
bool material_from_ikv(const ikv_node_t *node, asset_material_t *out);

bool material_save_file(const char *path, const asset_material_t *m);

bool material_load_file(const char *path, asset_material_t *out);
bool material_load_file_any(asset_manager_t *am, const char *path, const char *want_name, asset_material_t *out);

void material_set_flag(asset_material_t *m, material_flags_t flag, bool state);
