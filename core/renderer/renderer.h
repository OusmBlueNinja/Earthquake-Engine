#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "types/vec2i.h"
#include "types/mat4.h"
#include "types/vec3.h"
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

typedef enum render_gpu_phase_t
{
    R_GPU_SHADOW = 0,
    R_GPU_DEPTH_PREPASS,
    R_GPU_RESOLVE_DEPTH,
    R_GPU_FP_CULL,
    R_GPU_SKY,
    R_GPU_FORWARD,
    R_GPU_RESOLVE_COLOR,
    R_GPU_AUTO_EXPOSURE,
    R_GPU_BLOOM,
    R_GPU_COMPOSITE,
    R_GPU_PHASE_COUNT
} render_gpu_phase_t;

typedef struct render_gpu_timings_t
{
    uint8_t valid;
    double ms[R_GPU_PHASE_COUNT];
} render_gpu_timings_t;

typedef enum render_cpu_phase_t
{
    R_CPU_BUILD_INSTANCING = 0,
    R_CPU_BUILD_SHADOW_INSTANCING,
    R_CPU_SHADOW,
    R_CPU_DEPTH_PREPASS,
    R_CPU_RESOLVE_DEPTH,
    R_CPU_FP_CULL,
    R_CPU_SKY,
    R_CPU_FORWARD,
    R_CPU_RESOLVE_COLOR,
    R_CPU_AUTO_EXPOSURE,
    R_CPU_BLOOM,
    R_CPU_COMPOSITE,
    R_CPU_PHASE_COUNT
} render_cpu_phase_t;

typedef struct render_cpu_timings_t
{
    uint8_t valid;
    double ms[R_CPU_PHASE_COUNT];
} render_cpu_timings_t;

typedef struct renderer_cfg_t
{
    bool bloom;
    bool shadows;
    bool msaa;
    int msaa_samples;

    bool wireframe;
    int debug_mode;
} renderer_cfg_t;

#define R_SHADOW_MAX_CASCADES 4

typedef struct renderer_scene_settings_t
{
    float bloom_threshold;
    float bloom_knee;
    float bloom_intensity;
    uint32_t bloom_mips;

    float exposure;
    bool exposure_auto;
    float delta_time;
    float auto_exposure_speed;
    float auto_exposure_min;
    float auto_exposure_max;
    float output_gamma;
    bool manual_srgb;

    bool alpha_test;
    float alpha_cutoff;
    bool height_invert;

    float ibl_intensity;

    bool ssr;
    float ssr_intensity;
    int ssr_steps;
    float ssr_stride;
    float ssr_thickness;
    float ssr_max_dist;

    int shadow_cascades;
    int shadow_map_size;
    float shadow_max_dist;
    float shadow_split_lambda;
    float shadow_bias;
    float shadow_normal_bias;
    bool shadow_pcf;
} renderer_scene_settings_t;

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

typedef enum line3d_flags_t
{
    LINE3D_TRANSLUCENT = 1u << 0,
    LINE3D_ON_TOP = 1u << 1
} line3d_flags_t;

typedef struct line3d_t
{
    vec3 a;
    vec3 b;
    vec4 color;
    line3d_flags_t flags;

} line3d_t;

typedef struct renderer_t
{
    asset_manager_t *assets;

    struct frame_graph_t *fg;

    vec2i fb_size;
    vec2i fb_size_last;
    vec4 clear_color;

    renderer_cfg_t cfg;
    renderer_scene_settings_t scene;
    float exposure_adapted;
    bool exposure_adapted_valid;
    float exposure_readback_accum;
    uint32_t exposure_pbo[2];
    uint8_t exposure_pbo_index;
    uint8_t exposure_pbo_valid;

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

    vector_t lines3d;
    uint32_t line3d_vao;
    uint32_t line3d_vbo;
    uint32_t line3d_vbo_cap_vertices;
    uint8_t line3d_shader_id;

    uint32_t gbuf_fbo;
    uint32_t light_fbo;
    uint32_t final_fbo;
    uint32_t msaa_fbo;
    uint32_t msaa_color_rb;
    uint32_t msaa_depth_rb;
    int msaa_samples;

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

    render_gpu_timings_t gpu_timings;
    render_cpu_timings_t cpu_timings;
    uint32_t gpu_queries[R_GPU_PHASE_COUNT][16];
    uint32_t gpu_query_index;
    uint8_t gpu_timer_active;

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
void R_push_line3d(renderer_t *r, line3d_t line);

void R_push_hdri(renderer_t *r, ihandle_t tex);

renderer_scene_settings_t R_scene_settings_default(void);
void R_push_scene_settings(renderer_t *r, const renderer_scene_settings_t *settings);

const render_stats_t *R_get_stats(const renderer_t *r);
const render_gpu_timings_t *R_get_gpu_timings(const renderer_t *r);
const render_cpu_timings_t *R_get_cpu_timings(const renderer_t *r);

uint8_t R_add_shader(renderer_t *r, shader_t *shader);
shader_t *R_get_shader(const renderer_t *r, uint8_t shader_id);
const shader_t *R_get_shader_const(const renderer_t *r, uint8_t shader_id);
uint32_t R_get_final_fbo(const renderer_t *r);
uint32_t R_get_final_color_tex(const renderer_t *r);

shader_t *R_new_shader_from_files(const char *vp, const char *fp);

//! private
void R_update_resize(renderer_t *r);
