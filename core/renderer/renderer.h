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

    bool wireframe;

    int pt_enabled;
    int pt_spp;
    float pt_env_intensity;

    int pt_halfres;
    int pt_bounces;
    float pt_rebuild_eps;

} renderer_cfg_t;

typedef struct renderer_pt_t
{
    uint8_t shader_trace_id;
    uint8_t shader_finalize_id;

    uint32_t accum_tex;
    uint32_t accum_half_tex;
    uint64_t last_scene_hash;
    uint32_t last_scene_count;
    uint8_t shader_bvh_refit_id;

    uint32_t tris_ssbo;
    uint32_t bvh_ssbo;
    uint32_t mats_ssbo;

    uint32_t tri_cap;
    uint32_t node_cap;
    uint32_t mat_cap;

    uint32_t tri_count;
    uint32_t node_count;
    uint32_t mat_count;

    uint32_t frame_index;

    int scene_dirty;
    int reset_accum;

    mat4 last_view;
    mat4 last_proj;
    vec3 last_cam_pos;
} renderer_pt_t;

typedef struct renderer_t
{
    asset_manager_t *assets;

    vec2i fb_size;
    vec4 clear_color;

    renderer_cfg_t cfg;

    camera_t camera;

    vector_t lights;
    vector_t models;
    vector_t fwd_models;
    vector_t shaders;

    ihandle_t hdri_tex;

    uint32_t fs_vao;

    uint32_t gbuf_depth;

    uint32_t final_fbo;
    uint32_t final_color_tex;

    uint32_t present_fbo;
    uint32_t present_color_tex;

    uint32_t black_tex;
    uint32_t black_cube;

    uint8_t default_shader_id;
    uint8_t sky_shader_id;
    uint8_t present_shader_id;
    uint8_t depth_shader_id;

    render_stats_t stats[2];
    bool stats_write;

    renderer_pt_t pt;

    bloom_t bloom;
    ssr_t ssr;
    ibl_t ibl;

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
uint32_t R_get_present_tex(const renderer_t *r);

shader_t *R_new_shader_from_files(const char *vp, const char *fp);
