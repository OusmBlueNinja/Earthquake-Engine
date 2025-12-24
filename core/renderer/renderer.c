#include "renderer.h"
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

static GLuint g_pp_fbo = 0;
static GLuint g_black_tex = 0;

static void R_make_black_tex(void)
{
    if (g_black_tex)
        return;

    glGenTextures(1, &g_black_tex);
    glBindTexture(GL_TEXTURE_2D, g_black_tex);

    const float px[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, px);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
}

static void R_clear_rgba16f_tex(GLuint tex)
{
    if (!tex)
        return;
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearTexImage(tex, 0, GL_RGBA, GL_FLOAT, zero);
}

static void R_on_bloom_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    bool newb = *(const bool *)state;
    get_application()->renderer.cfg.bloom = newb;
}

static void R_on_debug_mode_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    int newv = *(const int *)state;
    if (newv < 0)
        newv = 0;
    get_application()->renderer.cfg.debug_mode = newv;
}

static shader_t *R_new_shader_from_files_with_defines(const char *vp, const char *fp)
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

static void R_alloc_tex2d(GLuint *tex, GLenum internal, int w, int h, GLenum format, GLenum type, GLenum minf, GLenum magf)
{
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magf);
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

    R_alloc_tex2d(&r->gbuf_normal, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, r->gbuf_normal, 0);

    R_alloc_tex2d(&r->gbuf_material, GL_RGBA8, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, r->gbuf_material, 0);

    R_alloc_tex2d(&r->gbuf_depth, GL_DEPTH_COMPONENT32F, r->fb_size.x, r->fb_size.y, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->gbuf_depth, 0);

    {
        GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, bufs);
    }

    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_ERROR("GBuffer FBO incomplete: 0x%x", (unsigned)status);
    }

    glGenFramebuffers(1, &r->light_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);

    R_alloc_tex2d(&r->light_color_tex, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->light_color_tex, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->gbuf_depth, 0);

    {
        GLenum bufs[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);
    }

    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_ERROR("Light FBO incomplete: 0x%x", (unsigned)status);
    }

    glGenFramebuffers(1, &r->final_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->final_fbo);

    R_alloc_tex2d(&r->final_color_tex, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->final_color_tex, 0);

    {
        GLenum bufs[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);
    }

    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
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
    blk.header[1] = 0;
    blk.header[2] = 0;
    blk.header[3] = 0;

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
        g->params[3] = 0.0f;

        g->meta[0] = (int)l->type;
        g->meta[1] = 0;
        g->meta[2] = 0;
        g->meta[3] = 0;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, r->lights_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(blk), &blk);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static uint32_t R_bloom_calc_mips(vec2i size, uint32_t req)
{
    uint32_t m = 0;
    int w = size.x;
    int h = size.y;
    while (m < req && w > 1 && h > 1 && m < 16)
    {
        w >>= 1;
        h >>= 1;
        if (w < 2 || h < 2)
            break;
        m++;
    }
    if (m < 1)
        m = 1;
    return m;
}

static void R_bloom_alloc_tex(uint32_t *tex, int w, int h)
{
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void R_bloom_free_textures(renderer_t *r)
{
    for (uint32_t i = 0; i < 16; i++)
    {
        if (r->bloom.tex_down[i])
            glDeleteTextures(1, &r->bloom.tex_down[i]);
        if (r->bloom.tex_up[i])
            glDeleteTextures(1, &r->bloom.tex_up[i]);
        r->bloom.tex_down[i] = 0;
        r->bloom.tex_up[i] = 0;
    }
    r->bloom.mips = 0;
    r->bloom.base_size = (vec2i){0, 0};
}

static void R_bloom_free_shaders(renderer_t *r)
{
    if (r->bloom.cs_extract)
    {
        shader_destroy(r->bloom.cs_extract);
        free(r->bloom.cs_extract);
    }
    if (r->bloom.cs_down)
    {
        shader_destroy(r->bloom.cs_down);
        free(r->bloom.cs_down);
    }
    if (r->bloom.cs_up)
    {
        shader_destroy(r->bloom.cs_up);
        free(r->bloom.cs_up);
    }
    if (r->bloom.post_present)
    {
        shader_destroy(r->bloom.post_present);
        free(r->bloom.post_present);
    }

    r->bloom.cs_extract = 0;
    r->bloom.cs_down = 0;
    r->bloom.cs_up = 0;
    r->bloom.post_present = 0;
}

static int R_bloom_init(renderer_t *r)
{
    if (!g_pp_fbo)
        glGenFramebuffers(1, &g_pp_fbo);

    if (!r->bloom.post_present)
        r->bloom.post_present = R_new_shader_from_files_with_defines("res/shaders/post_present.vert", "res/shaders/post_present.frag");

    if (!r->bloom.cs_extract)
        r->bloom.cs_extract = R_new_shader_from_files_with_defines("res/shaders/post_present.vert", "res/shaders/bloom_extract.frag");
    if (!r->bloom.cs_down)
        r->bloom.cs_down = R_new_shader_from_files_with_defines("res/shaders/post_present.vert", "res/shaders/bloom_down.frag");
    if (!r->bloom.cs_up)
        r->bloom.cs_up = R_new_shader_from_files_with_defines("res/shaders/post_present.vert", "res/shaders/bloom_up.frag");

    if (!r->bloom.post_present)
        return 0;
    if (!r->bloom.cs_extract || !r->bloom.cs_down || !r->bloom.cs_up)
        return 0;

    r->bloom.base_size = r->fb_size;
    r->bloom.mips = R_bloom_calc_mips(r->fb_size, r->cfg.bloom_mips);

    int w = r->fb_size.x;
    int h = r->fb_size.y;

    for (uint32_t i = 0; i < r->bloom.mips; i++)
    {
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        if (w < 1)
            w = 1;
        if (h < 1)
            h = 1;

        R_bloom_alloc_tex(&r->bloom.tex_down[i], w, h);
        R_bloom_alloc_tex(&r->bloom.tex_up[i], w, h);
        R_clear_rgba16f_tex(r->bloom.tex_down[i]);
        R_clear_rgba16f_tex(r->bloom.tex_up[i]);
    }

    r->cfg.bloom = cvar_get_bool_name("cl_bloom");
    cvar_set_callback_name("cl_bloom", R_on_bloom_change);

    r->cfg.debug_mode = cvar_get_int_name("cl_render_debug");
    cvar_set_callback_name("cl_render_debug", R_on_debug_mode_change);

    return 1;
}

static void R_bloom_ensure(renderer_t *r)
{
    uint32_t want_mips = R_bloom_calc_mips(r->fb_size, r->cfg.bloom_mips);
    if (r->bloom.mips != 0 &&
        r->bloom.base_size.x == r->fb_size.x &&
        r->bloom.base_size.y == r->fb_size.y &&
        r->bloom.mips == want_mips)
        return;

    R_bloom_free_textures(r);
    r->bloom.base_size = r->fb_size;
    r->bloom.mips = 0;

    if (!R_bloom_init(r))
        LOG_ERROR("Bloom init failed");
}

static void R_draw_fs_tri(renderer_t *r)
{
    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void R_pp_attach(GLuint tex)
{
    glBindFramebuffer(GL_FRAMEBUFFER, g_pp_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
}

static void R_pp_draw(renderer_t *r, int w, int h)
{
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    R_draw_fs_tri(r);
}

static void R_bloom_run(renderer_t *r)
{
    if (!r->cfg.bloom)
        return;

    R_bloom_ensure(r);
    if (!r->bloom.mips)
        return;

    int base_w = r->fb_size.x;
    int base_h = r->fb_size.y;
    if (base_w < 2 || base_h < 2)
        return;

    int w0 = base_w >> 1;
    int h0 = base_h >> 1;
    if (w0 < 1)
        w0 = 1;
    if (h0 < 1)
        h0 = 1;

    R_clear_rgba16f_tex(r->bloom.tex_down[0]);

    shader_bind(r->bloom.cs_extract);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->light_color_tex);
    shader_set_int(r->bloom.cs_extract, "u_Src", 0);
    shader_set_float(r->bloom.cs_extract, "u_Threshold", r->cfg.bloom_threshold);
    shader_set_float(r->bloom.cs_extract, "u_Knee", r->cfg.bloom_knee);

    R_pp_attach(r->bloom.tex_down[0]);
    R_pp_draw(r, w0, h0);

    int w = w0;
    int h = h0;

    for (uint32_t i = 1; i < r->bloom.mips; i++)
    {
        int nw = w >> 1;
        int nh = h >> 1;
        if (nw < 1)
            nw = 1;
        if (nh < 1)
            nh = 1;

        R_clear_rgba16f_tex(r->bloom.tex_down[i]);

        shader_bind(r->bloom.cs_down);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->bloom.tex_down[i - 1]);
        shader_set_int(r->bloom.cs_down, "u_Src", 0);
        shader_set_float(r->bloom.cs_down, "u_TexelX", 1.0f / (float)w);
        shader_set_float(r->bloom.cs_down, "u_TexelY", 1.0f / (float)h);

        R_pp_attach(r->bloom.tex_down[i]);
        R_pp_draw(r, nw, nh);

        w = nw;
        h = nh;
    }

    for (uint32_t i = 0; i < r->bloom.mips; i++)
        R_clear_rgba16f_tex(r->bloom.tex_up[i]);

    uint32_t last = r->bloom.mips - 1;

    {
        int lw = base_w >> (int)(last + 1);
        int lh = base_h >> (int)(last + 1);
        if (lw < 1)
            lw = 1;
        if (lh < 1)
            lh = 1;

        glCopyImageSubData(
            r->bloom.tex_down[last], GL_TEXTURE_2D, 0, 0, 0, 0,
            r->bloom.tex_up[last], GL_TEXTURE_2D, 0, 0, 0, 0,
            lw, lh, 1);
    }

    for (int i = (int)r->bloom.mips - 2; i >= 0; i--)
    {
        int dst_w = base_w >> (i + 1);
        int dst_h = base_h >> (i + 1);

        int low_w = base_w >> (i + 2);
        int low_h = base_h >> (i + 2);

        if (dst_w < 1)
            dst_w = 1;
        if (dst_h < 1)
            dst_h = 1;
        if (low_w < 1)
            low_w = 1;
        if (low_h < 1)
            low_h = 1;

        shader_bind(r->bloom.cs_up);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->bloom.tex_up[i + 1]);
        shader_set_int(r->bloom.cs_up, "u_Low", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->bloom.tex_down[i]);
        shader_set_int(r->bloom.cs_up, "u_High", 1);

        shader_set_float(r->bloom.cs_up, "u_TexelX", 1.0f / (float)low_w);
        shader_set_float(r->bloom.cs_up, "u_TexelY", 1.0f / (float)low_h);
        shader_set_float(r->bloom.cs_up, "u_Intensity", r->cfg.bloom_intensity);

        R_pp_attach(r->bloom.tex_up[i]);
        R_pp_draw(r, dst_w, dst_h);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

static void R_composite_to_final(renderer_t *r)
{
    if (!r->bloom.post_present)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->final_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    shader_bind(r->bloom.post_present);

    shader_set_int(r->bloom.post_present, "u_DebugMode", r->cfg.debug_mode);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->light_color_tex);
    shader_set_int(r->bloom.post_present, "u_Scene", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, (r->cfg.bloom && r->bloom.mips) ? r->bloom.tex_up[0] : g_black_tex);
    shader_set_int(r->bloom.post_present, "u_Bloom", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    shader_set_int(r->bloom.post_present, "u_Depth", 2);

    shader_set_int(r->bloom.post_present, "u_EnableBloom", r->cfg.bloom ? 1 : 0);
    shader_set_float(r->bloom.post_present, "u_BloomIntensity", r->cfg.bloom_intensity);

    shader_set_float(r->bloom.post_present, "u_Exposure", r->cfg.exposure);
    shader_set_float(r->bloom.post_present, "u_OutputGamma", r->cfg.output_gamma);

    R_draw_fs_tri(r);

    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int R_mat_is_transparent(const asset_material_t *m)
{
    if (!m)
        return 0;
    if (m->opacity < 0.999f)
        return 1;
    return 0;
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

static void R_bind_common_uniforms(renderer_t *r, shader_t *s, const mat4 model_mtx)
{
    shader_set_mat4(s, "u_Model", model_mtx);
    shader_set_mat4(s, "u_View", r->camera.view);
    shader_set_mat4(s, "u_Proj", r->camera.proj);
    shader_set_vec3(s, "u_CameraPos", r->camera.position);

    shader_set_int(s, "u_HeightInvert", r->cfg.height_invert ? 1 : 0);
    shader_set_int(s, "u_AlphaTest", r->cfg.alpha_test ? 1 : 0);
    shader_set_float(s, "u_AlphaCutoff", r->cfg.alpha_cutoff);
    shader_set_int(s, "u_ManualSRGB", r->cfg.manual_srgb ? 1 : 0);
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

    shader_bind(gbuf);

    for (uint32_t i = 0; i < r->models.size; i++)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm)
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh || !mesh->vao || !mesh->index_count)
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            if (R_mat_is_transparent(mat))
                continue;

            R_bind_common_uniforms(r, gbuf, pm->model_matrix);
            R_apply_material_or_default(r, gbuf, mat);

            glBindVertexArray(mesh->vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->index_count, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
}

static void R_deferred_light_pass(renderer_t *r)
{
    shader_t *ls = (r->light_shader_id != 0xFF) ? R_get_shader(r, r->light_shader_id) : NULL;
    if (!ls)
        return;

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

    shader_set_vec2(ls, "u_InvResolution", (vec2){1.0f / (float)r->fb_size.x, 1.0f / (float)r->fb_size.y});
    shader_set_mat4(ls, "u_InvView", r->camera.inv_view);
    shader_set_mat4(ls, "u_InvProj", r->camera.inv_proj);
    shader_set_vec3(ls, "u_CameraPos", r->camera.position);

    R_draw_fs_tri(r);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
}
static void R_forward_transparent_pass(renderer_t *r)
{
    glBindFramebuffer(GL_FRAMEBUFFER, r->light_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_CULL_FACE);

    for (uint32_t i = 0; i < r->models.size; i++)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm)
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh || !mesh->vao || !mesh->index_count)
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            if (!R_mat_is_transparent(mat))
                continue;

            uint8_t shader_id = mat ? mat->shader_id : r->default_shader_id;
            shader_t *s = R_get_shader(r, shader_id);
            if (!s)
                continue;

            shader_bind(s);

            R_bind_common_uniforms(r, s, pm->model_matrix);
            R_apply_material_or_default(r, s, mat);

            glBindVertexArray(mesh->vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->index_count, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

int R_init(renderer_t *r, asset_manager_t *assets)
{
    if (!r)
        return 1;

    memset(r, 0, sizeof(*r));
    r->assets = assets;

    r->cfg.bloom = cvar_get_bool_name("cl_bloom");
    r->cfg.debug_mode = cvar_get_int_name("cl_render_debug");

    r->cfg.bloom_threshold = 1.0f;
    r->cfg.bloom_knee = 0.5f;
    r->cfg.bloom_intensity = 0.10f;
    r->cfg.bloom_mips = 6;

    r->cfg.exposure = 1.0f;
    r->cfg.output_gamma = 2.2f;
    r->cfg.manual_srgb = 0;

    r->cfg.alpha_test = 0;
    r->cfg.alpha_cutoff = 0.5f;

    r->cfg.height_invert = 0;

    r->clear_color = (vec4){0.02f, 0.02f, 0.02f, 1.0f};
    r->fb_size = (vec2i){1, 1};

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);

    glGenVertexArrays(1, &r->fs_vao);

    R_make_black_tex();
    R_create_targets(r);

    r->lights = create_vector(light_t);
    r->models = create_vector(pushed_model_t);
    r->shaders = create_vector(shader_t *);

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

    R_lights_ubo_init(r);

    if (!R_bloom_init(r))
        LOG_ERROR("Bloom init failed");

    cvar_set_callback_name("cl_bloom", R_on_bloom_change);
    cvar_set_callback_name("cl_render_debug", R_on_debug_mode_change);

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

    R_bloom_free_textures(r);
    R_bloom_free_shaders(r);

    R_lights_ubo_shutdown(r);

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

    R_gl_delete_targets(r);

    r->assets = NULL;
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
    R_bloom_ensure(r);
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
}

void R_end_frame(renderer_t *r)
{
    if (!r)
        return;

    R_lights_ubo_upload(r);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->lights_ubo);

    R_deferred_geom_pass(r);
    R_deferred_light_pass(r);
    R_forward_transparent_pass(r);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    R_bloom_run(r);
    R_composite_to_final(r);
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
