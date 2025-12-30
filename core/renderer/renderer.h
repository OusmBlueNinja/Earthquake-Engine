#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "types/vec2i.h"
#include "types/mat4.h"
#include "types/vec4.h"
#include "handle.h"
#include "renderer/camera.h"
#include "renderer/light.h"
#include "asset_manager/asset_manager.h"
#include "renderer/bloom.h"
#include "renderer/ibl.h"
#include "renderer/ssr.h"
#include "shader.h"

typedef struct pushed_model_t
{
    ihandle_t model;
    mat4 model_matrix;
} pushed_model_t;

typedef struct render_stats_t
{
    uint64_t draw_calls;
    uint64_t triangles;

    uint64_t instanced_draw_calls;
    uint64_t instances;
    uint64_t instanced_triangles;
} render_stats_t;

typedef struct renderer_cfg_t
{
    int bloom;
    int debug_mode;

    float bloom_threshold;
    float bloom_knee;
    float bloom_intensity;
    uint32_t bloom_mips;

    float exposure;
    bool exposure_auto;

    float output_gamma;
    int manual_srgb;

    int alpha_test;
    float alpha_cutoff;

    int height_invert;
    float ibl_intensity;

    int ssr;
    float ssr_intensity;
    int ssr_steps;
    float ssr_stride;
    float ssr_thickness;
    float ssr_max_dist;

    bool shadows;
    int shadow_cascades;
    int shadow_map_size;
    float shadow_max_dist;
    float shadow_split_lambda;
    float shadow_bias;
    float shadow_normal_bias;
    bool shadow_pcf;

    bool wireframe;
} renderer_cfg_t;

#define R_SHADOW_MAX_CASCADES 4

typedef struct renderer_shadow_t
{
    uint32_t fbo;
    uint32_t tex;

    int size;
    int cascades;

    int light_index;
    mat4 vp[R_SHADOW_MAX_CASCADES];
    float splits[R_SHADOW_MAX_CASCADES];
} renderer_shadow_t;

typedef struct renderer_fp_t
{
    uint8_t shader_init_id;
    uint8_t shader_cull_id;
    uint8_t shader_finalize_id;

    uint32_t lights_ssbo;
    uint32_t tile_index_ssbo;
    uint32_t tile_list_ssbo;
    uint32_t tile_depth_ssbo;

    uint32_t lights_cap;
    uint32_t tile_max;

    int tile_count_x;
    int tile_count_y;
    int tiles;

} renderer_fp_t;

typedef struct renderer_t
{
    asset_manager_t *assets;

    vec2i fb_size;
    vec2i fb_size_last;
    vec4 clear_color;

    renderer_cfg_t cfg;

    camera_t camera;

    vector_t lights;
    vector_t models;
    vector_t fwd_models;
    vector_t shaders;

    ihandle_t hdri_tex;

    uint32_t instance_vbo;
    uint32_t instance_cap;

    vector_t inst_batches;
    vector_t fwd_inst_batches;
    vector_t inst_mats;

    vector_t shadow_inst_batches;
    vector_t shadow_inst_mats;

    uint32_t fs_vao;

    uint32_t gbuf_fbo;
    uint32_t light_fbo;
    uint32_t final_fbo;

    uint32_t gbuf_albedo;
    uint32_t gbuf_normal;
    uint32_t gbuf_material;
    uint32_t gbuf_depth;
    uint32_t gbuf_emissive;

    uint32_t light_color_tex;
    uint32_t final_color_tex;

    uint8_t gbuf_shader_id;
    uint8_t light_shader_id;
    uint8_t default_shader_id;
    uint8_t sky_shader_id;
    uint8_t present_shader_id;

    uint8_t depth_shader_id;
    uint8_t shadow_shader_id;

    uint32_t black_tex;
    uint32_t black_cube;

    renderer_fp_t fp;

    ibl_t ibl;
    bloom_t bloom;
    ssr_t ssr;

    renderer_shadow_t shadow;

    render_stats_t stats[2]; // Dubble Buff
    bool stats_write;

} renderer_t;

int R_init(renderer_t *r, asset_manager_t *assets);
void R_shutdown(renderer_t *r);
void R_resize(renderer_t *r, vec2i size);
void R_set_clear_color(renderer_t *r, vec4 color);

void R_begin_frame(renderer_t *r);
void R_end_frame(renderer_t *r);

void R_push_camera(renderer_t *r, const camera_t *cam);
void R_push_light(renderer_t *r, light_t light);
void R_push_model(renderer_t *r, const ihandle_t model, mat4 model_matrix);

void R_push_hdri(renderer_t *r, ihandle_t tex);

const render_stats_t *R_get_stats(const renderer_t *r);

uint8_t R_add_shader(renderer_t *r, shader_t *shader);
shader_t *R_get_shader(const renderer_t *r, uint8_t shader_id);
const shader_t *R_get_shader_const(const renderer_t *r, uint8_t shader_id);
uint32_t R_get_final_fbo(const renderer_t *r);

shader_t *R_new_shader_from_files(const char *vp, const char *fp);
