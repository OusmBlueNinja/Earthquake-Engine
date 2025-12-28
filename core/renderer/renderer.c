#include "renderer/renderer.h"
#include "renderer/ibl.h"
#include "renderer/bloom.h"
#include "renderer/ssr.h"
#include "shader.h"
#include "utils/logger.h"
#include "utils/macros.h"
#include "cvar.h"
#include "core.h"
#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/model.h"
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

typedef struct pt_tri_gpu_t
{
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 n0;
    vec4 n1;
    vec4 n2;
    vec4 uv0;
    vec4 uv1;
    vec4 uv2;
    int mat[4];
} pt_tri_gpu_t;

typedef struct pt_bvh_gpu_t
{
    vec4 bmin;
    vec4 bmax;
    int meta[4];
} pt_bvh_gpu_t;

typedef struct pt_mat_gpu_t
{
    vec4 albedo;
    vec4 emissive;
    vec4 params;
    int flags[4];
} pt_mat_gpu_t;

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

static inline render_stats_t *R_stats_write(renderer_t *r)
{
    return &r->stats[r->stats_write ? 0u : 1u];
}

static void R_stats_begin_frame(renderer_t *r)
{
    r->stats_write = !r->stats_write;
    memset(R_stats_write(r), 0, sizeof(render_stats_t));
}

static void R_make_black_tex(renderer_t *r)
{
    if (r->black_tex)
        return;

    glGenTextures(1, &r->black_tex);
    glBindTexture(GL_TEXTURE_2D, r->black_tex);

    {
        const float px[4] = {0, 0, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, px);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
}

static void R_make_black_cube(renderer_t *r)
{
    if (r->black_cube)
        return;

    glGenTextures(1, &r->black_cube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, r->black_cube);

    {
        const float px[4] = {0, 0, 0, 0};
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

static void R_fix_env_cubemap_runtime(uint32_t env_tex)
{
    if (!env_tex)
        return;
    glBindTexture(GL_TEXTURE_CUBE_MAP, env_tex);
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

    r->cfg.exposure = cvar_get_float_name("cl_r_exposure_level");
    r->cfg.exposure_auto = cvar_get_bool_name("cl_r_exposure_auto") ? 1 : 0;

    r->cfg.output_gamma = cvar_get_float_name("cl_r_output_gamma");
    r->cfg.manual_srgb = cvar_get_bool_name("cl_r_manual_srgb") ? 1 : 0;

    r->cfg.wireframe = cvar_get_bool_name("cl_r_wireframe") ? 1 : 0;

    r->cfg.pt_enabled = cvar_get_bool_name("cl_r_pt_enabled") ? 1 : 0;
    r->cfg.pt_spp = cvar_get_int_name("cl_r_pt_spp");
    if (r->cfg.pt_spp < 1)
        r->cfg.pt_spp = 1;
    if (r->cfg.pt_spp > 16)
        r->cfg.pt_spp = 16;

    r->cfg.pt_env_intensity = cvar_get_float_name("cl_r_pt_env_intensity");
    if (r->cfg.pt_env_intensity < 0.0f)
        r->cfg.pt_env_intensity = 0.0f;
}

static void R_on_cvar_any(renderer_t *r)
{
    R_cfg_pull_from_cvars(r);

    bloom_set_params(r,
                     r->cfg.bloom_threshold,
                     r->cfg.bloom_knee,
                     r->cfg.bloom_intensity,
                     r->cfg.bloom_mips);

    r->pt.reset_accum = 1;
}

static void R_on_r_cvar_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    (void)old_state;
    (void)state;
    renderer_t *r = &get_application()->renderer;
    R_on_cvar_any(r);
}

shader_t *R_new_shader_from_files(const char *vp, const char *fp)
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

static shader_t *R_new_compute_shader_from_file(const char *path)
{
    shader_t tmp = shader_create();
    if (!shader_load_compute_from_file(&tmp, path))
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
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void R_gl_delete_targets(renderer_t *r)
{
    if (r->final_fbo)
        glDeleteFramebuffers(1, &r->final_fbo);
    if (r->present_fbo)
        glDeleteFramebuffers(1, &r->present_fbo);

    if (r->gbuf_depth)
        glDeleteTextures(1, &r->gbuf_depth);
    if (r->final_color_tex)
        glDeleteTextures(1, &r->final_color_tex);
    if (r->present_color_tex)
        glDeleteTextures(1, &r->present_color_tex);

    if (r->pt.accum_tex)
        glDeleteTextures(1, &r->pt.accum_tex);

    r->final_fbo = 0;
    r->present_fbo = 0;

    r->gbuf_depth = 0;
    r->final_color_tex = 0;
    r->present_color_tex = 0;

    r->pt.accum_tex = 0;
}

static void R_create_targets(renderer_t *r)
{
    R_gl_delete_targets(r);

    if (r->fb_size.x < 1)
        r->fb_size.x = 1;
    if (r->fb_size.y < 1)
        r->fb_size.y = 1;

    R_alloc_tex2d(&r->gbuf_depth, GL_DEPTH_COMPONENT32F, r->fb_size.x, r->fb_size.y, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, GL_NEAREST);

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

    glGenFramebuffers(1, &r->present_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->present_fbo);

    R_alloc_tex2d(&r->present_color_tex, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->present_color_tex, 0);

    {
        uint32_t bufs[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, (const GLenum *)bufs);
    }

    {
        uint32_t status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            LOG_ERROR("Present FBO incomplete: 0x%x", (unsigned)status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    R_alloc_tex2d(&r->pt.accum_tex, GL_RGBA16F, r->fb_size.x, r->fb_size.y, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_NEAREST);
    {
        float z[4] = {0, 0, 0, 0};
        glClearTexImage(r->pt.accum_tex, 0, GL_RGBA, GL_FLOAT, z);
    }

    r->pt.reset_accum = 1;
}

static void R_copy_final_to_present(renderer_t *r)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->final_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->present_fbo);
    glBlitFramebuffer(
        0, 0, r->fb_size.x, r->fb_size.y,
        0, 0, r->fb_size.x, r->fb_size.y,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
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
    return r->present_fbo;
}

uint32_t R_get_present_tex(const renderer_t *r)
{
    if (!r)
        return 0;
    return r->present_color_tex;
}

static void R_pt_delete_buffers(renderer_t *r)
{
    if (r->pt.tris_ssbo)
        glDeleteBuffers(1, &r->pt.tris_ssbo);
    if (r->pt.bvh_ssbo)
        glDeleteBuffers(1, &r->pt.bvh_ssbo);
    if (r->pt.mats_ssbo)
        glDeleteBuffers(1, &r->pt.mats_ssbo);

    r->pt.tris_ssbo = 0;
    r->pt.bvh_ssbo = 0;
    r->pt.mats_ssbo = 0;

    r->pt.tri_cap = 0;
    r->pt.node_cap = 0;
    r->pt.mat_cap = 0;

    r->pt.tri_count = 0;
    r->pt.node_count = 0;
    r->pt.mat_count = 0;
}

static void R_pt_ensure_ssbo(uint32_t *ssbo, uint32_t bytes)
{
    if (!*ssbo)
        glGenBuffers(1, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, *ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)bytes, 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static void R_pt_upload_ssbo(uint32_t ssbo, const void *data, uint32_t bytes)
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)bytes, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

typedef struct pt_aabb_t
{
    vec3 bmin;
    vec3 bmax;
} pt_aabb_t;

static pt_aabb_t pt_aabb_empty(void)
{
    pt_aabb_t a;
    a.bmin = (vec3){1e30f, 1e30f, 1e30f};
    a.bmax = (vec3){-1e30f, -1e30f, -1e30f};
    return a;
}

static pt_aabb_t pt_aabb_union(pt_aabb_t a, pt_aabb_t b)
{
    pt_aabb_t o;
    o.bmin.x = (a.bmin.x < b.bmin.x) ? a.bmin.x : b.bmin.x;
    o.bmin.y = (a.bmin.y < b.bmin.y) ? a.bmin.y : b.bmin.y;
    o.bmin.z = (a.bmin.z < b.bmin.z) ? a.bmin.z : b.bmin.z;
    o.bmax.x = (a.bmax.x > b.bmax.x) ? a.bmax.x : b.bmax.x;
    o.bmax.y = (a.bmax.y > b.bmax.y) ? a.bmax.y : b.bmax.y;
    o.bmax.z = (a.bmax.z > b.bmax.z) ? a.bmax.z : b.bmax.z;
    return o;
}

static pt_aabb_t pt_tri_aabb(const pt_tri_gpu_t *t)
{
    pt_aabb_t a = pt_aabb_empty();
    vec3 p0 = (vec3){t->v0.x, t->v0.y, t->v0.z};
    vec3 p1 = (vec3){t->v1.x, t->v1.y, t->v1.z};
    vec3 p2 = (vec3){t->v2.x, t->v2.y, t->v2.z};

    a.bmin.x = fminf(p0.x, fminf(p1.x, p2.x));
    a.bmin.y = fminf(p0.y, fminf(p1.y, p2.y));
    a.bmin.z = fminf(p0.z, fminf(p1.z, p2.z));

    a.bmax.x = fmaxf(p0.x, fmaxf(p1.x, p2.x));
    a.bmax.y = fmaxf(p0.y, fmaxf(p1.y, p2.y));
    a.bmax.z = fmaxf(p0.z, fmaxf(p1.z, p2.z));

    return a;
}

static vec3 pt_tri_centroid(const pt_tri_gpu_t *t)
{
    return (vec3){
        (t->v0.x + t->v1.x + t->v2.x) * (1.0f / 3.0f),
        (t->v0.y + t->v1.y + t->v2.y) * (1.0f / 3.0f),
        (t->v0.z + t->v1.z + t->v2.z) * (1.0f / 3.0f)};
}

static int pt_axis_for_aabb(pt_aabb_t a)
{
    float ex = a.bmax.x - a.bmin.x;
    float ey = a.bmax.y - a.bmin.y;
    float ez = a.bmax.z - a.bmin.z;
    if (ex > ey && ex > ez)
        return 0;
    if (ey > ez)
        return 1;
    return 2;
}

static int pt_tri_cmp_axis;
static int pt_cmp_tris(const void *pa, const void *pb)
{
    const pt_tri_gpu_t *a = (const pt_tri_gpu_t *)pa;
    const pt_tri_gpu_t *b = (const pt_tri_gpu_t *)pb;
    vec3 ca = pt_tri_centroid(a);
    vec3 cb = pt_tri_centroid(b);
    float va = (pt_tri_cmp_axis == 0) ? ca.x : (pt_tri_cmp_axis == 1 ? ca.y : ca.z);
    float vb = (pt_tri_cmp_axis == 0) ? cb.x : (pt_tri_cmp_axis == 1 ? cb.y : cb.z);
    if (va < vb)
        return -1;
    if (va > vb)
        return 1;
    return 0;
}

static uint32_t pt_build_bvh_recursive(pt_bvh_gpu_t *nodes, uint32_t *ioNodeCount, pt_tri_gpu_t *tris, uint32_t first, uint32_t count)
{
    uint32_t nodeIndex = (*ioNodeCount)++;
    pt_bvh_gpu_t *n = &nodes[nodeIndex];

    pt_aabb_t bounds = pt_aabb_empty();
    for (uint32_t i = 0; i < count; ++i)
        bounds = pt_aabb_union(bounds, pt_tri_aabb(&tris[first + i]));

    n->bmin = (vec4){bounds.bmin.x, bounds.bmin.y, bounds.bmin.z, 0};
    n->bmax = (vec4){bounds.bmax.x, bounds.bmax.y, bounds.bmax.z, 0};

    if (count <= 4)
    {
        n->meta[0] = -1;
        n->meta[1] = -1;
        n->meta[2] = (int)first;
        n->meta[3] = (int)count;
        return nodeIndex;
    }

    int axis = pt_axis_for_aabb(bounds);
    pt_tri_cmp_axis = axis;
    qsort(&tris[first], (size_t)count, sizeof(pt_tri_gpu_t), pt_cmp_tris);

    uint32_t leftCount = count / 2;
    uint32_t rightCount = count - leftCount;

    uint32_t left = pt_build_bvh_recursive(nodes, ioNodeCount, tris, first, leftCount);
    uint32_t right = pt_build_bvh_recursive(nodes, ioNodeCount, tris, first + leftCount, rightCount);

    n->meta[0] = (int)left;
    n->meta[1] = (int)right;
    n->meta[2] = -1;
    n->meta[3] = 0;

    return nodeIndex;
}

static uint32_t R_pt_find_or_add_material(renderer_t *r, pt_mat_gpu_t *mats, uint32_t matCap, uint32_t *ioMatCount, ihandle_t matHandle)
{
    uint32_t mc = *ioMatCount;
    if (mc >= matCap)
        return 0;

    asset_material_t *mat = NULL;
    if (ihandle_is_valid(matHandle))
    {
        const asset_any_t *a = asset_manager_get_any(r->assets, matHandle);
        if (a && a->type == ASSET_MATERIAL && a->state == ASSET_STATE_READY)
            mat = (asset_material_t *)&a->as.material;
    }

    pt_mat_gpu_t m;
    memset(&m, 0, sizeof(m));

    if (mat)
    {
        m.albedo = (vec4){mat->albedo.x, mat->albedo.y, mat->albedo.z, 1.0f};
        m.emissive = (vec4){mat->emissive.x, mat->emissive.y, mat->emissive.z, 1.0f};
        m.params = (vec4){mat->roughness, mat->metallic, mat->opacity, 0.0f};
        m.flags[0] = (int)mat->flags;
    }
    else
    {
        m.albedo = (vec4){0.8f, 0.8f, 0.8f, 1.0f};
        m.emissive = (vec4){0, 0, 0, 1};
        m.params = (vec4){1, 0, 1, 0};
        m.flags[0] = 0;
    }

    mats[mc] = m;
    *ioMatCount = mc + 1;
    return mc;
}

static void R_pt_append_mesh_lod0_from_gpu(renderer_t *r, const mesh_lod_t *lod, ihandle_t material, const mat4 *M,
                                           pt_tri_gpu_t **ioTris, uint32_t *ioTriCap, uint32_t *ioTriCount,
                                           pt_mat_gpu_t **ioMats, uint32_t *ioMatCap, uint32_t *ioMatCount)
{
    if (!lod || !lod->vbo || !lod->ibo || lod->index_count < 3)
        return;

    GLint vboBytes = 0;
    GLint iboBytes = 0;

    glBindBuffer(GL_ARRAY_BUFFER, lod->vbo);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboBytes);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod->ibo);
    glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &iboBytes);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    if (vboBytes <= 0 || iboBytes <= 0)
        return;

    uint32_t vcount = (uint32_t)((uint32_t)vboBytes / (uint32_t)sizeof(model_vertex_t));
    uint32_t icount = (uint32_t)((uint32_t)iboBytes / (uint32_t)sizeof(uint32_t));
    if (vcount < 3 || icount < 3)
        return;

    model_vertex_t *verts = (model_vertex_t *)malloc((size_t)vcount * sizeof(model_vertex_t));
    uint32_t *inds = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!verts || !inds)
    {
        free(verts);
        free(inds);
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, lod->vbo);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, vboBytes, verts);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod->ibo);
    glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, iboBytes, inds);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    uint32_t matIndex = R_pt_find_or_add_material(r, *ioMats, *ioMatCap, ioMatCount, material);

    uint32_t triCount = icount / 3u;

    if (*ioTriCount + triCount > *ioTriCap)
    {
        uint32_t need = *ioTriCount + triCount + 8192u;
        uint32_t newCap = u32_next_pow2(need);
        pt_tri_gpu_t *nt = (pt_tri_gpu_t *)realloc(*ioTris, (size_t)newCap * sizeof(pt_tri_gpu_t));
        if (!nt)
        {
            free(verts);
            free(inds);
            return;
        }
        *ioTris = nt;
        *ioTriCap = newCap;
    }

    for (uint32_t t = 0; t < triCount; ++t)
    {
        uint32_t i0 = inds[t * 3u + 0u];
        uint32_t i1 = inds[t * 3u + 1u];
        uint32_t i2 = inds[t * 3u + 2u];
        if (i0 >= vcount || i1 >= vcount || i2 >= vcount)
            continue;

        model_vertex_t v0 = verts[i0];
        model_vertex_t v1 = verts[i1];
        model_vertex_t v2 = verts[i2];

        vec4 p0 = (vec4){v0.px, v0.py, v0.pz, 1.0f};
        vec4 p1 = (vec4){v1.px, v1.py, v1.pz, 1.0f};
        vec4 p2 = (vec4){v2.px, v2.py, v2.pz, 1.0f};

        vec4 wp0 = mat4_mul_vec4(*M, p0);
        vec4 wp1 = mat4_mul_vec4(*M, p1);
        vec4 wp2 = mat4_mul_vec4(*M, p2);

        pt_tri_gpu_t tri;
        memset(&tri, 0, sizeof(tri));

        tri.v0 = wp0;
        tri.v1 = wp1;
        tri.v2 = wp2;

        tri.n0 = (vec4){v0.nx, v0.ny, v0.nz, 0};
        tri.n1 = (vec4){v1.nx, v1.ny, v1.nz, 0};
        tri.n2 = (vec4){v2.nx, v2.ny, v2.nz, 0};

        tri.uv0 = (vec4){v0.u, v0.v, 0, 0};
        tri.uv1 = (vec4){v1.u, v1.v, 0, 0};
        tri.uv2 = (vec4){v2.u, v2.v, 0, 0};

        tri.mat[0] = (int)matIndex;

        (*ioTris)[(*ioTriCount)++] = tri;
    }

    free(verts);
    free(inds);
}

static void R_pt_build_scene(renderer_t *r)
{
    r->pt.tri_count = 0;
    r->pt.mat_count = 0;
    r->pt.node_count = 0;

    uint32_t triCap = r->pt.tri_cap;
    uint32_t matCap = r->pt.mat_cap;

    if (triCap < 4096u)
        triCap = 4096u;
    if (matCap < 512u)
        matCap = 512u;

    pt_tri_gpu_t *tris = (pt_tri_gpu_t *)malloc((size_t)triCap * sizeof(pt_tri_gpu_t));
    pt_mat_gpu_t *mats = (pt_mat_gpu_t *)malloc((size_t)matCap * sizeof(pt_mat_gpu_t));
    if (!tris || !mats)
    {
        free(tris);
        free(mats);
        return;
    }

    uint32_t triCount = 0;
    uint32_t matCount = 0;

    for (uint32_t i = 0; i < r->models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        const asset_any_t *a = asset_manager_get_any(r->assets, pm->model);
        if (!a || a->type != ASSET_MODEL || a->state != ASSET_STATE_READY)
            continue;

        asset_model_t *mdl = (asset_model_t *)&a->as.model;

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;
            if (mesh->lods.size < 1)
                continue;

            mesh_lod_t *lod0 = (mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 0);
            if (!lod0 || !lod0->vbo || !lod0->ibo)
                continue;

            R_pt_append_mesh_lod0_from_gpu(r, lod0, mesh->material, &pm->model_matrix,
                                           &tris, &triCap, &triCount,
                                           &mats, &matCap, &matCount);
        }
    }

    if (triCount < 1u)
    {
        free(tris);
        free(mats);
        r->pt.tri_count = 0;
        r->pt.mat_count = 0;
        r->pt.node_count = 0;
        LOG_WARN("PT: no triangles extracted (you will only see sky/env)");
        return;
    }

    uint32_t nodeCap = r->pt.node_cap;
    if (nodeCap < triCount * 2u)
        nodeCap = triCount * 2u;
    if (nodeCap < 8u)
        nodeCap = 8u;

    pt_bvh_gpu_t *nodes = (pt_bvh_gpu_t *)malloc((size_t)nodeCap * sizeof(pt_bvh_gpu_t));
    if (!nodes)
    {
        free(tris);
        free(mats);
        return;
    }

    uint32_t nodeCount = 0;
    pt_build_bvh_recursive(nodes, &nodeCount, tris, 0u, triCount);

    if (triCap > r->pt.tri_cap)
    {
        r->pt.tri_cap = triCap;
        R_pt_ensure_ssbo(&r->pt.tris_ssbo, (uint32_t)(sizeof(pt_tri_gpu_t) * (size_t)r->pt.tri_cap));
    }
    if (matCap > r->pt.mat_cap)
    {
        r->pt.mat_cap = matCap;
        R_pt_ensure_ssbo(&r->pt.mats_ssbo, (uint32_t)(sizeof(pt_mat_gpu_t) * (size_t)r->pt.mat_cap));
    }
    if (nodeCap > r->pt.node_cap)
    {
        r->pt.node_cap = nodeCap;
        R_pt_ensure_ssbo(&r->pt.bvh_ssbo, (uint32_t)(sizeof(pt_bvh_gpu_t) * (size_t)r->pt.node_cap));
    }

    R_pt_upload_ssbo(r->pt.tris_ssbo, tris, (uint32_t)(sizeof(pt_tri_gpu_t) * (size_t)triCount));
    R_pt_upload_ssbo(r->pt.mats_ssbo, mats, (uint32_t)(sizeof(pt_mat_gpu_t) * (size_t)matCount));
    R_pt_upload_ssbo(r->pt.bvh_ssbo, nodes, (uint32_t)(sizeof(pt_bvh_gpu_t) * (size_t)nodeCount));

    r->pt.tri_count = triCount;
    r->pt.mat_count = matCount;
    r->pt.node_count = nodeCount;

    free(nodes);
    free(tris);
    free(mats);

    LOG_INFO("PT scene built: tris=%u nodes=%u mats=%u", r->pt.tri_count, r->pt.node_count, r->pt.mat_count);
}

static void R_pt_reset_accum(renderer_t *r)
{
    float z[4] = {0, 0, 0, 0};
    glClearTexImage(r->pt.accum_tex, 0, GL_RGBA, GL_FLOAT, z);
    r->pt.frame_index = 0;
    r->pt.reset_accum = 0;
}

static int mat4_differs(const mat4 *a, const mat4 *b, float eps)
{
    for (int i = 0; i < 16; ++i)
    {
        float d = a->m[i] - b->m[i];
        if (fabsf(d) > eps)
            return 1;
    }
    return 0;
}

static void R_pt_dispatch(renderer_t *r)
{
    shader_t *pt = (r->pt.shader_trace_id != 0xFF) ? R_get_shader(r, r->pt.shader_trace_id) : NULL;
    shader_t *fin = (r->pt.shader_finalize_id != 0xFF) ? R_get_shader(r, r->pt.shader_finalize_id) : NULL;
    if (!pt || !fin)
        return;

    if (mat4_differs(&r->camera.view, &r->pt.last_view, 1e-6f) ||
        mat4_differs(&r->camera.proj, &r->pt.last_proj, 1e-6f) ||
        fabsf(r->camera.position.x - r->pt.last_cam_pos.x) > 1e-6f ||
        fabsf(r->camera.position.y - r->pt.last_cam_pos.y) > 1e-6f ||
        fabsf(r->camera.position.z - r->pt.last_cam_pos.z) > 1e-6f)
    {
        r->pt.reset_accum = 1;
    }

    r->pt.last_view = r->camera.view;
    r->pt.last_proj = r->camera.proj;
    r->pt.last_cam_pos = r->camera.position;

    if (r->pt.scene_dirty)
    {
        R_pt_build_scene(r);
        r->pt.scene_dirty = 0;
        r->pt.reset_accum = 1;
    }

    if (r->pt.reset_accum)
        R_pt_reset_accum(r);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, r->pt.tris_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, r->pt.bvh_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, r->pt.mats_ssbo);

    uint32_t env = ibl_get_env(r);
    if (env)
        R_fix_env_cubemap_runtime(env);

    shader_bind(pt);

    glBindImageTexture(0, r->pt.accum_tex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

    shader_set_ivec2(pt, "u_ScreenSize", r->fb_size);
    shader_set_int(pt, "u_TriCount", (int)r->pt.tri_count);
    shader_set_int(pt, "u_NodeCount", (int)r->pt.node_count);
    shader_set_int(pt, "u_MatCount", (int)r->pt.mat_count);

    shader_set_uint(pt, "u_FrameIndex", r->pt.frame_index);
    shader_set_uint(pt, "u_Spp", (uint32_t)r->cfg.pt_spp);

    shader_set_mat4(pt, "u_InvView", r->camera.inv_view);
    shader_set_mat4(pt, "u_InvProj", r->camera.inv_proj);
    shader_set_vec3(pt, "u_CamPos", r->camera.position);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env ? env : r->black_cube);
    shader_set_int(pt, "u_Env", 0);
    shader_set_float(pt, "u_EnvIntensity", r->cfg.pt_env_intensity);

    uint32_t gx = (uint32_t)((r->fb_size.x + 7) / 8);
    uint32_t gy = (uint32_t)((r->fb_size.y + 7) / 8);
    if (gx < 1)
        gx = 1;
    if (gy < 1)
        gy = 1;

    shader_dispatch_compute(pt, gx, gy, 1);

    shader_memory_barrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    shader_bind(fin);

    glBindImageTexture(0, r->pt.accum_tex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    glBindImageTexture(1, r->final_color_tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    shader_set_ivec2(fin, "u_ScreenSize", r->fb_size);
    shader_set_float(fin, "u_Exposure", r->cfg.exposure);
    shader_set_float(fin, "u_Gamma", r->cfg.output_gamma);
    shader_set_uint(fin, "u_FrameIndex", r->pt.frame_index);

    shader_dispatch_compute(fin, gx, gy, 1);

    shader_unbind();
    shader_memory_barrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    r->pt.frame_index += 1;
}

int R_init(renderer_t *r, asset_manager_t *assets)
{
    if (!r)
        return 1;

    memset(r, 0, sizeof(*r));
    r->assets = assets;

    r->default_shader_id = 0xFF;
    r->sky_shader_id = 0xFF;
    r->present_shader_id = 0xFF;
    r->depth_shader_id = 0xFF;

    r->pt.shader_trace_id = 0xFF;
    r->pt.shader_finalize_id = 0xFF;

    R_cfg_pull_from_cvars(r);

    r->clear_color = (vec4){0.02f, 0.02f, 0.02f, 1.0f};
    r->fb_size = (vec2i){1, 1};

    r->hdri_tex = ihandle_invalid();

    glGenVertexArrays(1, &r->fs_vao);

    R_make_black_tex(r);
    R_make_black_cube(r);

    r->lights = create_vector(light_t);
    r->models = create_vector(pushed_model_t);
    r->fwd_models = create_vector(pushed_model_t);
    r->shaders = create_vector(shader_t *);

    shader_t *pt_shader = R_new_compute_shader_from_file("res/shaders/PathTrace/pathtrace.comp");
    shader_t *pt_fin = R_new_compute_shader_from_file("res/shaders/PathTrace/pathtrace_finalize.comp");
    if (!pt_shader || !pt_fin)
        return 1;

    r->pt.shader_trace_id = R_add_shader(r, pt_shader);
    r->pt.shader_finalize_id = R_add_shader(r, pt_fin);

    if (!ibl_init(r))
        LOG_ERROR("IBL init failed");
    if (!bloom_init(r))
        LOG_ERROR("Bloom init failed");
    if (!ssr_init(r))
        LOG_ERROR("SSR init failed");

    bloom_set_params(r, r->cfg.bloom_threshold, r->cfg.bloom_knee, r->cfg.bloom_intensity, r->cfg.bloom_mips);

    R_create_targets(r);

    memset(r->stats, 0, sizeof(r->stats));
    r->stats_write = 0;

    r->pt.tris_ssbo = 0;
    r->pt.bvh_ssbo = 0;
    r->pt.mats_ssbo = 0;

    r->pt.tri_cap = 0;
    r->pt.node_cap = 0;
    r->pt.mat_cap = 0;

    r->pt.tri_count = 0;
    r->pt.node_count = 0;
    r->pt.mat_count = 0;

    r->pt.scene_dirty = 1;
    r->pt.reset_accum = 1;
    r->pt.frame_index = 0;

    cvar_set_callback_name("cl_bloom", R_on_r_cvar_change);
    cvar_set_callback_name("cl_render_debug", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_bloom_threshold", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_bloom_knee", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_bloom_intensity", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_bloom_mips", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_exposure_level", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_exposure_auto", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_output_gamma", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_manual_srgb", R_on_r_cvar_change);

    cvar_set_callback_name("cl_r_pt_enabled", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_pt_spp", R_on_r_cvar_change);
    cvar_set_callback_name("cl_r_pt_env_intensity", R_on_r_cvar_change);

    return 0;
}

void R_shutdown(renderer_t *r)
{
    if (!r)
        return;

    bloom_shutdown(r);
    ssr_shutdown(r);
    ibl_shutdown(r);

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
    R_pt_delete_buffers(r);

    r->assets = NULL;

    if (r->black_tex)
        glDeleteTextures(1, &r->black_tex);
    r->black_tex = 0;

    if (r->black_cube)
        glDeleteTextures(1, &r->black_cube);
    r->black_cube = 0;
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

    r->pt.reset_accum = 1;
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

    R_stats_begin_frame(r);
}

void R_end_frame(renderer_t *r)
{
    if (!r)
        return;

    ibl_ensure(r);

    glBindFramebuffer(GL_FRAMEBUFFER, r->final_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    if (r->cfg.pt_enabled)
    {
        R_pt_dispatch(r);

        if (r->cfg.bloom)
        {
            bloom_run(r, r->final_color_tex, r->black_tex);
            uint32_t bloom_tex = (r->bloom.mips) ? r->bloom.tex_up[0] : 0;
            bloom_composite_to_final(r, r->final_color_tex, bloom_tex, r->gbuf_depth, r->black_tex);
        }

        R_copy_final_to_present(r);
        return;
    }

    glClearColor(r->clear_color.x, r->clear_color.y, r->clear_color.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    R_copy_final_to_present(r);
}

void R_push_camera(renderer_t *r, const camera_t *cam)
{
    if (!r || !cam)
        return;
    r->camera = *cam;
    r->pt.reset_accum = 1;
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

    r->pt.scene_dirty = 1;
}

void R_push_hdri(renderer_t *r, ihandle_t tex)
{
    if (!r)
        return;
    r->hdri_tex = tex;
    r->pt.reset_accum = 1;
}

const render_stats_t *R_get_stats(const renderer_t *r)
{
    return &r->stats[r->stats_write ? 1u : 0u];
}
