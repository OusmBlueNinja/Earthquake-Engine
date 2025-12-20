#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "types/vec2i.h"
#include "types/vec4.h"
#include "renderer/camera.h"
#include "renderer/light.h"
#include "renderer/model.h"
#include "renderer/material.h"
#include "asset_manager/asset_manager.h"
#include "vector.h"
#include "shader.h"

typedef struct renderer_config_t
{
    int debug_mode;
    bool bloom;
    float bloom_threshold;
    float bloom_knee;
    float bloom_intensity;
    uint32_t bloom_mips;
} renderer_config_t;

typedef struct pushed_model_t
{
    const model_t *model;
    mat4 model_matrix;
} pushed_model_t;

typedef struct renderer_bloom_t
{
    uint32_t enabled;

    uint32_t mips;
    vec2i base_size;

    uint32_t tex_down[16];
    uint32_t tex_up[16];

    uint32_t fbo_dummy;

    shader_t *cs_extract;
    shader_t *cs_down;
    shader_t *cs_up;

    shader_t *post_present;
} renderer_bloom_t;

typedef struct renderer_t
{
    asset_manager_t *assets;

    renderer_config_t cfg;

    vec4 clear_color;
    vec2i fb_size;

    uint32_t scene_fbo;
    uint32_t final_fbo;

    uint32_t fs_vao;

    uint32_t scene_color_tex;
    uint32_t final_color_tex;
    uint32_t depth_tex;

    vector_t lights;
    vector_t models;
    vector_t shaders;

    uint8_t default_shader_id;

    camera_t camera;

    uint32_t lights_ubo;

    model_factory_t model_factory;

    renderer_bloom_t bloom;
} renderer_t;

int R_init(renderer_t *r, asset_manager_t *assets);
void R_shutdown(renderer_t *r);

void R_resize(renderer_t *r, vec2i size);
void R_set_clear_color(renderer_t *r, vec4 color);

void R_begin_frame(renderer_t *r);
void R_end_frame(renderer_t *r);

void R_push_camera(renderer_t *r, const camera_t *cam);
void R_push_light(renderer_t *r, light_t light);
void R_push_model(renderer_t *r, const model_t *model, mat4 model_matrix);

uint8_t R_add_shader(renderer_t *r, shader_t *shader);
shader_t *R_get_shader(const renderer_t *r, uint8_t shader_id);
const shader_t *R_get_shader_const(const renderer_t *r, uint8_t shader_id);

uint32_t R_get_final_fbo(const renderer_t *r);
