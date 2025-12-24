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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#ifndef MAX_LIGHTS
#define MAX_LIGHTS 16
#endif

typedef struct gpu_light_t
{
    float position[4];
    float direction[4];
    float color[4];
    float params[4];
    int meta[4];
} gpu_light_t;

typedef struct gpu_lights_block_t
{
    int header[4];
    gpu_light_t lights[MAX_LIGHTS];
} gpu_lights_block_t;

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
    uint32_t lod;
    uint32_t start;
    uint32_t count;
} inst_batch_t;

typedef struct inst_item_t
{
    ihandle_t model;
    uint32_t lod;
    mat4 m;
} inst_item_t;

static uint32_t g_black_tex = 0;
static uint32_t g_black_cube = 0;

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
    shader_define(&tmp, "MAX_LIGHTS", STR(MAX_LIGHTS));

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

static void R_create_targets(renderer_t *r)
{
    R_gl_delete_targets(r);

    if (r->fb_size.x < 1)
        r->fb_size.x = 1;
    if (r->fb_size.y < 1)
        r->fb_size.y = 1;

    glGenFramebuffers(1, &r->gbuf_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);

    R_alloc_tex2d(&r->gbuf_albedo, GL_RGBA8, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->gbuf_albedo, 0);

    R_alloc_tex2d(&r->gbuf_normal, GL_RG16_SNORM, r->fb_size.x, r->fb_size.y, GL_RG, GL_SHORT, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, r->gbuf_normal, 0);

    R_alloc_tex2d(&r->gbuf_material, GL_RGBA8, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, r->gbuf_material, 0);

    R_alloc_tex2d(&r->gbuf_emissive, GL_RGB10_A2, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, r->gbuf_emissive, 0);

    R_alloc_tex2d(&r->gbuf_depth, GL_DEPTH_COMPONENT32F, r->fb_size.x, r->fb_size.y, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->gbuf_depth, 0);

    {
        uint32_t bufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, (const GLenum *)bufs);
    }

    {
        uint32_t status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_ERROR("GBuffer FBO incomplete: 0x%x", (unsigned)status);
    }

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
            LOG_ERROR("Light FBO incomplete: 0x%x", (unsigned)status);
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

static void R_lights_ubo_init(renderer_t *r)
{
    glGenBuffers(1, &r->lights_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->lights_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(gpu_lights_block_t), 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static void R_lights_ubo_shutdown(renderer_t *r)
{
    if (r->lights_ubo)
        glDeleteBuffers(1, &r->lights_ubo);
    r->lights_ubo = 0;
}

static void R_lights_ubo_upload(renderer_t *r)
{
    gpu_lights_block_t blk;
    memset(&blk, 0, sizeof(blk));

    uint32_t light_count = r->lights.size;
    if (light_count > MAX_LIGHTS)
        light_count = MAX_LIGHTS;

    blk.header[0] = (int)light_count;

    for (uint32_t i = 0; i < light_count; i++)
    {
        light_t *l = (light_t *)vector_at(&r->lights, i);
        gpu_light_t *g = &blk.lights[i];

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

        g->meta[0] = (int)l->type;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, r->lights_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(blk), &blk);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static void R_draw_fs_tri(renderer_t *r)
{
    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static int R_mat_is_transparent(const asset_material_t *m)
{
    if (!m)
        return 0;
    return (m->opacity < 0.999f) ? 1 : 0;
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
    return cvar_get_int_name("cl_r_force_lod_level");
}

static uint32_t R_pick_lod_level_for_model(const renderer_t *r, const asset_model_t *m, const mat4 *model_mtx)
{
    if (!m || !model_mtx)
        return 0;

    int forced = R_force_lod_level();
    if (forced >= 0)
        return (uint32_t)forced;

    if (m->meshes.size == 0)
        return 0;

    mesh_t *mesh0 = (mesh_t *)vector_at((vector_t *)&m->meshes, 0);
    if (!mesh0)
        return 0;

    uint32_t lods = mesh0->lods.size;
    if (lods <= 1)
        return 0;

    float dx = model_mtx->m[12] - r->camera.position.x;
    float dy = model_mtx->m[13] - r->camera.position.y;
    float dz = model_mtx->m[14] - r->camera.position.z;
    float d2 = dx * dx + dy * dy + dz * dz;

    uint32_t lod = 0;
    if (d2 > 25.0f * 25.0f)
        lod = 1;
    if (d2 > 60.0f * 60.0f)
        lod = 2;
    if (d2 > 120.0f * 120.0f)
        lod = 3;

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
    cur.lod = items[0].lod;
    cur.start = cur_start;

    for (uint32_t i = 0; i < n; ++i)
    {
        int same_model = ihandle_eq(cur.model, items[i].model);
        int same_lod = (cur.lod == items[i].lod);

        if (!(same_model && same_lod))
        {
            cur.count = r->inst_mats.size - cur_start;
            if (cur.count)
                vector_push_back(batches, &cur);

            cur_start = r->inst_mats.size;
            cur.model = items[i].model;
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

    if (r->models.size)
    {
        inst_item_t *items = (inst_item_t *)malloc(sizeof(inst_item_t) * (size_t)r->models.size);
        if (items)
        {
            uint32_t n = 0;
            for (uint32_t i = 0; i < r->models.size; ++i)
            {
                pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
                if (!pm)
                    continue;
                if (!ihandle_is_valid(pm->model))
                    continue;

                asset_model_t *mdl = R_resolve_model(r, pm->model);
                uint32_t lod = 0;
                if (mdl)
                    lod = R_pick_lod_level_for_model(r, mdl, &pm->model_matrix);

                items[n].model = pm->model;
                items[n].lod = lod;
                items[n].m = pm->model_matrix;
                n++;
            }

            if (n)
                R_emit_batches_from_items(r, items, n, &r->inst_batches);

            free(items);
        }
    }

    if (r->fwd_models.size)
    {
        inst_item_t *items = (inst_item_t *)malloc(sizeof(inst_item_t) * (size_t)r->fwd_models.size);
        if (items)
        {
            uint32_t n = 0;
            for (uint32_t i = 0; i < r->fwd_models.size; ++i)
            {
                pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
                if (!pm)
                    continue;
                if (!ihandle_is_valid(pm->model))
                    continue;

                asset_model_t *mdl = R_resolve_model(r, pm->model);
                uint32_t lod = 0;
                if (mdl)
                    lod = R_pick_lod_level_for_model(r, mdl, &pm->model_matrix);

                items[n].model = pm->model;
                items[n].lod = lod;
                items[n].m = pm->model_matrix;
                n++;
            }

            if (n)
                R_emit_batches_from_items(r, items, n, &r->fwd_inst_batches);

            free(items);
        }
    }
}

static void R_deferred_geom_pass(renderer_t *r)
{
    shader_t *gbuf = (r->gbuf_shader_id != 0xFF) ? R_get_shader(r, r->gbuf_shader_id) : NULL;
    if (!gbuf)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    shader_bind(gbuf);

    shader_set_int(gbuf, "u_UseInstancing", 1);

    R_bind_common_uniforms(r, gbuf);

    for (uint32_t bi = 0; bi < r->inst_batches.size; ++bi)
    {
        inst_batch_t *b = (inst_batch_t *)vector_at(&r->inst_batches, bi);
        if (!b || !ihandle_is_valid(b->model) || b->count == 0)
            continue;

        asset_model_t *mdl = R_resolve_model(r, b->model);
        if (!mdl)
            continue;

        mat4 *mats = (mat4 *)vector_at(&r->inst_mats, b->start);
        if (!mats)
            continue;

        uint32_t lod_level = b->lod;

        shader_set_int(gbuf, "u_DebugLod", (r->cfg.debug_mode != 0) ? (int)(lod_level + 1u) : 0);

        R_upload_instances(r, mats, b->count);

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            const mesh_lod_t *lod = R_mesh_get_lod(mesh, lod_level);
            if (!lod || !lod->vao || !lod->index_count)
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            if (R_mat_is_transparent(mat))
                continue;

            R_mesh_bind_instance_attribs(r, lod->vao);

            R_apply_material_or_default(r, gbuf, mat);

            glBindVertexArray(lod->vao);
            R_stats_add_draw_instanced(r, lod->index_count, b->count);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);

            glBindVertexArray(0);
        }
    }

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void R_deferred_light_pass(renderer_t *r)
{
    shader_t *ls = (r->light_shader_id != 0xFF) ? R_get_shader(r, r->light_shader_id) : NULL;
    if (!ls)
        return;

    ibl_ensure(r);
    ssr_ensure(r);

    uint32_t irr = ibl_get_irradiance(r);
    uint32_t pre = ibl_get_prefilter(r);
    uint32_t brdf = ibl_get_brdf_lut(r);

    int has_ibl = (irr && pre && brdf) ? 1 : 0;

    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    shader_bind(ls);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_albedo);
    shader_set_int(ls, "u_GAlbedo", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_normal);
    shader_set_int(ls, "u_GNormal", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_material);
    shader_set_int(ls, "u_GMaterial", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    shader_set_int(ls, "u_GDepth", 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irr ? irr : g_black_cube);
    shader_set_int(ls, "u_IrradianceMap", 4);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, pre ? pre : g_black_cube);
    shader_set_int(ls, "u_PrefilterMap", 5);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, brdf ? brdf : g_black_tex);
    shader_set_int(ls, "u_BRDFLUT", 6);

    shader_set_int(ls, "u_HasIBL", has_ibl);
    shader_set_float(ls, "u_IBLIntensity", r->cfg.ibl_intensity);

    {
        uint32_t ssr_tex = (r->cfg.ssr && r->ssr.color_tex) ? r->ssr.color_tex : 0;
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, ssr_tex ? ssr_tex : g_black_tex);
        shader_set_int(ls, "u_SSR", 7);

        shader_set_int(ls, "u_HasSSR", ssr_tex ? 1 : 0);
        shader_set_float(ls, "u_SSRIntensity", r->cfg.ssr_intensity);
    }

    shader_set_mat4(ls, "u_InvView", r->camera.inv_view);
    shader_set_mat4(ls, "u_InvProj", r->camera.inv_proj);
    shader_set_vec3(ls, "u_CameraPos", r->camera.position);

    R_draw_fs_tri(r);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
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

static float R_dist2_to_cam_from_model_mtx(mat4 m, vec3 cam)
{
    float x = m.m[12] - cam.x;
    float y = m.m[13] - cam.y;
    float z = m.m[14] - cam.z;
    return x * x + y * y + z * z;
}

static int R_fwd_sort_desc(const void *a, const void *b)
{
    const float da = ((const float *)a)[0];
    const float db = ((const float *)b)[0];
    if (da < db)
        return 1;
    if (da > db)
        return -1;
    return 0;
}

static void R_forward_transparent_pass(renderer_t *r)
{
    if (!r)
        return;
    if (r->fwd_models.size == 0)
        return;

    typedef struct fwd_mesh_item_t
    {
        float dist2;
        pushed_model_t pm;
        uint32_t mesh_index;
    } fwd_mesh_item_t;

    uint32_t total = 0;
    for (uint32_t i = 0; i < r->fwd_models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
        if (!pm)
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        total += mdl->meshes.size;
    }

    if (total == 0)
        return;

    fwd_mesh_item_t *items = (fwd_mesh_item_t *)malloc(sizeof(fwd_mesh_item_t) * (size_t)total);
    if (!items)
        return;

    uint32_t count = 0;
    for (uint32_t i = 0; i < r->fwd_models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
        if (!pm)
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        float d2 = R_dist2_to_cam_from_model_mtx(pm->model_matrix, r->camera.position);

        uint32_t lod_level = R_pick_lod_level_for_model(r, mdl, &pm->model_matrix);

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            const mesh_lod_t *lod = R_mesh_get_lod(mesh, lod_level);
            if (!lod || !lod->vao || !lod->index_count)
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            if (!R_mat_is_transparent(mat))
                continue;

            items[count].dist2 = d2;
            items[count].pm = *pm;
            items[count].mesh_index = mi;
            count++;
        }
    }

    if (count == 0)
    {
        free(items);
        return;
    }

    qsort(items, (size_t)count, sizeof(fwd_mesh_item_t), R_fwd_sort_desc);

    ibl_ensure(r);

    uint32_t irr = ibl_get_irradiance(r);
    uint32_t pre = ibl_get_prefilter(r);
    uint32_t brdf = ibl_get_brdf_lut(r);
    int has_ibl = (irr && pre && brdf) ? 1 : 0;

    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    shader_t *bound = NULL;

    vec2 invSize = (vec2){1.0f / (float)r->fb_size.x, 1.0f / (float)r->fb_size.y};

    for (uint32_t i = 0; i < count; ++i)
    {
        pushed_model_t *pm = &items[i].pm;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, items[i].mesh_index);
        if (!mesh)
            continue;

        uint32_t lod_level = R_pick_lod_level_for_model(r, mdl, &pm->model_matrix);
        const mesh_lod_t *lod = R_mesh_get_lod(mesh, lod_level);
        if (!lod || !lod->vao || !lod->index_count)
            continue;

        asset_material_t *mat = R_resolve_material(r, mesh->material);
        if (!R_mat_is_transparent(mat))
            continue;

        uint8_t shader_id = mat ? mat->shader_id : r->default_shader_id;
        shader_t *s = R_get_shader(r, shader_id);
        if (!s)
            continue;

        if (s != bound)
        {
            shader_bind(s);
            bound = s;

            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_CUBE_MAP, irr ? irr : g_black_cube);
            shader_set_int(s, "u_IrradianceMap", 8);

            glActiveTexture(GL_TEXTURE9);
            glBindTexture(GL_TEXTURE_CUBE_MAP, pre ? pre : g_black_cube);
            shader_set_int(s, "u_PrefilterMap", 9);

            glActiveTexture(GL_TEXTURE10);
            glBindTexture(GL_TEXTURE_2D, brdf ? brdf : g_black_tex);
            shader_set_int(s, "u_BRDFLUT", 10);

            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_2D, r->light_color_tex ? r->light_color_tex : g_black_tex);
            shader_set_int(s, "u_SceneColor", 11);

            shader_set_int(s, "u_HasIBL", has_ibl);
            shader_set_float(s, "u_IBLIntensity", r->cfg.ibl_intensity);

            shader_set_vec2(s, "u_SceneInvSize", invSize);
            shader_set_int(s, "u_IsTransparent", 1);
            shader_set_float(s, "u_Transmission", 0.0f);
        }

        shader_set_int(s, "u_DebugLod", (r->cfg.debug_mode != 0) ? (int)(lod_level + 1u) : 0);

        shader_set_mat4(s, "u_Model", pm->model_matrix);
        R_bind_common_uniforms(r, s);
        R_apply_material_or_default(r, s, mat);

        R_stats_add_draw(r, lod->index_count);
        glCullFace(GL_FRONT);
        glDrawElements(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0);

        R_stats_add_draw(r, lod->index_count);
        glCullFace(GL_BACK);
        glDrawElements(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);
    }

    free(items);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
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
    R_create_targets(r);

    r->lights = create_vector(light_t);
    r->models = create_vector(pushed_model_t);
    r->fwd_models = create_vector(pushed_model_t);
    r->shaders = create_vector(shader_t *);

    R_instance_stream_init(r);

    shader_t *gbuf_shader = R_new_shader_from_files_with_defines("res/shaders/gbuffer.vert", "res/shaders/gbuffer.frag");
    if (!gbuf_shader)
        return 1;
    r->gbuf_shader_id = R_add_shader(r, gbuf_shader);

    shader_t *light_shader = R_new_shader_from_files_with_defines("res/shaders/deferred_light.vert", "res/shaders/deferred_light.frag");
    if (!light_shader)
        return 1;
    r->light_shader_id = R_add_shader(r, light_shader);

    shader_t *default_shader = R_new_shader_from_files_with_defines("res/shaders/shader.vert", "res/shaders/shader.frag");
    if (!default_shader)
        return 1;
    r->default_shader_id = R_add_shader(r, default_shader);

    shader_t *sky_shader = R_new_shader_from_files_with_defines("res/shaders/sky.vert", "res/shaders/sky.frag");
    if (!sky_shader)
        return 1;
    r->sky_shader_id = R_add_shader(r, sky_shader);

    shader_t *present_shader = R_new_shader_from_files_with_defines("res/shaders/fs_tri.vert", "res/shaders/present.frag");
    if (!present_shader)
        return 1;
    r->present_shader_id = R_add_shader(r, present_shader);

    R_lights_ubo_init(r);

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

    R_lights_ubo_shutdown(r);

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

    glBindFramebuffer(GL_FRAMEBUFFER, r->gbuf_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    R_stats_begin_frame(r);
}

void R_end_frame(renderer_t *r)
{
    if (!r)
        return;

    ibl_ensure(r);

    R_build_instancing(r);

    R_lights_ubo_upload(r);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->lights_ubo);

    R_deferred_geom_pass(r);

    if (r->cfg.ssr)
        ssr_run(r, r->light_color_tex);
    glDisable(GL_CULL_FACE);
    R_deferred_light_pass(r);
    R_sky_pass(r);
    R_forward_transparent_pass(r);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    bloom_run(r, r->light_color_tex, g_black_tex);

    {
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
