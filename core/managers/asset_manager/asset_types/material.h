#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "types/vec3.h"
#include "handle.h"
#include "ikv1.h"

typedef struct asset_material_t
{
    uint8_t shader_id;
    uint32_t flags;

    vec3 albedo;
    vec3 emissive;
    float roughness;
    float metallic;
    float opacity;

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
