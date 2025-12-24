#pragma once
#include <stdint.h>

#include "handle.h"

typedef struct renderer_t renderer_t;

typedef struct ibl_t
{
    uint32_t env_cubemap;
    uint32_t irradiance_map;
    uint32_t prefilter_map;
    uint32_t brdf_lut;

    uint32_t capture_fbo;
    uint32_t capture_rbo;

    uint32_t env_size;
    uint32_t irradiance_size;
    uint32_t prefilter_size;
    uint32_t brdf_size;

    uint8_t equirect_to_cube_shader_id;
    uint8_t irradiance_shader_id;
    uint8_t prefilter_shader_id;
    uint8_t brdf_shader_id;

    ihandle_t src_hdri;
    int ready;
} ibl_t;

int ibl_init(renderer_t *r);
void ibl_shutdown(renderer_t *r);
void ibl_on_resize(renderer_t *r);
void ibl_ensure(renderer_t *r);

uint32_t ibl_get_env(const renderer_t *r);
uint32_t ibl_get_irradiance(const renderer_t *r);
uint32_t ibl_get_prefilter(const renderer_t *r);
uint32_t ibl_get_brdf_lut(const renderer_t *r);
int ibl_is_ready(const renderer_t *r);
