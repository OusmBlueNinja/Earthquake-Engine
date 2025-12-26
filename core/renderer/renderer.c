#include "renderer/renderer.h"
#include "renderer/ibl.h"
#include "renderer/bloom.h"
#include "renderer/ssr.h"
#include "shader.h"
#include "utils/logger.h"
#include "utils/macros.h"
#include "cvar.h"
#include "core.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#define FP_TILE_SIZE 16
#define FP_TILE_MAX_CAP 65536u
#define FP_TILE_MAX_MIN 64u

typedef struct gpu_light_t
{
    float position[4];
    float direction[4];
    float color[4];
    float params[4];
    int meta[4];
} gpu_light_t;

enum material_tex_flags
{
    MAT_TEX_ALBEDO = 1 << 0,
    MAT_TEX_NORMAL = 1 << 1,
    MAT_TEX_METALLIC = 1 << 2,
    MAT_TEX_ROUGHNESS = 1 << 3,
    MAT_TEX_EMISSIVE = 1 << 4,
    MAT_TEX_OCCLUSION = 1 << 5,
    MAT_TEX_HEIGHT = 1 << 6,
    MAT_TEX_ARM = 1 << 7
};

typedef struct inst_batch_t
{
    ihandle_t model;
    uint32_t mesh_index;
    uint32_t lod;
    uint32_t start;
    uint32_t count;
} inst_batch_t;

typedef struct inst_item_t
{
    ihandle_t model;
    uint32_t mesh_index;
    uint32_t lod;
    mat4 m;
} inst_item_t;

static uint32_t g_black_tex = 0;
static uint32_t g_black_cube = 0;

static uint32_t g_fp_prog_init = 0;
static uint32_t g_fp_prog_cull = 0;
static uint32_t g_fp_prog_finalize = 0;

static uint32_t g_fp_lights_ssbo = 0;
static uint32_t g_fp_tile_index_ssbo = 0;
static uint32_t g_fp_tile_list_ssbo = 0;

static uint32_t g_fp_lights_cap = 0;
static uint32_t g_fp_tile_max = FP_TILE_MAX_MIN;

static int g_fp_tile_count_x = 1;
static int g_fp_tile_count_y = 1;
static int g_fp_tiles = 1;

static uint32_t u32_next_pow2(uint32_t x)
{
    if (x <= 1u)
        return 1u;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1u;
}

static uint32_t u32_clamp(uint32_t x, uint32_t lo, uint32_t hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static void R_stats_begin_frame(renderer_t *r)
{
    memset(&r->stats, 0, sizeof(r->stats));
}

static void R_stats_add_draw(renderer_t *r, uint32_t index_count)
{
    r->stats.draw_calls += 1;
    r->stats.triangles += (uint64_t)(index_count / 3u);
}

static void R_stats_add_draw_instanced(renderer_t *r, uint32_t index_count, uint32_t instance_count)
{
    r->stats.draw_calls += 1;
    r->stats.instanced_draw_calls += 1;
    r->stats.instances += (uint64_t)instance_count;

    uint64_t tris = (uint64_t)(index_count / 3u);
    r->stats.triangles += tris * (uint64_t)instance_count;
    r->stats.instanced_triangles += tris * (uint64_t)instance_count;
}

static void R_make_black_tex(void)
{
    if (g_black_tex)
        return;

    glGenTextures(1, &g_black_tex);
    glBindTexture(GL_TEXTURE_2D, g_black_tex);

    {
        const float px[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, px);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
}

static void R_make_black_cube(void)
{
    if (g_black_cube)
        return;

    glGenTextures(1, &g_black_cube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_black_cube);

    {
        const float px[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int face = 0; face < 6; ++face)
            glTexImage2D((GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, px);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

static void R_cfg_pull_from_cvars(renderer_t *r)
{
    r->cfg.bloom = cvar_get_bool_name("cl_bloom") ? 1 : 0;
    r->cfg.debug_mode = cvar_get_int_name("cl_render_debug");

    r->cfg.bloom_threshold = cvar_get_float_name("cl_r_bloom_threshold");
    r->cfg.bloom_knee = cvar_get_float_name("cl_r_bloom_knee");
    r->cfg.bloom_intensity = cvar_get_float_name("cl_r_bloom_intensity");

    {
        int32_t m = cvar_get_int_name("cl_r_bloom_mips");
        if (m < 1)
            m = 1;
        if (m > 10)
            m = 10;
        r->cfg.bloom_mips = (uint32_t)m;
    }

    r->cfg.exposure = cvar_get_float_name("cl_r_exposure");
    r->cfg.output_gamma = cvar_get_float_name("cl_r_output_gamma");
    r->cfg.manual_srgb = cvar_get_bool_name("cl_r_manual_srgb") ? 1 : 0;

    r->cfg.alpha_test = cvar_get_bool_name("cl_r_alpha_test") ? 1 : 0;
    r->cfg.alpha_cutoff = cvar_get_float_name("cl_r_alpha_cutoff");

    r->cfg.height_invert = cvar_get_bool_name("cl_r_height_invert") ? 1 : 0;
    r->cfg.ibl_intensity = cvar_get_float_name("cl_r_ibl_intensity");

    r->cfg.ssr = cvar_get_bool_name("cl_r_ssr") ? 1 : 0;
    r->cfg.ssr_intensity = cvar_get_float_name("cl_r_ssr_intensity");

    r->cfg.ssr_steps = cvar_get_int_name("cl_r_ssr_steps");
    r->cfg.ssr_stride = cvar_get_float_name("cl_r_ssr_stride");
    r->cfg.ssr_thickness = cvar_get_float_name("cl_r_ssr_thickness");
    r->cfg.ssr_max_dist = cvar_get_float_name("cl_r_ssr_max_dist");

    r->cfg.wireframe = cvar_get_bool_name("cl_r_wireframe") ? 1 : 0;
}

static void R_on_cvar_any(renderer_t *r)
{
    R_cfg_pull_from_cvars(r);

    bloom_set_params(r,
                     r->cfg.bloom_threshold,
                     r->cfg.bloom_knee,
                     r->cfg.bloom_intensity,
                     r->cfg.bloom_mips);
}

static void R_on_bloom_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    (void)state;
    renderer_t *r = &get_application()->renderer;
    R_on_cvar_any(r);
}

static void R_on_debug_mode_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    (void)state;
    renderer_t *r = &get_application()->renderer;
    R_on_cvar_any(r);
}

static void R_on_r_cvar_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    (void)state;
    renderer_t *r = &get_application()->renderer;
    R_on_cvar_any(r);
}

static void R_on_wireframe_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    (void)state;
    renderer_t *r = &get_application()->renderer;
    R_on_cvar_any(r);
}

shader_t *R_new_shader_from_files_with_defines(const char *vp, const char *fp)
{
    shader_t tmp = shader_create();
    if (!shader_load_from_files(&tmp, vp, fp))
    {
        shader_destroy(&tmp);
        return NULL;
    }

    shader_t *out = (shader_t *)malloc(sizeof(shader_t));
    if (!out)
    {
        shader_destroy(&tmp);
        return NULL;
    }

    *out = tmp;
    return out;
}

static void R_alloc_tex2d(uint32_t *tex, uint32_t internal, int w, int h, uint32_t format, uint32_t type, uint32_t minf, uint32_t magf)
{
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLenum)internal, w, h, 0, (GLenum)format, (GLenum)type, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLenum)minf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLenum)magf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void R_gl_delete_targets(renderer_t *r)
{
    if (r->gbuf_fbo)
        glDeleteFramebuffers(1, &r->gbuf_fbo);
    if (r->light_fbo)
        glDeleteFramebuffers(1, &r->light_fbo);
    if (r->final_fbo)
        glDeleteFramebuffers(1, &r->final_fbo);

    if (r->gbuf_albedo)
        glDeleteTextures(1, &r->gbuf_albedo);
    if (r->gbuf_normal)
        glDeleteTextures(1, &r->gbuf_normal);
    if (r->gbuf_material)
        glDeleteTextures(1, &r->gbuf_material);
    if (r->gbuf_depth)
        glDeleteTextures(1, &r->gbuf_depth);
    if (r->gbuf_emissive)
        glDeleteTextures(1, &r->gbuf_emissive);

    if (r->light_color_tex)
        glDeleteTextures(1, &r->light_color_tex);
    if (r->final_color_tex)
        glDeleteTextures(1, &r->final_color_tex);

    r->gbuf_fbo = 0;
    r->light_fbo = 0;
    r->final_fbo = 0;

    r->gbuf_albedo = 0;
    r->gbuf_normal = 0;
    r->gbuf_material = 0;
    r->gbuf_depth = 0;
    r->gbuf_emissive = 0;

    r->light_color_tex = 0;
    r->final_color_tex = 0;
}

static void R_fp_delete_buffers(void)
{
    if (g_fp_lights_ssbo)
        glDeleteBuffers(1, &g_fp_lights_ssbo);
    if (g_fp_tile_index_ssbo)
        glDeleteBuffers(1, &g_fp_tile_index_ssbo);
    if (g_fp_tile_list_ssbo)
        glDeleteBuffers(1, &g_fp_tile_list_ssbo);

    g_fp_lights_ssbo = 0;
    g_fp_tile_index_ssbo = 0;
    g_fp_tile_list_ssbo = 0;

    g_fp_lights_cap = 0;
    g_fp_tile_max = FP_TILE_MAX_MIN;

    g_fp_tile_count_x = 1;
    g_fp_tile_count_y = 1;
    g_fp_tiles = 1;
}

static void R_fp_ensure_lights_capacity(uint32_t needed)
{
    if (needed < 1u)
        needed = 1u;

    if (needed <= g_fp_lights_cap && g_fp_lights_ssbo)
        return;

    uint32_t new_cap = u32_next_pow2(needed);
    if (new_cap < 256u)
        new_cap = 256u;

    if (!g_fp_lights_ssbo)
        glGenBuffers(1, &g_fp_lights_ssbo);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_fp_lights_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(gpu_light_t) * (size_t)new_cap), 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    g_fp_lights_cap = new_cap;
}

static uint32_t R_fp_pick_tile_max(uint32_t light_count)
{
    uint32_t want = u32_next_pow2(light_count);
    want = u32_clamp(want, FP_TILE_MAX_MIN, FP_TILE_MAX_CAP);
    return want;
}

static void R_fp_resize_tile_buffers(vec2i fb, uint32_t light_count)
{
    if (fb.x < 1)
        fb.x = 1;
    if (fb.y < 1)
        fb.y = 1;

    g_fp_tile_count_x = (fb.x + (FP_TILE_SIZE - 1)) / FP_TILE_SIZE;
    g_fp_tile_count_y = (fb.y + (FP_TILE_SIZE - 1)) / FP_TILE_SIZE;
    g_fp_tiles = g_fp_tile_count_x * g_fp_tile_count_y;
    if (g_fp_tiles < 1)
        g_fp_tiles = 1;

    uint32_t new_tile_max = R_fp_pick_tile_max(light_count);
    if (new_tile_max != g_fp_tile_max || !g_fp_tile_index_ssbo || !g_fp_tile_list_ssbo)
    {
        g_fp_tile_max = new_tile_max;

        if (!g_fp_tile_index_ssbo)
            glGenBuffers(1, &g_fp_tile_index_ssbo);
        if (!g_fp_tile_list_ssbo)
            glGenBuffers(1, &g_fp_tile_list_ssbo);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_fp_tile_index_ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * 2u * (size_t)g_fp_tiles), 0, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_fp_tile_list_ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * (size_t)g_fp_tiles * (size_t)g_fp_tile_max), 0, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}

static void R_create_targets(renderer_t *r)
{
    R_gl_delete_targets(r);

    if (r->fb_size.x < 1)
        r->fb_size.x = 1;
    if (r->fb_size.y < 1)
        r->fb_size.y = 1;

    r->gbuf_fbo = 0;
    r->gbuf_albedo = 0;
    r->gbuf_normal = 0;
    r->gbuf_material = 0;
    r->gbuf_emissive = 0;

    R_alloc_tex2d(&r->gbuf_depth, GL_DEPTH_COMPONENT32F, r->fb_size.x, r->fb_size.y, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST);

    glGenFramebuffers(1, &r->light_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);

    R_alloc_tex2d(&r->light_color_tex, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->light_color_tex, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->gbuf_depth, 0);

    {
        uint32_t bufs[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, (const GLenum *)bufs);
    }

    {
        uint32_t status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_ERROR("Scene FBO incomplete: 0x%x", (unsigned)status);
    }

    glGenFramebuffers(1, &r->final_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->final_fbo);

    R_alloc_tex2d(&r->final_color_tex, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->final_color_tex, 0);

    {
        uint32_t bufs[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, (const GLenum *)bufs);
    }

    {
        uint32_t status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_ERROR("Final FBO incomplete: 0x%x", (unsigned)status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    R_fp_resize_tile_buffers(r->fb_size, 1u);
    R_fp_ensure_lights_capacity(1u);
}

static uint32_t R_resolve_image_gl(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return 0;
    if (!ihandle_is_valid(h))
        return 0;

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return 0;
    if (a->type != ASSET_IMAGE)
        return 0;
    if (a->state != ASSET_STATE_READY)
        return 0;

    return a->as.image.gl_handle;
}

static asset_material_t *R_resolve_material(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return NULL;
    if (!ihandle_is_valid(h))
        return NULL;

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return NULL;
    if (a->type != ASSET_MATERIAL)
        return NULL;
    if (a->state != ASSET_STATE_READY)
        return NULL;

    return (asset_material_t *)&a->as.material;
}

static asset_model_t *R_resolve_model(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return NULL;
    if (!ihandle_is_valid(h))
        return NULL;

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return NULL;
    if (a->type != ASSET_MODEL)
        return NULL;
    if (a->state != ASSET_STATE_READY)
        return NULL;

    return (asset_model_t *)&a->as.model;
}

static void R_bind_image_slot_mask(renderer_t *r, shader_t *s, const char *sampler_name, int unit, ihandle_t h, uint32_t bit, uint32_t *mask)
{
    uint32_t glh = R_resolve_image_gl(r, h);

    glActiveTexture((GLenum)(GL_TEXTURE0 + (GLenum)unit));

    if (glh)
    {
        glBindTexture(GL_TEXTURE_2D, glh);
        *mask |= bit;
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, g_black_tex);
    }

    shader_set_int(s, sampler_name, unit);
}

static void R_draw_fs_tri(renderer_t *r)
{
    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void R_apply_material_or_default(renderer_t *r, shader_t *s, asset_material_t *mat)
{
    shader_set_int(s, "u_HasMaterial", mat ? 1 : 0);

    if (mat)
    {
        shader_set_vec3(s, "u_Albedo", mat->albedo);
        shader_set_vec3(s, "u_Emissive", mat->emissive);
        shader_set_float(s, "u_Roughness", mat->roughness);
        shader_set_float(s, "u_Metallic", mat->metallic);
        shader_set_float(s, "u_Opacity", mat->opacity);

        shader_set_float(s, "u_NormalStrength", mat->normal_strength);
        shader_set_float(s, "u_HeightScale", mat->height_scale);
        shader_set_int(s, "u_HeightSteps", mat->height_steps);

        uint32_t tex_mask = 0;

        R_bind_image_slot_mask(r, s, "u_AlbedoTex", 0, mat->albedo_tex, MAT_TEX_ALBEDO, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_NormalTex", 1, mat->normal_tex, MAT_TEX_NORMAL, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_MetallicTex", 2, mat->metallic_tex, MAT_TEX_METALLIC, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_RoughnessTex", 3, mat->roughness_tex, MAT_TEX_ROUGHNESS, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_EmissiveTex", 4, mat->emissive_tex, MAT_TEX_EMISSIVE, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_OcclusionTex", 5, mat->occlusion_tex, MAT_TEX_OCCLUSION, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_HeightTex", 6, mat->height_tex, MAT_TEX_HEIGHT, &tex_mask);
        R_bind_image_slot_mask(r, s, "u_ArmTex", 7, mat->arm_tex, MAT_TEX_ARM, &tex_mask);

        shader_set_int(s, "u_MaterialTexMask", (int)tex_mask);
    }
    else
    {
        for (int ti = 0; ti < 8; ++ti)
        {
            glActiveTexture((GLenum)(GL_TEXTURE0 + (GLenum)ti));
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        shader_set_int(s, "u_MaterialTexMask", 0);

        shader_set_vec3(s, "u_Albedo", (vec3){1.0f, 1.0f, 1.0f});
        shader_set_vec3(s, "u_Emissive", (vec3){0.0f, 0.0f, 0.0f});
        shader_set_float(s, "u_Roughness", 1.0f);
        shader_set_float(s, "u_Metallic", 0.0f);
        shader_set_float(s, "u_Opacity", 1.0f);

        shader_set_float(s, "u_NormalStrength", 1.0f);
        shader_set_float(s, "u_HeightScale", 0.0f);
        shader_set_int(s, "u_HeightSteps", 0);
    }
}

static void R_bind_common_uniforms(renderer_t *r, shader_t *s)
{
    shader_set_mat4(s, "u_View", r->camera.view);
    shader_set_mat4(s, "u_Proj", r->camera.proj);
    shader_set_vec3(s, "u_CameraPos", r->camera.position);

    shader_set_int(s, "u_HeightInvert", r->cfg.height_invert ? 1 : 0);
    shader_set_int(s, "u_AlphaTest", r->cfg.alpha_test ? 1 : 0);
    shader_set_float(s, "u_AlphaCutoff", r->cfg.alpha_cutoff);
    shader_set_int(s, "u_ManualSRGB", r->cfg.manual_srgb ? 1 : 0);
}

static int R_force_lod_level(void)
{
    int v = cvar_get_int_name("cl_r_force_lod_level");
    if (v < -1)
        v = -1;
    if (v > 31)
        v = 31;
    return v;
}

static float R_mat4_max_scale_xyz(const mat4 *m)
{
    float sx = sqrtf(m->m[0] * m->m[0] + m->m[1] * m->m[1] + m->m[2] * m->m[2]);
    float sy = sqrtf(m->m[4] * m->m[4] + m->m[5] * m->m[5] + m->m[6] * m->m[6]);
    float sz = sqrtf(m->m[8] * m->m[8] + m->m[9] * m->m[9] + m->m[10] * m->m[10]);
    float s = sx;
    if (sy > s)
        s = sy;
    if (sz > s)
        s = sz;
    if (s < 1e-6f)
        s = 1e-6f;
    return s;
}

static float R_mesh_local_radius(const mesh_t *mesh)
{
    if (!mesh || !(mesh->flags & MESH_FLAG_HAS_AABB))
        return 1.0f;

    float ex = 0.5f * (mesh->local_aabb.max.x - mesh->local_aabb.min.x);
    float ey = 0.5f * (mesh->local_aabb.max.y - mesh->local_aabb.min.y);
    float ez = 0.5f * (mesh->local_aabb.max.z - mesh->local_aabb.min.z);

    float r2 = ex * ex + ey * ey + ez * ez;
    float r = (r2 > 1e-12f) ? sqrtf(r2) : 1.0f;
    if (r < 1e-6f)
        r = 1.0f;
    return r;
}

static vec3 R_mesh_local_center(const mesh_t *mesh)
{
    if (!mesh || !(mesh->flags & MESH_FLAG_HAS_AABB))
        return (vec3){0.0f, 0.0f, 0.0f};
    vec3 c;
    c.x = 0.5f * (mesh->local_aabb.min.x + mesh->local_aabb.max.x);
    c.y = 0.5f * (mesh->local_aabb.min.y + mesh->local_aabb.max.y);
    c.z = 0.5f * (mesh->local_aabb.min.z + mesh->local_aabb.max.z);
    return c;
}

static vec3 R_transform_point(mat4 m, vec3 p)
{
    vec3 o;
    o.x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    o.y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    o.z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return o;
}

static void R_log_missing_forced_lod_once(ihandle_t model, uint32_t mesh_index, uint32_t lod_wanted, uint32_t lods)
{
    static uint32_t budget = 64u;
    if (!budget)
        return;
    budget--;

    LOG_WARN("Forced LOD %u requested but mesh has %u lods (model type=%u val=%u meta=%u mesh=%u). Using lod=%u",
             lod_wanted, lods,
             (unsigned)model.type, (unsigned)model.value, (unsigned)model.meta,
             (unsigned)mesh_index,
             (lods ? (lods - 1u) : 0u));
}

static uint32_t R_pick_lod_level_for_mesh(const renderer_t *r, const mesh_t *mesh, const mat4 *model_mtx, ihandle_t model_h, uint32_t mesh_index)
{
    if (!r || !mesh || !model_mtx)
        return 0;

    uint32_t lods = mesh->lods.size;
    if (lods <= 1)
    {
        int forced0 = R_force_lod_level();
        if (forced0 >= 0 && (uint32_t)forced0 > 0u)
            R_log_missing_forced_lod_once(model_h, mesh_index, (uint32_t)forced0, lods);
        return 0;
    }

    int forced = R_force_lod_level();
    if (forced >= 0)
    {
        uint32_t want = (uint32_t)forced;
        if (want >= lods)
        {
            R_log_missing_forced_lod_once(model_h, mesh_index, want, lods);
            return lods - 1u;
        }
        return want;
    }

    float dist = 0.0f;

    {
        vec3 lc = R_mesh_local_center(mesh);
        vec3 wc = R_transform_point(*model_mtx, lc);

        float dx = wc.x - r->camera.position.x;
        float dy = wc.y - r->camera.position.y;
        float dz = wc.z - r->camera.position.z;

        dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < 1e-3f)
            dist = 1e-3f;
    }

    float proj_fy = r->camera.proj.m[5];
    if (fabsf(proj_fy) < 1e-6f)
        proj_fy = 1.0f;

    float px_per_world = (0.5f * (float)r->fb_size.y) * proj_fy / dist;

    float radius_world = R_mesh_local_radius(mesh) * R_mat4_max_scale_xyz(model_mtx);
    float diameter_px = 2.0f * radius_world * px_per_world;

    uint32_t lod = 0;
    if (diameter_px < 240.0f)
        lod = 1;
    if (diameter_px < 110.0f)
        lod = 2;
    if (diameter_px < 55.0f)
        lod = 3;
    if (diameter_px < 26.0f)
        lod = 4;
    if (diameter_px < 13.0f)
        lod = 5;

    if (lod >= lods)
        lod = lods - 1;

    return lod;
}

static const mesh_lod_t *R_mesh_get_lod(const mesh_t *m, uint32_t lod)
{
    if (!m)
        return NULL;

    if (m->lods.size == 0)
        return NULL;

    if (lod >= m->lods.size)
        lod = m->lods.size - 1;

    return (const mesh_lod_t *)vector_at((vector_t *)&m->lods, lod);
}

static void R_instance_stream_init(renderer_t *r)
{
    r->inst_batches = create_vector(inst_batch_t);
    r->fwd_inst_batches = create_vector(inst_batch_t);
    r->inst_mats = create_vector(mat4);

    glGenBuffers(1, &r->instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(mat4) * 1024u), 0, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_instance_stream_shutdown(renderer_t *r)
{
    if (r->instance_vbo)
        glDeleteBuffers(1, &r->instance_vbo);
    r->instance_vbo = 0;

    vector_free(&r->inst_batches);
    vector_free(&r->fwd_inst_batches);
    vector_free(&r->inst_mats);
}

static void R_upload_instances(renderer_t *r, const mat4 *mats, uint32_t count)
{
    if (!count)
        return;
    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(mat4) * (size_t)count), mats, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_mesh_bind_instance_attribs(renderer_t *r, uint32_t vao)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);

    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
    glEnableVertexAttribArray(7);

    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(mat4), (void *)(sizeof(float) * 0));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(mat4), (void *)(sizeof(float) * 4));
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(mat4), (void *)(sizeof(float) * 8));
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(mat4), (void *)(sizeof(float) * 12));

    glVertexAttribDivisor(4, 1);
    glVertexAttribDivisor(5, 1);
    glVertexAttribDivisor(6, 1);
    glVertexAttribDivisor(7, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

static int R_inst_item_sort(const void *a, const void *b)
{
    const inst_item_t *ia = (const inst_item_t *)a;
    const inst_item_t *ib = (const inst_item_t *)b;

    if (ia->model.type < ib->model.type)
        return -1;
    if (ia->model.type > ib->model.type)
        return 1;

    if (ia->model.value < ib->model.value)
        return -1;
    if (ia->model.value > ib->model.value)
        return 1;

    if (ia->model.meta < ib->model.meta)
        return -1;
    if (ia->model.meta > ib->model.meta)
        return 1;

    if (ia->mesh_index < ib->mesh_index)
        return -1;
    if (ia->mesh_index > ib->mesh_index)
        return 1;

    if (ia->lod < ib->lod)
        return -1;
    if (ia->lod > ib->lod)
        return 1;

    return 0;
}

static void R_emit_batches_from_items(renderer_t *r, inst_item_t *items, uint32_t n, vector_t *batches)
{
    if (!n)
        return;

    qsort(items, (size_t)n, sizeof(inst_item_t), R_inst_item_sort);

    uint32_t cur_start = r->inst_mats.size;

    inst_batch_t cur;
    memset(&cur, 0, sizeof(cur));
    cur.model = items[0].model;
    cur.mesh_index = items[0].mesh_index;
    cur.lod = items[0].lod;
    cur.start = cur_start;

    for (uint32_t i = 0; i < n; ++i)
    {
        int same_model = ihandle_eq(cur.model, items[i].model);
        int same_mesh = (cur.mesh_index == items[i].mesh_index);
        int same_lod = (cur.lod == items[i].lod);

        if (!(same_model && same_mesh && same_lod))
        {
            cur.count = r->inst_mats.size - cur_start;
            if (cur.count)
                vector_push_back(batches, &cur);

            cur_start = r->inst_mats.size;
            cur.model = items[i].model;
            cur.mesh_index = items[i].mesh_index;
            cur.lod = items[i].lod;
            cur.start = cur_start;
            cur.count = 0;
        }

        vector_push_back(&r->inst_mats, &items[i].m);
    }

    cur.count = r->inst_mats.size - cur_start;
    if (cur.count)
        vector_push_back(batches, &cur);
}

static void R_build_instancing(renderer_t *r)
{
    vector_clear(&r->inst_batches);
    vector_clear(&r->fwd_inst_batches);
    vector_clear(&r->inst_mats);

    uint32_t max_items = 0;

    for (uint32_t i = 0; i < r->models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        max_items += mdl->meshes.size;
    }

    for (uint32_t i = 0; i < r->fwd_models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        max_items += mdl->meshes.size;
    }

    if (!max_items)
        return;

    inst_item_t *items = (inst_item_t *)malloc(sizeof(inst_item_t) * (size_t)max_items);
    if (!items)
        return;

    uint32_t n = 0;

    for (uint32_t i = 0; i < r->models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            uint32_t lod = R_pick_lod_level_for_mesh(r, mesh, &pm->model_matrix, pm->model, mi);

            items[n].model = pm->model;
            items[n].mesh_index = mi;
            items[n].lod = lod;
            items[n].m = pm->model_matrix;
            n++;
        }
    }

    for (uint32_t i = 0; i < r->fwd_models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            uint32_t lod = R_pick_lod_level_for_mesh(r, mesh, &pm->model_matrix, pm->model, mi);

            items[n].model = pm->model;
            items[n].mesh_index = mi;
            items[n].lod = lod;
            items[n].m = pm->model_matrix;
            n++;
        }
    }

    if (n)
        R_emit_batches_from_items(r, items, n, &r->inst_batches);

    free(items);
}

static char *R_read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }

    long n = ftell(f);
    if (n <= 0)
    {
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);

    if (rd != (size_t)n)
    {
        free(buf);
        return NULL;
    }

    buf[n] = 0;
    return buf;
}

static uint32_t R_build_compute_program_from_file(const char *path)
{
    char *src = R_read_text_file(path);
    if (!src)
    {
        LOG_ERROR("Compute shader read failed: %s", path);
        return 0;
    }

    uint32_t sh = glCreateShader(GL_COMPUTE_SHADER);
    const char *psrc = src;
    glShaderSource(sh, 1, &psrc, 0);
    glCompileShader(sh);
    free(src);

    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        int ln = 0;
        glGetShaderInfoLog(sh, (int)sizeof(log), &ln, log);
        LOG_ERROR("Compute compile failed (%s): %s", path, log);
        glDeleteShader(sh);
        return 0;
    }

    uint32_t prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    glDeleteShader(sh);

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        int ln = 0;
        glGetProgramInfoLog(prog, (int)sizeof(log), &ln, log);
        LOG_ERROR("Compute link failed (%s): %s", path, log);
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

static int R_gpu_light_type(int t)
{
    if (t == (int)LIGHT_DIRECTIONAL)
        return 1;
    return 0;
}

static uint32_t R_fp_upload_lights(renderer_t *r)
{
    uint32_t count = r ? r->lights.size : 0u;
    if (count < 1u)
        count = 1u;

    R_fp_ensure_lights_capacity(count);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_fp_lights_ssbo);

    gpu_light_t *dst = (gpu_light_t *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(sizeof(gpu_light_t) * (size_t)count),
                                                       GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
    if (!dst)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return (r ? r->lights.size : 0u);
    }

    uint32_t real = r ? r->lights.size : 0u;

    for (uint32_t i = 0; i < real; ++i)
    {
        light_t *l = (light_t *)vector_at(&r->lights, i);
        gpu_light_t *g = &dst[i];
        memset(g, 0, sizeof(*g));

        g->position[0] = l->position.x;
        g->position[1] = l->position.y;
        g->position[2] = l->position.z;
        g->position[3] = 1.0f;

        g->direction[0] = l->direction.x;
        g->direction[1] = l->direction.y;
        g->direction[2] = l->direction.z;
        g->direction[3] = 0.0f;

        g->color[0] = l->color.x;
        g->color[1] = l->color.y;
        g->color[2] = l->color.z;
        g->color[3] = 1.0f;

        g->params[0] = l->intensity;
        g->params[1] = l->radius;
        g->params[2] = l->range;
        g->params[3] = 0.0f;

        g->meta[0] = R_gpu_light_type((int)l->type);
        g->meta[1] = 0;
        g->meta[2] = 0;
        g->meta[3] = 0;
    }

    if (count > real)
        memset(&dst[real], 0, sizeof(gpu_light_t) * (size_t)(count - real));

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    return real;
}

static void R_fp_dispatch(renderer_t *r)
{
    if (!g_fp_prog_init || !g_fp_prog_cull || !g_fp_prog_finalize)
        return;

    uint32_t light_count = r ? r->lights.size : 0u;

    R_fp_resize_tile_buffers(r->fb_size, light_count);
    R_fp_ensure_lights_capacity(light_count ? light_count : 1u);

    uint32_t uploaded = R_fp_upload_lights(r);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_fp_lights_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_fp_tile_index_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_fp_tile_list_ssbo);

    glUseProgram(g_fp_prog_init);
    {
        int loc_tc = glGetUniformLocation(g_fp_prog_init, "u_TileCount");
        int loc_tm = glGetUniformLocation(g_fp_prog_init, "u_TileMax");
        glUniform1i(loc_tc, g_fp_tiles);
        glUniform1i(loc_tm, (int)g_fp_tile_max);

        uint32_t gx = (uint32_t)((g_fp_tiles + 255) / 256);
        if (gx < 1)
            gx = 1;
        glDispatchCompute(gx, 1, 1);
    }

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(g_fp_prog_cull);
    {
        int loc_view = glGetUniformLocation(g_fp_prog_cull, "u_View");
        int loc_proj = glGetUniformLocation(g_fp_prog_cull, "u_Proj");
        int loc_scr = glGetUniformLocation(g_fp_prog_cull, "u_ScreenSize");
        int loc_tc = glGetUniformLocation(g_fp_prog_cull, "u_TileCount");
        int loc_ts = glGetUniformLocation(g_fp_prog_cull, "u_TileSize");
        int loc_lc = glGetUniformLocation(g_fp_prog_cull, "u_LightCount");
        int loc_tm = glGetUniformLocation(g_fp_prog_cull, "u_TileMax");

        glUniformMatrix4fv(loc_view, 1, GL_FALSE, (const float *)r->camera.view.m);
        glUniformMatrix4fv(loc_proj, 1, GL_FALSE, (const float *)r->camera.proj.m);
        glUniform2i(loc_scr, r->fb_size.x, r->fb_size.y);
        glUniform2i(loc_tc, g_fp_tile_count_x, g_fp_tile_count_y);
        glUniform1i(loc_ts, FP_TILE_SIZE);
        glUniform1i(loc_lc, (int)uploaded);
        glUniform1i(loc_tm, (int)g_fp_tile_max);

        uint32_t gx = (uint32_t)((uploaded + 63u) / 64u);
        if (gx < 1)
            gx = 1;
        glDispatchCompute(gx, 1, 1);
    }

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glUseProgram(g_fp_prog_finalize);
    {
        int loc_tc = glGetUniformLocation(g_fp_prog_finalize, "u_TileCount");
        int loc_tm = glGetUniformLocation(g_fp_prog_finalize, "u_TileMax");
        glUniform1i(loc_tc, g_fp_tiles);
        glUniform1i(loc_tm, (int)g_fp_tile_max);

        uint32_t gx = (uint32_t)((g_fp_tiles + 255) / 256);
        if (gx < 1)
            gx = 1;
        glDispatchCompute(gx, 1, 1);
    }

    glUseProgram(0);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

static void R_sky_pass(renderer_t *r)
{
    shader_t *sky = (r->sky_shader_id != 0xFF) ? R_get_shader(r, r->sky_shader_id) : NULL;
    if (!sky)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_ALWAYS);

    shader_bind(sky);

    uint32_t env = ibl_get_env(r);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env ? env : g_black_cube);
    shader_set_int(sky, "u_Env", 0);
    shader_set_int(sky, "u_HasEnv", env ? 1 : 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    shader_set_int(sky, "u_Depth", 1);

    shader_set_mat4(sky, "u_InvProj", r->camera.inv_proj);
    shader_set_mat4(sky, "u_InvView", r->camera.inv_view);

    R_draw_fs_tri(r);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
}

static void R_forward_one_pass(renderer_t *r)
{
    shader_t *fwd = (r->default_shader_id != 0xFF) ? R_get_shader(r, r->default_shader_id) : NULL;
    if (!fwd)
        return;

    ibl_ensure(r);

    uint32_t irr = ibl_get_irradiance(r);
    uint32_t pre = ibl_get_prefilter(r);
    uint32_t brdf = ibl_get_brdf_lut(r);
    int has_ibl = (irr && pre && brdf) ? 1 : 0;

    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glFrontFace(GL_CCW);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g_fp_lights_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_fp_tile_index_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g_fp_tile_list_ssbo);

    shader_bind(fwd);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irr ? irr : g_black_cube);
    shader_set_int(fwd, "u_IrradianceMap", 8);

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_CUBE_MAP, pre ? pre : g_black_cube);
    shader_set_int(fwd, "u_PrefilterMap", 9);

    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, brdf ? brdf : g_black_tex);
    shader_set_int(fwd, "u_BRDFLUT", 10);

    shader_set_int(fwd, "u_HasIBL", has_ibl);
    shader_set_float(fwd, "u_IBLIntensity", r->cfg.ibl_intensity);

    shader_set_int(fwd, "u_TileSize", FP_TILE_SIZE);
    shader_set_int(fwd, "u_TileCountX", g_fp_tile_count_x);
    shader_set_int(fwd, "u_TileCountY", g_fp_tile_count_y);

    shader_set_int(fwd, "u_UseInstancing", 1);

    R_bind_common_uniforms(r, fwd);

    for (uint32_t bi = 0; bi < r->inst_batches.size; ++bi)
    {
        inst_batch_t *b = (inst_batch_t *)vector_at(&r->inst_batches, bi);
        if (!b || !ihandle_is_valid(b->model) || b->count == 0)
            continue;

        asset_model_t *mdl = R_resolve_model(r, b->model);
        if (!mdl)
            continue;

        if (b->mesh_index >= mdl->meshes.size)
            continue;

        mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, b->mesh_index);
        if (!mesh)
            continue;

        const mesh_lod_t *lod = R_mesh_get_lod(mesh, b->lod);
        if (!lod || !lod->vao || !lod->index_count)
            continue;

        asset_material_t *mat = R_resolve_material(r, mesh->material);

        int is_blended = (mat && mat->opacity < 0.999f) ? 1 : 0;
        int wants_two_sided = (r->cfg.alpha_test || is_blended) ? 1 : 0;

        if (is_blended)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        else
        {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }

        if (wants_two_sided)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }

        mat4 *mats = (mat4 *)vector_at(&r->inst_mats, b->start);
        if (!mats)
            continue;

        shader_set_int(fwd, "u_DebugLod", (r->cfg.debug_mode != 0) ? (int)(b->lod + 1u) : 0);

        R_upload_instances(r, mats, b->count);

        R_mesh_bind_instance_attribs(r, lod->vao);
        R_apply_material_or_default(r, fwd, mat);

        glBindVertexArray(lod->vao);
        R_stats_add_draw_instanced(r, lod->index_count, b->count);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
        glBindVertexArray(0);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

int R_init(renderer_t *r, asset_manager_t *assets)
{
    if (!r)
        return 1;

    memset(r, 0, sizeof(*r));
    r->assets = assets;

    r->gbuf_shader_id = 0xFF;
    r->light_shader_id = 0xFF;
    r->default_shader_id = 0xFF;
    r->sky_shader_id = 0xFF;
    r->present_shader_id = 0xFF;

    R_cfg_pull_from_cvars(r);

    r->clear_color = (vec4){0.02f, 0.02f, 0.02f, 1.0f};
    r->fb_size = (vec2i){1, 1};

    r->hdri_tex = ihandle_invalid();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);

    glGenVertexArrays(1, &r->fs_vao);

    R_make_black_tex();
    R_make_black_cube();

    g_fp_prog_init = R_build_compute_program_from_file("res/shaders/Forward/fp_init.comp");
    g_fp_prog_cull = R_build_compute_program_from_file("res/shaders/Forward/fp_cull.comp");
    g_fp_prog_finalize = R_build_compute_program_from_file("res/shaders/Forward/fp_finalize.comp");

    if (!g_fp_prog_init || !g_fp_prog_cull || !g_fp_prog_finalize)
        return 1;

    R_create_targets(r);

    r->lights = create_vector(light_t);
    r->models = create_vector(pushed_model_t);
    r->fwd_models = create_vector(pushed_model_t);
    r->shaders = create_vector(shader_t *);

    R_instance_stream_init(r);

    shader_t *forward_shader = R_new_shader_from_files_with_defines("res/shaders/Forward/Forward.vert", "res/shaders/Forward/Forward.frag");
    if (!forward_shader)
        return 1;
    r->default_shader_id = R_add_shader(r, forward_shader);

    shader_t *sky_shader = R_new_shader_from_files_with_defines("res/shaders/sky.vert", "res/shaders/sky.frag");
    if (!sky_shader)
        return 1;
    r->sky_shader_id = R_add_shader(r, sky_shader);

    shader_t *present_shader = R_new_shader_from_files_with_defines("res/shaders/fs_tri.vert", "res/shaders/present.frag");
    if (!present_shader)
        return 1;
    r->present_shader_id = R_add_shader(r, present_shader);

    if (!ibl_init(r))
        LOG_ERROR("IBL init failed");

    if (!ssr_init(r))
        LOG_ERROR("SSR init failed");

    if (!bloom_init(r))
        LOG_ERROR("Bloom init failed");

    bloom_set_params(r,
                     r->cfg.bloom_threshold,
                     r->cfg.bloom_knee,
                     r->cfg.bloom_intensity,
                     r->cfg.bloom_mips);

    cvar_set_callback_name("cl_bloom", R_on_bloom_change);
    cvar_set_callback_name("cl_render_debug", R_on_debug_mode_change);

    cvar_set_callback_name("cl_r_bloom_threshold", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_bloom_knee", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_bloom_intensity", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_bloom_mips", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_exposure", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_output_gamma", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_manual_srgb", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_alpha_test", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_alpha_cutoff", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_height_invert", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_ibl_intensity", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_ssr", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_ssr_intensity", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_ssr_steps", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_ssr_stride", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_ssr_thickness", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_ssr_max_dist", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_wireframe", R_on_wireframe_change);

    return 0;
}

uint8_t R_add_shader(renderer_t *r, shader_t *shader)
{
    if (!r || !shader)
        return 0xFF;
    vector_push_back(&r->shaders, &shader);
    return (uint8_t)(r->shaders.size - 1);
}

shader_t *R_get_shader(const renderer_t *r, uint8_t shader_id)
{
    if (!r)
        return NULL;
    if (shader_id >= r->shaders.size)
        return NULL;
    shader_t **ps = (shader_t **)vector_at((vector_t *)&r->shaders, shader_id);
    return ps ? *ps : NULL;
}

const shader_t *R_get_shader_const(const renderer_t *r, uint8_t shader_id)
{
    return (const shader_t *)R_get_shader(r, shader_id);
}

uint32_t R_get_final_fbo(const renderer_t *r)
{
    if (!r)
        return 0;
    return r->final_fbo;
}

void R_shutdown(renderer_t *r)
{
    if (!r)
        return;

    bloom_shutdown(r);
    ssr_shutdown(r);
    ibl_shutdown(r);

    R_instance_stream_shutdown(r);

    if (r->fs_vao)
        glDeleteVertexArrays(1, &r->fs_vao);
    r->fs_vao = 0;

    for (uint32_t i = 0; i < r->shaders.size; i++)
    {
        shader_t *s = *(shader_t **)vector_at(&r->shaders, i);
        if (s)
        {
            shader_destroy(s);
            free(s);
        }
    }

    vector_free(&r->shaders);
    vector_free(&r->lights);
    vector_free(&r->models);
    vector_free(&r->fwd_models);

    R_gl_delete_targets(r);

    r->assets = NULL;

    if (g_black_tex)
        glDeleteTextures(1, &g_black_tex);
    g_black_tex = 0;

    if (g_black_cube)
        glDeleteTextures(1, &g_black_cube);
    g_black_cube = 0;

    R_fp_delete_buffers();

    if (g_fp_prog_init)
        glDeleteProgram(g_fp_prog_init);
    if (g_fp_prog_cull)
        glDeleteProgram(g_fp_prog_cull);
    if (g_fp_prog_finalize)
        glDeleteProgram(g_fp_prog_finalize);

    g_fp_prog_init = 0;
    g_fp_prog_cull = 0;
    g_fp_prog_finalize = 0;
}

void R_resize(renderer_t *r, vec2i size)
{
    if (!r)
        return;

    if (size.x < 1)
        size.x = 1;
    if (size.y < 1)
        size.y = 1;
    if (r->fb_size.x == size.x && r->fb_size.y == size.y)
        return;

    r->fb_size = size;
    R_create_targets(r);
    bloom_ensure(r);
    ssr_ensure(r);
}

void R_set_clear_color(renderer_t *r, vec4 color)
{
    if (!r)
        return;
    r->clear_color = color;
}

void R_begin_frame(renderer_t *r)
{
    if (!r)
        return;

    vector_clear(&r->lights);
    vector_clear(&r->models);
    vector_clear(&r->fwd_models);

    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glClearColor(r->clear_color.x, r->clear_color.y, r->clear_color.z, r->clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    R_stats_begin_frame(r);
}

void R_end_frame(renderer_t *r)
{
    if (!r)
        return;

    ibl_ensure(r);

    R_build_instancing(r);

    R_fp_dispatch(r);

    R_sky_pass(r);

    R_forward_one_pass(r);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    bloom_run(r, r->light_color_tex, g_black_tex);

    {
        shader_t *present = (r->present_shader_id != 0xFF) ? R_get_shader(r, r->present_shader_id) : NULL;
        if (present)
        {
            shader_bind(present);
            shader_set_int(present, "u_TileSize", FP_TILE_SIZE);
            shader_set_int(present, "u_TileCountX", g_fp_tile_count_x);
            shader_set_int(present, "u_TileCountY", g_fp_tile_count_y);
            shader_set_int(present, "u_TileMax", (int)g_fp_tile_max);
        }

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, g_fp_tile_index_ssbo);

        uint32_t bloom_tex = (r->cfg.bloom && r->bloom.mips) ? r->bloom.tex_up[0] : 0;
        bloom_composite_to_final(r, r->light_color_tex, bloom_tex, r->gbuf_depth, g_black_tex);
    }
}

void R_push_camera(renderer_t *r, const camera_t *cam)
{
    if (!r || !cam)
        return;
    r->camera = *cam;
}

void R_push_light(renderer_t *r, light_t light)
{
    if (!r)
        return;
    vector_push_back(&r->lights, &light);
}

void R_push_model(renderer_t *r, const ihandle_t model, mat4 model_matrix)
{
    if (!r)
        return;
    if (!ihandle_is_valid(model))
        return;

    pushed_model_t pm;
    memset(&pm, 0, sizeof(pm));
    pm.model = model;
    pm.model_matrix = model_matrix;

    vector_push_back(&r->models, &pm);
}

void R_push_model_forward(renderer_t *r, const ihandle_t model, mat4 model_matrix)
{
    if (!r)
        return;
    if (!ihandle_is_valid(model))
        return;

    pushed_model_t pm;
    memset(&pm, 0, sizeof(pm));
    pm.model = model;
    pm.model_matrix = model_matrix;

    vector_push_back(&r->fwd_models, &pm);
}

void R_push_hdri(renderer_t *r, ihandle_t tex)
{
    if (!r)
        return;
    r->hdri_tex = tex;
}

const render_stats_t *R_get_stats(const renderer_t *r)
{
    return r ? &r->stats : NULL;
}
