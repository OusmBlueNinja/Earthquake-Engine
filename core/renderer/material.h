#pragma once
#include <stdint.h>
#include "types/vec3.h"
#include "handle.h"

typedef struct material_t
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
} material_t;
