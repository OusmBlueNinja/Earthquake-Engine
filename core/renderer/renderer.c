#include "renderer/renderer.h"
#include "renderer/ibl.h"
#include "renderer/bloom.h"
#include "renderer/ssr.h"
#include "shader.h"
#include "utils/logger.h"
#include "utils/macros.h"
#include "cvar.h"
#include "core.h"
#include "renderer/frame_graph.h"
#include "renderer/gl_state_cache.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#if !defined(_WIN32)
#include <time.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#define FP_TILE_SIZE 32

#define R_SHADOW_LIGHT_DISTANCE 200.0f
#define R_SHADOW_PAD_XY 10.0f
#define R_SHADOW_PAD_Z 50.0f

static void R_bind_common_uniforms(renderer_t *r, const shader_t *s);

renderer_scene_settings_t R_scene_settings_default(void)
{
    renderer_scene_settings_t s;
    memset(&s, 0, sizeof(s));

    s.bloom_threshold = 1.0f;
    s.bloom_knee = 0.5f;
    s.bloom_intensity = 0.08f;
    s.bloom_mips = 6u;

    s.exposure = 1.0f;
    s.exposure_auto = cvar_get_bool_name("cl_auto_exposure") ? 1 : 0;
    s.delta_time = 1.0f / 60.0f;
    s.auto_exposure_speed = 2.0f;
    s.auto_exposure_min = 0.05f;
    s.auto_exposure_max = 8.0f;

    s.output_gamma = 2.2f;
    s.manual_srgb = 0;

    s.alpha_test = 0;
    s.alpha_cutoff = 0.5f;

    s.height_invert = 0;
    s.ibl_intensity = 0.2f;

    s.ssr = 0;
    s.ssr_intensity = 1.0f;
    s.ssr_steps = 48;
    s.ssr_stride = 0.12f;
    s.ssr_thickness = 0.18f;
    s.ssr_max_dist = 40.0f;

    s.shadow_cascades = 4;
    s.shadow_map_size = 2048;
    s.shadow_max_dist = 120.0f;
    s.shadow_split_lambda = 0.65f;
    s.shadow_bias = 0.0012f;
    s.shadow_normal_bias = 0.0035f;
    s.shadow_pcf = 1;

    return s;
}

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

    const mesh_lod_t *lod_ptr;
    const asset_material_t *mat_ptr;
    uint64_t tex_key;

    uint8_t mat_cutout;
    uint8_t mat_blend;
    uint8_t mat_doublesided;
    uint8_t pad0;
    float alpha_cutoff;
    uint32_t albedo_tex;
} inst_batch_t;

typedef struct inst_item_t
{
    ihandle_t model;
    uint32_t mesh_index;
    uint32_t lod;
    float fade01;
    mat4 m;

    const mesh_lod_t *lod_ptr;
    const asset_material_t *mat_ptr;
    uint64_t tex_key;

    uint8_t mat_cutout;
    uint8_t mat_blend;
    uint8_t mat_doublesided;
    uint8_t pad0;
    float alpha_cutoff;
    uint32_t albedo_tex;
} inst_item_t;

typedef struct instance_gpu_t
{
    mat4 m;
    float fade01;
    float pad0;
    float pad1;
    float pad2;
} instance_gpu_t;

typedef struct line3d_vertex_t
{
    float pos[3];
    float color[4];
} line3d_vertex_t;

static inst_item_t *g_inst_item_scratch = NULL;
static uint32_t g_inst_item_scratch_cap = 0;

static line3d_vertex_t *g_line3d_vert_scratch = NULL;
static uint32_t g_line3d_vert_scratch_cap = 0;

static inst_item_t *R_inst_item_scratch(uint32_t need)
{
    if (need <= g_inst_item_scratch_cap && g_inst_item_scratch)
        return g_inst_item_scratch;

    inst_item_t *p = (inst_item_t *)realloc(g_inst_item_scratch, sizeof(inst_item_t) * (size_t)need);
    if (!p)
        return NULL;

    g_inst_item_scratch = p;
    g_inst_item_scratch_cap = need;
    return g_inst_item_scratch;
}

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

static line3d_vertex_t *R_line3d_vert_scratch(uint32_t need)
{
    if (need <= g_line3d_vert_scratch_cap && g_line3d_vert_scratch)
        return g_line3d_vert_scratch;

    line3d_vertex_t *p = (line3d_vertex_t *)realloc(g_line3d_vert_scratch, sizeof(line3d_vertex_t) * (size_t)need);
    if (!p)
        return NULL;

    g_line3d_vert_scratch = p;
    g_line3d_vert_scratch_cap = need;
    return g_line3d_vert_scratch;
}

typedef struct u32_set_t
{
    uint32_t *keys;
    uint32_t cap;  
    uint32_t size;
} u32_set_t;

static u32_set_t g_instanced_vao_set = {0};

typedef struct model_bounds_entry_t
{
    uint64_t key;
    vec3 c_local;
    float r_local;
} model_bounds_entry_t;

static model_bounds_entry_t *g_model_bounds = NULL;
static uint32_t g_model_bounds_cap = 0;
static uint32_t g_model_bounds_size = 0;

static uint64_t model_key64(ihandle_t h)
{
    return ((uint64_t)h.type << 48) | ((uint64_t)h.meta << 32) | (uint64_t)h.value;
}

static uint32_t u64_hash32(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdu;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53u;
    x ^= x >> 33;
    return (uint32_t)x;
}

static uint64_t u64_fnv1a(uint64_t h, uint64_t v)
{
    h ^= v;
    h *= 1099511628211ull;
    return h;
}

static uint64_t R_material_tex_key(const asset_material_t *mat)
{
    if (!mat)
        return 0ull;

    uint64_t h = 1469598103934665603ull;
    h = u64_fnv1a(h, (uint64_t)mat->shader_id);
    h = u64_fnv1a(h, (uint64_t)mat->flags);

    h = u64_fnv1a(h, model_key64(mat->albedo_tex));
    h = u64_fnv1a(h, model_key64(mat->normal_tex));
    h = u64_fnv1a(h, model_key64(mat->metallic_tex));
    h = u64_fnv1a(h, model_key64(mat->roughness_tex));
    h = u64_fnv1a(h, model_key64(mat->emissive_tex));
    h = u64_fnv1a(h, model_key64(mat->occlusion_tex));
    h = u64_fnv1a(h, model_key64(mat->height_tex));
    h = u64_fnv1a(h, model_key64(mat->arm_tex));

    return h;
}

static void model_bounds_free(void)
{
    free(g_model_bounds);
    g_model_bounds = NULL;
    g_model_bounds_cap = 0;
    g_model_bounds_size = 0;
}

static void model_bounds_rehash(uint32_t new_cap)
{
    if (new_cap < 64u)
        new_cap = 64u;
    new_cap = u32_next_pow2(new_cap);

    model_bounds_entry_t *n = (model_bounds_entry_t *)calloc((size_t)new_cap, sizeof(model_bounds_entry_t));
    if (!n)
        return;

    model_bounds_entry_t *old = g_model_bounds;
    uint32_t old_cap = g_model_bounds_cap;

    g_model_bounds = n;
    g_model_bounds_cap = new_cap;
    g_model_bounds_size = 0;

    if (old && old_cap)
    {
        for (uint32_t i = 0; i < old_cap; ++i)
        {
            if (!old[i].key)
                continue;
            uint32_t mask = g_model_bounds_cap - 1u;
            uint32_t idx = u64_hash32(old[i].key) & mask;
            while (g_model_bounds[idx].key)
                idx = (idx + 1u) & mask;
            g_model_bounds[idx] = old[i];
            g_model_bounds_size++;
        }
    }

    free(old);
}

static const model_bounds_entry_t *model_bounds_get_or_build(ihandle_t model_h, const asset_model_t *mdl)
{
    if (!ihandle_is_valid(model_h) || !mdl)
        return NULL;

    uint64_t key = model_key64(model_h);
    if (!key)
        return NULL;

    if (!g_model_bounds || !g_model_bounds_cap)
        model_bounds_rehash(256u);
    if (!g_model_bounds || !g_model_bounds_cap)
        return NULL;

    if ((g_model_bounds_size + 1u) * 10u >= g_model_bounds_cap * 7u)
        model_bounds_rehash(g_model_bounds_cap * 2u);

    uint32_t mask = g_model_bounds_cap - 1u;
    uint32_t idx = u64_hash32(key) & mask;
    while (g_model_bounds[idx].key && g_model_bounds[idx].key != key)
        idx = (idx + 1u) & mask;

    if (g_model_bounds[idx].key == key)
        return &g_model_bounds[idx];

    aabb_t a;
    a.min = (vec3){1e30f, 1e30f, 1e30f};
    a.max = (vec3){-1e30f, -1e30f, -1e30f};
    uint32_t have = 0;

    for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
    {
        const mesh_t *mesh = (const mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
        if (!mesh || !(mesh->flags & MESH_FLAG_HAS_AABB))
            continue;

        if (mesh->local_aabb.min.x < a.min.x)
            a.min.x = mesh->local_aabb.min.x;
        if (mesh->local_aabb.min.y < a.min.y)
            a.min.y = mesh->local_aabb.min.y;
        if (mesh->local_aabb.min.z < a.min.z)
            a.min.z = mesh->local_aabb.min.z;

        if (mesh->local_aabb.max.x > a.max.x)
            a.max.x = mesh->local_aabb.max.x;
        if (mesh->local_aabb.max.y > a.max.y)
            a.max.y = mesh->local_aabb.max.y;
        if (mesh->local_aabb.max.z > a.max.z)
            a.max.z = mesh->local_aabb.max.z;

        have = 1;
    }

    vec3 c = have ? (vec3){0.5f * (a.min.x + a.max.x), 0.5f * (a.min.y + a.max.y), 0.5f * (a.min.z + a.max.z)} : (vec3){0, 0, 0};
    float ex = have ? (0.5f * (a.max.x - a.min.x)) : 1.0f;
    float ey = have ? (0.5f * (a.max.y - a.min.y)) : 1.0f;
    float ez = have ? (0.5f * (a.max.z - a.min.z)) : 1.0f;
    float r2 = ex * ex + ey * ey + ez * ez;
    float r = (r2 > 1e-12f) ? sqrtf(r2) : 1.0f;

    g_model_bounds[idx].key = key;
    g_model_bounds[idx].c_local = c;
    g_model_bounds[idx].r_local = r;
    g_model_bounds_size++;
    return &g_model_bounds[idx];
}

static uint32_t u32_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static void u32_set_free(u32_set_t *s)
{
    if (!s)
        return;
    free(s->keys);
    s->keys = NULL;
    s->cap = 0;
    s->size = 0;
}

static void u32_set_rehash(u32_set_t *s, uint32_t new_cap)
{
    if (!s)
        return;
    if (new_cap < 16u)
        new_cap = 16u;
    new_cap = u32_next_pow2(new_cap);

    uint32_t *new_keys = (uint32_t *)calloc((size_t)new_cap, sizeof(uint32_t));
    if (!new_keys)
        return;

    uint32_t *old = s->keys;
    uint32_t old_cap = s->cap;

    s->keys = new_keys;
    s->cap = new_cap;
    s->size = 0;

    if (old && old_cap)
    {
        for (uint32_t i = 0; i < old_cap; ++i)
        {
            uint32_t k = old[i];
            if (!k)
                continue;
            uint32_t mask = s->cap - 1u;
            uint32_t idx = u32_hash(k) & mask;
            while (s->keys[idx] != 0u)
                idx = (idx + 1u) & mask;
            s->keys[idx] = k;
            s->size++;
        }
    }

    free(old);
}

static int u32_set_has(const u32_set_t *s, uint32_t k)
{
    if (!s || !s->keys || !s->cap || !k)
        return 0;
    uint32_t mask = s->cap - 1u;
    uint32_t idx = u32_hash(k) & mask;
    for (;;)
    {
        uint32_t v = s->keys[idx];
        if (v == 0u)
            return 0;
        if (v == k)
            return 1;
        idx = (idx + 1u) & mask;
    }
}

static void u32_set_add(u32_set_t *s, uint32_t k)
{
    if (!s || !k)
        return;
    if (!s->keys || !s->cap)
        u32_set_rehash(s, 64u);
    if (!s->keys || !s->cap)
        return;

    if ((s->size + 1u) * 10u >= s->cap * 7u)
        u32_set_rehash(s, s->cap * 2u);

    uint32_t mask = s->cap - 1u;
    uint32_t idx = u32_hash(k) & mask;
    while (s->keys[idx] != 0u)
    {
        if (s->keys[idx] == k)
            return;
        idx = (idx + 1u) & mask;
    }
    s->keys[idx] = k;
    s->size++;
}

static float clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

static inline render_stats_t *R_stats_write(renderer_t *r)
{
    return &r->stats[r->stats_write ? 0u : 1u];
}

static void R_stats_add_draw_arrays(renderer_t *r)
{
    render_stats_t *s = R_stats_write(r);
    s->draw_calls += 1;
}

static void R_stats_begin_frame(renderer_t *r)
{
    r->stats_write = !r->stats_write;
    memset(R_stats_write(r), 0, sizeof(render_stats_t));
}

static void R_stats_add_draw_instanced(renderer_t *r, uint32_t index_count, uint32_t instance_count)
{
    render_stats_t *s = R_stats_write(r);

    s->draw_calls += 1;
    s->instanced_draw_calls += 1;
    s->instances += (uint64_t)instance_count;

    uint64_t tris = (uint64_t)(index_count / 3u);
    uint64_t tri_total = tris * (uint64_t)instance_count;

    s->triangles += tri_total;
    s->instanced_triangles += tri_total;
}

static void R_make_black_tex(renderer_t *r)
{
    if (r->black_tex)
        return;

    glGenTextures(1, &r->black_tex);
    glBindTexture(GL_TEXTURE_2D, r->black_tex);

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

static void R_make_black_cube(renderer_t *r)
{
    if (r->black_cube)
        return;

    glGenTextures(1, &r->black_cube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, r->black_cube);

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

static void R_line3d_ensure_gpu(renderer_t *r)
{
    if (!r)
        return;

    if (!r->line3d_vao)
        glGenVertexArrays(1, &r->line3d_vao);
    if (!r->line3d_vbo)
        glGenBuffers(1, &r->line3d_vbo);

    glBindVertexArray(r->line3d_vao);

    glBindBuffer(GL_ARRAY_BUFFER, r->line3d_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(line3d_vertex_t), (const void *)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(line3d_vertex_t), (const void *)(uintptr_t)(sizeof(float) * 3u));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_line3d_ensure_vbo_capacity(renderer_t *r, uint32_t vertex_count)
{
    if (!r || !r->line3d_vbo)
        return;
    if (vertex_count <= r->line3d_vbo_cap_vertices)
        return;

    uint32_t new_cap = u32_next_pow2(vertex_count);
    if (new_cap < 256u)
        new_cap = 256u;

    glBindBuffer(GL_ARRAY_BUFFER, r->line3d_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)new_cap * sizeof(line3d_vertex_t)), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    r->line3d_vbo_cap_vertices = new_cap;
}

static void R_line3d_draw_batch(renderer_t *r,
                                shader_t *s,
                                uint32_t first_vertex,
                                uint32_t vertex_count,
                                bool depth_test,
                                bool translucent)
{
    if (!r || !s)
        return;
    if (!vertex_count)
        return;

    glLineWidth(1.0f);

    if (depth_test)
        gl_state_enable(&r->gl, GL_DEPTH_TEST);
    else
        gl_state_disable(&r->gl, GL_DEPTH_TEST);

    if (translucent)
    {
        gl_state_enable(&r->gl, GL_BLEND);
        gl_state_blend_func(&r->gl, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl_state_depth_mask(&r->gl, 0);
    }
    else
    {
        gl_state_disable(&r->gl, GL_BLEND);
        gl_state_depth_mask(&r->gl, 1);
    }

    if (!depth_test)
        gl_state_depth_mask(&r->gl, 0);

    shader_bind(s);
    R_bind_common_uniforms(r, s);

    glBindVertexArray(r->line3d_vao);
    glDrawArrays(GL_LINES, (GLint)first_vertex, (GLsizei)vertex_count);
    glBindVertexArray(0);

    R_stats_add_draw_arrays(r);
}

static void R_line3d_render(renderer_t *r)
{
    if (!r || r->lines3d.size == 0)
        return;

    shader_t *s = (r->line3d_shader_id != 0xFF) ? R_get_shader(r, r->line3d_shader_id) : NULL;
    if (!s)
        return;

    uint32_t line_count = r->lines3d.size;
    uint32_t total_vertices = line_count * 2u;

    line3d_vertex_t *verts = R_line3d_vert_scratch(total_vertices);
    if (!verts)
        return;

    enum
    {
        LINE_BATCH_DEPTH_OPAQUE = 0,
        LINE_BATCH_DEPTH_TRANSLUCENT = 1,
        LINE_BATCH_ONTOP_OPAQUE = 2,
        LINE_BATCH_ONTOP_TRANSLUCENT = 3,
        LINE_BATCH_COUNT = 4
    };

    uint32_t batch_counts[LINE_BATCH_COUNT] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < line_count; ++i)
    {
        const line3d_t *ln = (const line3d_t *)vector_at((vector_t *)&r->lines3d, i);
        if (!ln)
            continue;

        uint32_t batch = 0;
        bool on_top = (ln->flags & LINE3D_ON_TOP) != 0u;
        bool translucent = (ln->flags & LINE3D_TRANSLUCENT) != 0u;
        if (on_top && translucent)
            batch = LINE_BATCH_ONTOP_TRANSLUCENT;
        else if (on_top)
            batch = LINE_BATCH_ONTOP_OPAQUE;
        else if (translucent)
            batch = LINE_BATCH_DEPTH_TRANSLUCENT;
        else
            batch = LINE_BATCH_DEPTH_OPAQUE;

        batch_counts[batch] += 2u;
    }

    uint32_t batch_first[LINE_BATCH_COUNT] = {0, 0, 0, 0};
    uint32_t sum = 0;
    for (uint32_t b = 0; b < LINE_BATCH_COUNT; ++b)
    {
        batch_first[b] = sum;
        sum += batch_counts[b];
    }
    if (sum == 0)
        return;

    uint32_t batch_write[LINE_BATCH_COUNT];
    for (uint32_t b = 0; b < LINE_BATCH_COUNT; ++b)
        batch_write[b] = batch_first[b];

    for (uint32_t i = 0; i < line_count; ++i)
    {
        const line3d_t *ln = (const line3d_t *)vector_at((vector_t *)&r->lines3d, i);
        if (!ln)
            continue;

        uint32_t batch = 0;
        bool on_top = (ln->flags & LINE3D_ON_TOP) != 0u;
        bool translucent = (ln->flags & LINE3D_TRANSLUCENT) != 0u;
        if (on_top && translucent)
            batch = LINE_BATCH_ONTOP_TRANSLUCENT;
        else if (on_top)
            batch = LINE_BATCH_ONTOP_OPAQUE;
        else if (translucent)
            batch = LINE_BATCH_DEPTH_TRANSLUCENT;
        else
            batch = LINE_BATCH_DEPTH_OPAQUE;

        uint32_t w = batch_write[batch];
        if (w + 1u >= sum)
            continue;

        verts[w + 0u] = (line3d_vertex_t){
            {ln->a.x, ln->a.y, ln->a.z},
            {ln->color.x, ln->color.y, ln->color.z, ln->color.w}};
        verts[w + 1u] = (line3d_vertex_t){
            {ln->b.x, ln->b.y, ln->b.z},
            {ln->color.x, ln->color.y, ln->color.z, ln->color.w}};

        batch_write[batch] = w + 2u;
    }

    R_line3d_ensure_gpu(r);
    R_line3d_ensure_vbo_capacity(r, sum);

    glBindBuffer(GL_ARRAY_BUFFER, r->line3d_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)r->line3d_vbo_cap_vertices * sizeof(line3d_vertex_t)),
                 NULL,
                 GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)sum * sizeof(line3d_vertex_t)), verts);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_func(&r->gl, GL_LEQUAL);

    R_line3d_draw_batch(r, s, batch_first[LINE_BATCH_DEPTH_OPAQUE], batch_counts[LINE_BATCH_DEPTH_OPAQUE], true, false);
    R_line3d_draw_batch(r, s, batch_first[LINE_BATCH_DEPTH_TRANSLUCENT], batch_counts[LINE_BATCH_DEPTH_TRANSLUCENT], true, true);
    R_line3d_draw_batch(r, s, batch_first[LINE_BATCH_ONTOP_OPAQUE], batch_counts[LINE_BATCH_ONTOP_OPAQUE], false, false);
    R_line3d_draw_batch(r, s, batch_first[LINE_BATCH_ONTOP_TRANSLUCENT], batch_counts[LINE_BATCH_ONTOP_TRANSLUCENT], false, true);

    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
    gl_state_depth_mask(&r->gl, 1);
}

static void R_user_cfg_pull_from_cvars(renderer_t *r)
{
    r->cfg.bloom = cvar_get_bool_name("cl_bloom");
    r->cfg.shadows = cvar_get_bool_name("cl_r_shadows");
    r->cfg.msaa = cvar_get_bool_name("cl_msaa_enabled");
    r->cfg.msaa_samples = cvar_get_int_name("cl_msaa_samples");
    if (r->cfg.msaa_samples < 1)
        r->cfg.msaa_samples = 1;
    if (r->cfg.msaa_samples > 16)
        r->cfg.msaa_samples = 16;

    r->cfg.wireframe = cvar_get_bool_name("cl_r_wireframe");
    r->cfg.debug_mode = cvar_get_int_name("cl_render_debug");
}

static int R_gpu_timers_supported(void)
{
#if defined(__APPLE__)
    return 0;
#else
    return (GLEW_ARB_timer_query || GLEW_VERSION_3_3) ? 1 : 0;
#endif
}

static void R_gpu_timer_begin(renderer_t *r, render_gpu_phase_t phase)
{
    if (!r || !R_gpu_timers_supported())
        return;
    if (phase < 0 || phase >= R_GPU_PHASE_COUNT)
        return;
    uint32_t idx = r->gpu_query_index & 15u;
    GLuint q = r->gpu_queries[phase][idx];
    if (!q)
        return;
    glBeginQuery(GL_TIME_ELAPSED, q);
    r->gpu_timer_active = 1;
}

static void R_gpu_timer_end(renderer_t *r)
{
    if (!r || !R_gpu_timers_supported())
        return;
    if (!r->gpu_timer_active)
        return;
    glEndQuery(GL_TIME_ELAPSED);
    r->gpu_timer_active = 0;
}

static void R_gpu_timers_resolve(renderer_t *r)
{
    if (!r || !R_gpu_timers_supported())
        return;

    for (int p = 0; p < (int)R_GPU_PHASE_COUNT; ++p)
    {
        for (uint32_t i = 1; i < 16u; ++i)
        {
            uint32_t idx = (r->gpu_query_index - i) & 15u;
            GLuint q = r->gpu_queries[p][idx];
            if (!q)
                continue;

            GLint available = 0;
            glGetQueryObjectiv(q, GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                continue;

            GLuint64 ns = 0;
            glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
            r->gpu_timings.ms[p] = (double)ns * 1e-6;
            r->gpu_timings.valid = 1;
            break;
        }
    }
}

static double R_time_now_ms(void)
{
#if defined(_WIN32)
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
#endif
}

static int R_mip_count_2d(int w, int h);
static uint32_t R_scene_draw_fbo(const renderer_t *r);
static void R_msaa_resolve(renderer_t *r, GLbitfield mask);

static void R_exposure_reduce_ensure(renderer_t *r, uint32_t need_vec4);
static int R_exposure_reduce_avg_color(renderer_t *r, vec3 *out_avg);

typedef struct R_per_frame_ubo_t
{
    mat4 u_View;
    mat4 u_Proj;
    vec4 u_CameraPos; // xyz, w unused
    int32_t u_HeightInvert;
    int32_t u_ManualSRGB;
    int32_t u_ShadowEnabled;
    int32_t u_ShadowCascadeCount;
    int32_t u_ShadowMapSize;
    int32_t u_ShadowLightIndex;
    int32_t u_ShadowPCF;
    int32_t _pad0;
    vec4 u_ShadowSplits;
    float u_ShadowBias;
    float u_ShadowNormalBias;
    float u_IBLIntensity;
    float _pad1;
    mat4 u_ShadowVP[4];
} R_per_frame_ubo_t;

static void R_per_frame_ubo_ensure(renderer_t *r)
{
    if (!r)
        return;
    if (r->per_frame_ubo == 0)
        glGenBuffers(1, &r->per_frame_ubo);
    if (r->per_frame_ubo == 0)
        return;

    glBindBuffer(GL_UNIFORM_BUFFER, r->per_frame_ubo);
    if (!r->per_frame_ubo_valid)
        glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(R_per_frame_ubo_t), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    r->per_frame_ubo_valid = 1;
}

static void R_per_frame_ubo_update(renderer_t *r)
{
    if (!r)
        return;
    R_per_frame_ubo_ensure(r);
    if (!r->per_frame_ubo)
        return;

    R_per_frame_ubo_t u;
    memset(&u, 0, sizeof(u));

    u.u_View = r->camera.view;
    u.u_Proj = r->camera.proj;
    u.u_CameraPos = (vec4){r->camera.position.x, r->camera.position.y, r->camera.position.z, 1.0f};

    u.u_HeightInvert = r->scene.height_invert ? 1 : 0;
    u.u_ManualSRGB = r->scene.manual_srgb ? 1 : 0;

    u.u_ShadowEnabled = (r->cfg.shadows && r->shadow.tex && r->shadow.light_index >= 0) ? 1 : 0;
    u.u_ShadowCascadeCount = r->shadow.cascades;
    u.u_ShadowMapSize = r->shadow.size;
    u.u_ShadowLightIndex = r->shadow.light_index;
    u.u_ShadowPCF = r->scene.shadow_pcf ? 1 : 0;
    u.u_ShadowSplits = (vec4){r->shadow.splits[0], r->shadow.splits[1], r->shadow.splits[2], r->shadow.splits[3]};
    u.u_ShadowBias = r->scene.shadow_bias;
    u.u_ShadowNormalBias = r->scene.shadow_normal_bias;
    u.u_IBLIntensity = r->scene.ibl_intensity;
    for (int i = 0; i < 4; ++i)
        u.u_ShadowVP[i] = r->shadow.vp[i];

    glBindBuffer(GL_UNIFORM_BUFFER, r->per_frame_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)sizeof(u), &u);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static void R_per_frame_ubo_bind(renderer_t *r)
{
    if (!r || !r->per_frame_ubo)
        return;
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->per_frame_ubo);
}

static float R_luminance(vec3 c)
{
    return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f;
}

static uint32_t R_scene_draw_fbo(const renderer_t *r)
{
    if (!r)
        return 0;
    if (r->msaa_fbo && r->cfg.msaa && r->msaa_samples > 1)
        return r->msaa_fbo;
    return r->light_fbo;
}

static void R_msaa_resolve(renderer_t *r, GLbitfield mask)
{
    if (!r)
        return;
    if (!r->msaa_fbo || !r->light_fbo)
        return;
    if (!r->cfg.msaa || r->msaa_samples <= 1)
        return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->msaa_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->light_fbo);

    int w = r->fb_size.x;
    int h = r->fb_size.y;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    GLbitfield m = mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m == 0)
        return;

    glBlitFramebuffer(0, 0, w, h,
                      0, 0, w, h,
                      m, GL_NEAREST);
}

static void R_exposure_reduce_ensure(renderer_t *r, uint32_t need_vec4)
{
    if (!r)
        return;

    if (need_vec4 < 1u)
        need_vec4 = 1u;

    if (r->exposure_reduce_ssbo[0] == 0 && r->exposure_reduce_ssbo[1] == 0)
        glGenBuffers(2, r->exposure_reduce_ssbo);

    if (r->exposure_reduce_cap_vec4 >= need_vec4)
        return;

    uint32_t new_cap = u32_next_pow2(need_vec4);
    if (new_cap < 256u)
        new_cap = 256u;

    GLsizeiptr bytes = (GLsizeiptr)((size_t)new_cap * sizeof(float) * 4u);
    for (int i = 0; i < 2; ++i)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->exposure_reduce_ssbo[i]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, NULL, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    r->exposure_reduce_cap_vec4 = new_cap;
}

static int R_exposure_reduce_avg_color(renderer_t *r, vec3 *out_avg)
{
    if (!r || !out_avg)
        return 0;
    if (!r->light_color_tex)
        return 0;
    if (r->exposure_reduce_tex_shader_id == 0xFF || r->exposure_reduce_buf_shader_id == 0xFF)
        return 0;

    shader_t *s_tex = R_get_shader(r, r->exposure_reduce_tex_shader_id);
    shader_t *s_buf = R_get_shader(r, r->exposure_reduce_buf_shader_id);
    if (!s_tex || !s_buf)
        return 0;

    int w = r->fb_size.x;
    int h = r->fb_size.y;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    uint32_t gx = (uint32_t)((w + 15) / 16);
    uint32_t gy = (uint32_t)((h + 15) / 16);
    uint32_t n = gx * gy;
    if (!n)
        n = 1u;

    R_exposure_reduce_ensure(r, n);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, r->exposure_reduce_ssbo[0]);

    shader_bind(s_tex);
    shader_set_int(s_tex, "u_Width", w);
    shader_set_int(s_tex, "u_Height", h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->light_color_tex);
    shader_set_int(s_tex, "u_Src", 0);

    shader_dispatch_compute(s_tex, gx, gy, 1);
    shader_memory_barrier(GL_SHADER_STORAGE_BARRIER_BIT);

    int ping = 0;
    while (n > 1u)
    {
        uint32_t groups = (n + 255u) / 256u;
        if (groups < 1u)
            groups = 1u;

        R_exposure_reduce_ensure(r, groups);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, r->exposure_reduce_ssbo[ping]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, r->exposure_reduce_ssbo[ping ^ 1]);

        shader_bind(s_buf);
        shader_set_int(s_buf, "u_Count", (int)n);
        shader_dispatch_compute(s_buf, groups, 1, 1);
        shader_memory_barrier(GL_SHADER_STORAGE_BARRIER_BIT);

        ping ^= 1;
        n = groups;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->exposure_reduce_ssbo[ping]);
    float *v = (float *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(sizeof(float) * 4u), GL_MAP_READ_BIT);
    if (!v)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return 0;
    }

    float sum_r = v[0];
    float sum_g = v[1];
    float sum_b = v[2];
    float count = v[3];

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    if (count < 1.0f)
        count = 1.0f;

    out_avg->x = sum_r / count;
    out_avg->y = sum_g / count;
    out_avg->z = sum_b / count;
    return 1;
}

static void R_auto_exposure_update(renderer_t *r)
{
    if (!r)
        return;

    if (!r->scene.exposure_auto)
    {
        r->exposure_adapted = r->scene.exposure;
        r->exposure_adapted_valid = true;
        return;
    }

    if (!r->light_color_tex)
        return;

    float dt = r->scene.delta_time;
    if (dt < 0.0f)
        dt = 0.0f;

    int hz = cvar_get_int_name("cl_auto_exposure_hz");
    if (hz < 1)
        hz = 1;
    if (hz > 240)
        hz = 240;

    float period = 1.0f / (float)hz;
    r->exposure_readback_accum += dt;
    if (r->exposure_readback_accum < period)
        return;
    r->exposure_readback_accum = 0.0f;

    vec3 avg = (vec3){0, 0, 0};
    if (!R_exposure_reduce_avg_color(r, &avg))
        return;

    float avgLum = R_luminance(avg);
    if (avgLum < 1e-6f)
        avgLum = 1e-6f;

    float targetExposure = 0.18f / avgLum;
    float exMin = r->scene.auto_exposure_min;
    float exMax = r->scene.auto_exposure_max;
    if (exMin < 0.0f)
        exMin = 0.0f;
    if (exMax < exMin)
        exMax = exMin;
    if (targetExposure < exMin)
        targetExposure = exMin;
    if (targetExposure > exMax)
        targetExposure = exMax;

    float speed = r->scene.auto_exposure_speed;
    if (speed < 0.0f)
        speed = 0.0f;

    float cur = r->exposure_adapted_valid ? r->exposure_adapted : r->scene.exposure;
    if (cur < 0.0f)
        cur = 0.0f;

    float k = 1.0f - expf(-speed * dt);
    r->exposure_adapted = cur + (targetExposure - cur) * k;
    r->exposure_adapted_valid = true;
}

static void R_shadow_delete(renderer_t *r)
{
    if (!r)
        return;

    if (r->shadow.fbo)
        glDeleteFramebuffers(1, &r->shadow.fbo);
    r->shadow.fbo = 0;

    if (r->shadow.tex)
        glDeleteTextures(1, &r->shadow.tex);
    r->shadow.tex = 0;

    r->shadow.cascades = 0;
    r->shadow.size = 0;
    r->shadow.light_index = -1;
    memset(r->shadow.vp, 0, sizeof(r->shadow.vp));
    memset(r->shadow.splits, 0, sizeof(r->shadow.splits));
}

static int R_shadow_clamp_cascades(int c)
{
    if (c < 1)
        c = 1;
    if (c > R_SHADOW_MAX_CASCADES)
        c = R_SHADOW_MAX_CASCADES;
    return c;
}

static int R_shadow_clamp_size(int s)
{
    if (s < 256)
        s = 256;
    if (s > 8192)
        s = 8192;
    return s;
}

static void R_shadow_ensure(renderer_t *r)
{
    if (!r)
        return;

    if (!r->cfg.shadows)
    {
        if (r->shadow.tex || r->shadow.fbo)
            R_shadow_delete(r);
        return;
    }

    int want_size = R_shadow_clamp_size(r->scene.shadow_map_size);
    int want_cascades = R_shadow_clamp_cascades(r->scene.shadow_cascades);

    if (r->shadow.tex && r->shadow.fbo && r->shadow.size == want_size && r->shadow.cascades == want_cascades)
        return;

    R_shadow_delete(r);

    glGenFramebuffers(1, &r->shadow.fbo);

    glGenTextures(1, &r->shadow.tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, r->shadow.tex);

    glTexImage3D(GL_TEXTURE_2D_ARRAY,
                 0,
                 GL_DEPTH_COMPONENT32F,
                 want_size,
                 want_size,
                 want_cascades,
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 NULL);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, r->scene.shadow_pcf ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, r->scene.shadow_pcf ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    {
        float bc[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bc);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    r->shadow.size = want_size;
    r->shadow.cascades = want_cascades;
    r->shadow.light_index = -1;
}

static void R_on_cvar_any(renderer_t *r)
{
    bool old_msaa = r->cfg.msaa;
    int old_msaa_samples = r->cfg.msaa_samples;

    R_user_cfg_pull_from_cvars(r);

    if (r->cfg.msaa != old_msaa || r->cfg.msaa_samples != old_msaa_samples)
    {
        r->fb_size_last = (vec2i){0, 0};
        r->exposure_adapted_valid = false;
        R_update_resize(r);
    }
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

static void R_on_wireframe_change(sv_cvar_key_t key, const void *old_state, const void *state)
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
}

static int R_mip_count_2d(int w, int h)
{
    int m = 1;
    int s = (w > h) ? w : h;
    while (s > 1)
    {
        s >>= 1;
        ++m;
    }
    return m;
}

static void R_alloc_tex2d_rgba16f_mipped(uint32_t *tex, int w, int h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    int levels = R_mip_count_2d(w, h);

    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);

    glTexStorage2D(GL_TEXTURE_2D, levels, GL_RGBA16F, w, h);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
}

static void R_gl_delete_targets(renderer_t *r)
{
    if (r->gbuf_fbo)
        glDeleteFramebuffers(1, &r->gbuf_fbo);
    if (r->light_fbo)
        glDeleteFramebuffers(1, &r->light_fbo);
    if (r->final_fbo)
        glDeleteFramebuffers(1, &r->final_fbo);
    if (r->msaa_fbo)
        glDeleteFramebuffers(1, &r->msaa_fbo);

    if (r->msaa_color_rb)
        glDeleteRenderbuffers(1, &r->msaa_color_rb);
    if (r->msaa_depth_rb)
        glDeleteRenderbuffers(1, &r->msaa_depth_rb);

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
    r->msaa_fbo = 0;
    r->msaa_color_rb = 0;
    r->msaa_depth_rb = 0;

    r->gbuf_albedo = 0;
    r->gbuf_normal = 0;
    r->gbuf_material = 0;
    r->gbuf_depth = 0;
    r->gbuf_emissive = 0;

    r->light_color_tex = 0;
    r->final_color_tex = 0;
}

static void R_fp_delete_buffers(renderer_t *r)
{
    if (r->fp.lights_ssbo)
        glDeleteBuffers(1, &r->fp.lights_ssbo);
    if (r->fp.tile_index_ssbo)
        glDeleteBuffers(1, &r->fp.tile_index_ssbo);
    if (r->fp.tile_list_ssbo)
        glDeleteBuffers(1, &r->fp.tile_list_ssbo);
    if (r->fp.tile_depth_ssbo)
        glDeleteBuffers(1, &r->fp.tile_depth_ssbo);

    r->fp.lights_ssbo = 0;
    r->fp.tile_index_ssbo = 0;
    r->fp.tile_list_ssbo = 0;
    r->fp.tile_depth_ssbo = 0;

    r->fp.lights_cap = 0;
    r->fp.tile_max = 1u;

    r->fp.tile_count_x = 1;
    r->fp.tile_count_y = 1;
    r->fp.tiles = 1;
}

static void R_fp_ensure_lights_capacity(renderer_t *r, uint32_t needed)
{
    if (needed < 1u)
        needed = 1u;

    if (needed <= r->fp.lights_cap && r->fp.lights_ssbo)
        return;

    uint32_t new_cap = u32_next_pow2(needed);
    if (new_cap < 256u)
        new_cap = 256u;

    if (!r->fp.lights_ssbo)
        glGenBuffers(1, &r->fp.lights_ssbo);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->fp.lights_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(gpu_light_t) * (size_t)new_cap), 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    r->fp.lights_cap = new_cap;
}

static uint32_t R_fp_pick_tile_max(uint32_t light_count)
{
    if (light_count < 1u)
        light_count = 1u;
    uint32_t t = u32_next_pow2(light_count);
    if (t > 128u)
        t = 128u;
    return t;
}

static void R_fp_resize_tile_buffers(renderer_t *r, vec2i fb, uint32_t light_count)
{
    if (fb.x < 1)
        fb.x = 1;
    if (fb.y < 1)
        fb.y = 1;

    int new_tc_x = (fb.x + (FP_TILE_SIZE - 1)) / FP_TILE_SIZE;
    int new_tc_y = (fb.y + (FP_TILE_SIZE - 1)) / FP_TILE_SIZE;
    int new_tiles = new_tc_x * new_tc_y;
    if (new_tiles < 1)
        new_tiles = 1;

    uint32_t new_tile_max = R_fp_pick_tile_max(light_count);

    int need_realloc = 0;
    if (!r->fp.tile_index_ssbo || !r->fp.tile_list_ssbo || !r->fp.tile_depth_ssbo)
        need_realloc = 1;
    if (new_tiles != r->fp.tiles)
        need_realloc = 1;
    if (new_tile_max != r->fp.tile_max)
        need_realloc = 1;

    r->fp.tile_count_x = new_tc_x;
    r->fp.tile_count_y = new_tc_y;
    r->fp.tiles = new_tiles;
    r->fp.tile_max = new_tile_max;

    if (need_realloc)
    {
        if (!r->fp.tile_index_ssbo)
            glGenBuffers(1, &r->fp.tile_index_ssbo);
        if (!r->fp.tile_list_ssbo)
            glGenBuffers(1, &r->fp.tile_list_ssbo);
        if (!r->fp.tile_depth_ssbo)
            glGenBuffers(1, &r->fp.tile_depth_ssbo);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->fp.tile_index_ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * 2u * (size_t)r->fp.tiles), 0, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->fp.tile_list_ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * (size_t)r->fp.tiles * (size_t)r->fp.tile_max), 0, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->fp.tile_depth_ssbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(sizeof(float) * 2u * (size_t)r->fp.tiles), 0, GL_DYNAMIC_DRAW);

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

    R_alloc_tex2d_rgba16f_mipped(&r->light_color_tex, r->fb_size.x, r->fb_size.y);
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

    r->msaa_fbo = 0;
    r->msaa_color_rb = 0;
    r->msaa_depth_rb = 0;
    r->msaa_samples = 0;

    if (r->cfg.msaa && r->cfg.msaa_samples > 1)
    {
        GLint max_samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        int samples = r->cfg.msaa_samples;
        if (max_samples > 0 && samples > max_samples)
            samples = max_samples;
        if (samples < 2)
            samples = 2;

        glGenFramebuffers(1, &r->msaa_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, r->msaa_fbo);

        glGenRenderbuffers(1, &r->msaa_color_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, r->msaa_color_rb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, r->fb_size.x, r->fb_size.y);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, r->msaa_color_rb);

        glGenRenderbuffers(1, &r->msaa_depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, r->msaa_depth_rb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT32F, r->fb_size.x, r->fb_size.y);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->msaa_depth_rb);

        {
            uint32_t bufs[] = {GL_COLOR_ATTACHMENT0};
            glDrawBuffers(1, (const GLenum *)bufs);
        }

        {
            uint32_t status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
                LOG_ERROR("MSAA FBO incomplete: 0x%x", (unsigned)status);
        }

        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        r->msaa_samples = samples;
    }

    R_fp_resize_tile_buffers(r, r->fb_size, 1u);
    R_fp_ensure_lights_capacity(r, 1u);
}

static uint32_t R_resolve_image_gl(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return 0;
    if (!ihandle_is_valid(h))
        return 0;

    asset_manager_touch(r->assets, h);

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return 0;
    if (a->type != ASSET_IMAGE)
        return 0;
    if (a->state != ASSET_STATE_READY)
        return 0;

    return a->as.image.gl_handle;
}

static int R_resolve_image_has_alpha(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return 0;
    if (!ihandle_is_valid(h))
        return 0;

    asset_manager_touch(r->assets, h);

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return 0;
    if (a->type != ASSET_IMAGE)
        return 0;
    if (a->state != ASSET_STATE_READY)
        return 0;

    return a->as.image.has_alpha ? 1 : 0;
}

static int R_resolve_image_has_smooth_alpha(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return 0;
    if (!ihandle_is_valid(h))
        return 0;

    asset_manager_touch(r->assets, h);

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return 0;
    if (a->type != ASSET_IMAGE)
        return 0;
    if (a->state != ASSET_STATE_READY)
        return 0;

    return a->as.image.has_smooth_alpha ? 1 : 0;
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

static void R_bind_image_slot_mask(renderer_t *r, const shader_t *s, const char *sampler_name, int unit, ihandle_t h, uint32_t bit, uint32_t *mask)
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
        glBindTexture(GL_TEXTURE_2D, r->black_tex);
    }

    shader_set_int(s, sampler_name, unit);
}

static void R_draw_fs_tri(renderer_t *r)
{
    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void R_apply_material_or_default(renderer_t *r, const shader_t *s, asset_material_t *mat)
{
    shader_set_int(s, "u_HasMaterial", mat ? 1 : 0);

    if (mat)
    {
        int alpha_test = (mat->flags & MAT_FLAG_ALPHA_CUTOUT) ? 1 : 0;
        float alpha_cutoff = mat->alpha_cutoff;
        if (alpha_cutoff < 0.0f)
            alpha_cutoff = 0.0f;
        if (alpha_cutoff > 1.0f)
            alpha_cutoff = 1.0f;

        shader_set_int(s, "u_AlphaTest", alpha_test);
        shader_set_float(s, "u_AlphaCutoff", alpha_cutoff);

        shader_set_int(s, "u_MaterialFlags", (int)mat->flags);
        shader_set_int(s, "u_MatDoubleSided", (mat->flags & MAT_FLAG_DOUBLE_SIDED) ? 1 : 0);
        shader_set_int(s, "u_MatAlphaCutout", (mat->flags & MAT_FLAG_ALPHA_CUTOUT) ? 1 : 0);
        shader_set_int(s, "u_MatAlphaBlend", (mat->flags & MAT_FLAG_ALPHA_BLEND) ? 1 : 0);

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

        shader_set_int(s, "u_AlphaTest", 0);
        shader_set_float(s, "u_AlphaCutoff", 0.0f);

        shader_set_int(s, "u_MaterialFlags", 0);
        shader_set_int(s, "u_MatDoubleSided", 0);
        shader_set_int(s, "u_MatAlphaCutout", 0);
        shader_set_int(s, "u_MatAlphaBlend", 0);

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

static void R_bind_common_uniforms(renderer_t *r, const shader_t *s)
{
    shader_set_mat4(s, "u_View", r->camera.view);
    shader_set_mat4(s, "u_Proj", r->camera.proj);
    shader_set_vec3(s, "u_CameraPos", r->camera.position);

    shader_set_int(s, "u_HeightInvert", r->scene.height_invert ? 1 : 0);
    shader_set_int(s, "u_ManualSRGB", r->scene.manual_srgb ? 1 : 0);
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

typedef struct frustum_plane_t
{
    float a, b, c, d;
} frustum_plane_t;

typedef struct frustum_t
{
    frustum_plane_t p[6];
} frustum_t;

static void R_plane_normalize(frustum_plane_t *pl)
{
    float len2 = pl->a * pl->a + pl->b * pl->b + pl->c * pl->c;
    if (len2 < 1e-20f)
        return;
    float inv = 1.0f / sqrtf(len2);
    pl->a *= inv;
    pl->b *= inv;
    pl->c *= inv;
    pl->d *= inv;
}

static void R_frustum_build(frustum_t *f, const renderer_t *r)
{
    mat4 vp = mat4_mul(r->camera.proj, r->camera.view);

    float r0x = vp.m[0], r0y = vp.m[4], r0z = vp.m[8], r0w = vp.m[12];
    float r1x = vp.m[1], r1y = vp.m[5], r1z = vp.m[9], r1w = vp.m[13];
    float r2x = vp.m[2], r2y = vp.m[6], r2z = vp.m[10], r2w = vp.m[14];
    float r3x = vp.m[3], r3y = vp.m[7], r3z = vp.m[11], r3w = vp.m[15];

    f->p[0] = (frustum_plane_t){r3x + r0x, r3y + r0y, r3z + r0z, r3w + r0w};
    f->p[1] = (frustum_plane_t){r3x - r0x, r3y - r0y, r3z - r0z, r3w - r0w};
    f->p[2] = (frustum_plane_t){r3x + r1x, r3y + r1y, r3z + r1z, r3w + r1w};
    f->p[3] = (frustum_plane_t){r3x - r1x, r3y - r1y, r3z - r1z, r3w - r1w};
    f->p[4] = (frustum_plane_t){r3x + r2x, r3y + r2y, r3z + r2z, r3w + r2w};
    f->p[5] = (frustum_plane_t){r3x - r2x, r3y - r2y, r3z - r2z, r3w - r2w};

    for (int i = 0; i < 6; ++i)
        R_plane_normalize(&f->p[i]);
}

static int R_frustum_sphere_visible(const frustum_t *f, vec3 c, float r)
{
    for (int i = 0; i < 6; ++i)
    {
        const frustum_plane_t *p = &f->p[i];
        float d = p->a * c.x + p->b * c.y + p->c * c.z + p->d;
        if (d < -r)
            return 0;
    }
    return 1;
}

static int R_mesh_visible_frustum(const frustum_t *f, const mesh_t *mesh, const mat4 *model_mtx, float max_scale)
{
    if (!mesh || !model_mtx)
        return 1;
    if (!(mesh->flags & MESH_FLAG_HAS_AABB))
        return 1;

    vec3 lc = R_mesh_local_center(mesh);
    vec3 wc = R_transform_point(*model_mtx, lc);

    float radius_world = R_mesh_local_radius(mesh) * max_scale;
    if (radius_world < 1e-6f)
        radius_world = 1e-6f;

    return R_frustum_sphere_visible(f, wc, radius_world);
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

static uint32_t R_pick_lod_level_for_mesh_fade01(const renderer_t *r, const mesh_t *mesh, const mat4 *model_mtx, float max_scale, ihandle_t model_h, uint32_t mesh_index, float *out_fade01, int *out_xfade_01)
{
    if (out_fade01)
        *out_fade01 = 0.0f;
    if (out_xfade_01)
        *out_xfade_01 = 0;

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

    float dist2 = 0.0f;

    {
        vec3 lc = R_mesh_local_center(mesh);
        vec3 wc = R_transform_point(*model_mtx, lc);

        float dx = wc.x - r->camera.position.x;
        float dy = wc.y - r->camera.position.y;
        float dz = wc.z - r->camera.position.z;

        dist2 = dx * dx + dy * dy + dz * dz;
        if (dist2 < 1e-6f)
            dist2 = 1e-6f;
    }

    float proj_fy = r->camera.proj.m[5];
    if (fabsf(proj_fy) < 1e-6f)
        proj_fy = 1.0f;

    float radius_world = R_mesh_local_radius(mesh) * max_scale;
    float k = (float)r->fb_size.y * proj_fy * radius_world; // 2*0.5*H*proj_fy*radius_world
    if (k < 1e-6f)
        k = 1e-6f;

    float t1 = 120.0f;
    float band = 0.18f;
    float hi = t1 * (1.0f + band);
    float lo = t1 * (1.0f - band);

    // diameter_px = k / dist, so diameter between (lo,hi) => dist between (k/hi, k/lo)
    float dhi = k / hi;
    float dlo = k / lo;
    float dhi2 = dhi * dhi;
    float dlo2 = dlo * dlo;

    if (dist2 > dhi2 && dist2 < dlo2 && lods >= 2)
    {
        float dist = sqrtf(dist2);
        float diameter_px = k / dist;
        float f = (hi - diameter_px) / (hi - lo);
        f = clamp01(f);
        if (out_fade01)
            *out_fade01 = f;
        if (out_xfade_01)
            *out_xfade_01 = 1;
        return 0;
    }

    uint32_t lod = 0;
    // diameter_px < t  <=> dist2 > (k/t)^2
    float d1 = k / t1;
    if (dist2 > d1 * d1)
        lod = 1;
    float d2 = k / 80.0f;
    if (dist2 > d2 * d2)
        lod = 2;
    float d3 = k / 40.0f;
    if (dist2 > d3 * d3)
        lod = 3;
    float d4 = k / 20.0f;
    if (dist2 > d4 * d4)
        lod = 4;
    float d5 = k / 10.0f;
    if (dist2 > d5 * d5)
        lod = 5;

    if (lod >= lods)
        lod = lods - 1u;

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
    r->inst_mats = create_vector(instance_gpu_t);

    r->shadow_inst_batches = create_vector(inst_batch_t);
    r->shadow_inst_mats = create_vector(instance_gpu_t);

    glGenBuffers(1, &r->instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);

    r->instance_cap = 1024u;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(instance_gpu_t) * (size_t)r->instance_cap), 0, GL_STREAM_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_instance_stream_shutdown(renderer_t *r)
{
    if (r->instance_vbo)
        glDeleteBuffers(1, &r->instance_vbo);
    r->instance_vbo = 0;

    r->instance_cap = 0;

    vector_free(&r->inst_batches);
    vector_free(&r->fwd_inst_batches);
    vector_free(&r->inst_mats);

    vector_free(&r->shadow_inst_batches);
    vector_free(&r->shadow_inst_mats);
}

static void R_upload_instances(renderer_t *r, const instance_gpu_t *inst, uint32_t count)
{
    if (!count)
        return;

    if (count > r->instance_cap)
    {
        r->instance_cap = u32_next_pow2(count);
        glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(instance_gpu_t) * (size_t)r->instance_cap), 0, GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(instance_gpu_t) * (size_t)count), inst);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_upload_instances_full(renderer_t *r, const instance_gpu_t *inst, uint32_t count)
{
    if (!r || !inst || !count)
        return;

    if (count > r->instance_cap)
    {
        r->instance_cap = u32_next_pow2(count);
        glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(instance_gpu_t) * (size_t)r->instance_cap), 0, GL_STREAM_DRAW);
    }
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(instance_gpu_t) * (size_t)count), inst);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_mesh_ensure_instance_attribs(renderer_t *r, uint32_t vao)
{
    if (!r || !vao)
        return;
    if (u32_set_has(&g_instanced_vao_set, vao))
        return;

    GLsizei stride = (GLsizei)sizeof(instance_gpu_t);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->instance_vbo);

    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
    glEnableVertexAttribArray(7);
    glEnableVertexAttribArray(8);

    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void *)(uintptr_t)(sizeof(float) * 0));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void *)(uintptr_t)(sizeof(float) * 4));
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void *)(uintptr_t)(sizeof(float) * 8));
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, (void *)(uintptr_t)(sizeof(float) * 12));
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride, (void *)(uintptr_t)(sizeof(mat4)));

    glVertexAttribDivisor(4, 1);
    glVertexAttribDivisor(5, 1);
    glVertexAttribDivisor(6, 1);
    glVertexAttribDivisor(7, 1);
    glVertexAttribDivisor(8, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    u32_set_add(&g_instanced_vao_set, vao);
}

static int R_resolve_batch_resources(renderer_t *r,
                                     const inst_batch_t *b,
                                     asset_model_t **out_mdl,
                                     mesh_t **out_mesh,
                                     const mesh_lod_t **out_lod,
                                     asset_material_t **out_mat)
{
    if (!b || !ihandle_is_valid(b->model))
        return 0;

    asset_model_t *mdl = R_resolve_model(r, b->model);
    if (!mdl)
        return 0;

    if (b->mesh_index >= mdl->meshes.size)
        return 0;

    mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, b->mesh_index);
    if (!mesh)
        return 0;

    const mesh_lod_t *lod = R_mesh_get_lod(mesh, b->lod);
    if (!lod || !lod->vao || !lod->index_count)
        return 0;

    if (out_mdl)
        *out_mdl = mdl;
    if (out_mesh)
        *out_mesh = mesh;
    if (out_lod)
        *out_lod = lod;
    if (out_mat)
        *out_mat = R_resolve_material(r, mesh->material);
    return 1;
}

static void R_material_state(const renderer_t *r,
                             asset_material_t *mat,
                             int *out_cutout,
                             int *out_blend,
                             int *out_doublesided,
                             float *out_alpha_cutoff,
                             uint32_t *out_albedo_tex)
{
    int cutout = 0;
    int blend = 0;
    int double_sided = 0;
    float alpha_cutoff = 0.0f;
    uint32_t albedo_tex = 0;

    if (mat)
    {
        cutout = (mat->flags & MAT_FLAG_ALPHA_CUTOUT) ? 1 : 0;
        blend = (mat->flags & MAT_FLAG_ALPHA_BLEND) ? 1 : 0;
        double_sided = (mat->flags & MAT_FLAG_DOUBLE_SIDED) ? 1 : 0;

        if (cutout)
        {
            alpha_cutoff = mat->alpha_cutoff;
            if (alpha_cutoff < 0.0f)
                alpha_cutoff = 0.0f;
            if (alpha_cutoff > 1.0f)
                alpha_cutoff = 1.0f;
        }

        albedo_tex = R_resolve_image_gl(r, mat->albedo_tex);

        if (blend)
        {
            int tex_has_alpha = R_resolve_image_has_alpha(r, mat->albedo_tex);
            int tex_has_smooth_alpha = R_resolve_image_has_smooth_alpha(r, mat->albedo_tex);
            int opacity_blend = (mat->opacity < 0.999f) ? 1 : 0;
            blend = (opacity_blend || tex_has_alpha) ? 1 : 0;

            if (!cutout && blend && !opacity_blend && tex_has_alpha && !tex_has_smooth_alpha)
            {
                cutout = 1;
                blend = 0;
                alpha_cutoff = 0.5f;
            }
        }
    }

    if (out_cutout)
        *out_cutout = cutout;
    if (out_blend)
        *out_blend = blend;
    if (out_doublesided)
        *out_doublesided = double_sided;
    if (out_alpha_cutoff)
        *out_alpha_cutoff = alpha_cutoff;
    if (out_albedo_tex)
        *out_albedo_tex = albedo_tex;
}

static float R_batch_view_z(const renderer_t *r, const inst_batch_t *b, const mesh_t *mesh)
{
    if (!r || !b || b->count == 0)
        return 0.0f;
    instance_gpu_t *inst = (instance_gpu_t *)vector_at((vector_t *)&r->inst_mats, b->start);
    if (!inst)
        return 0.0f;

    vec3 local_center = (vec3){0.0f, 0.0f, 0.0f};
    if (mesh && (mesh->flags & MESH_FLAG_HAS_AABB))
    {
        local_center.x = (mesh->local_aabb.min.x + mesh->local_aabb.max.x) * 0.5f;
        local_center.y = (mesh->local_aabb.min.y + mesh->local_aabb.max.y) * 0.5f;
        local_center.z = (mesh->local_aabb.min.z + mesh->local_aabb.max.z) * 0.5f;
    }

    vec4 wp = mat4_mul_vec4(inst->m, (vec4){local_center.x, local_center.y, local_center.z, 1.0f});
    vec4 vp = mat4_mul_vec4(r->camera.view, wp);
    return vp.z;
}

static int R_inst_item_sort(const void *a, const void *b)
{
    const inst_item_t *ia = (const inst_item_t *)a;
    const inst_item_t *ib = (const inst_item_t *)b;

    if (ia->mat_blend < ib->mat_blend)
        return -1;
    if (ia->mat_blend > ib->mat_blend)
        return 1;

    if (ia->tex_key < ib->tex_key)
        return -1;
    if (ia->tex_key > ib->tex_key)
        return 1;

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

static int R_inst_item_is_alpha_blend(const inst_item_t *it)
{
    return it ? (it->mat_blend ? 1 : 0) : 0;
}

static void R_emit_batches_from_items(renderer_t *r, inst_item_t *items, uint32_t n, vector_t *batches, vector_t *mats)
{
    if (!n)
        return;

    qsort(items, (size_t)n, sizeof(inst_item_t), R_inst_item_sort);

    uint32_t cur_start = 0;

    inst_batch_t cur;
    memset(&cur, 0, sizeof(cur));
    int have_cur = 0;
    int cur_blend = 0;

    for (uint32_t i = 0; i < n; ++i)
    {
        int item_blend = R_inst_item_is_alpha_blend(&items[i]);

        if (!have_cur)
        {
            cur_start = mats->size;
            cur.model = items[i].model;
            cur.mesh_index = items[i].mesh_index;
            cur.lod = items[i].lod;
            cur.start = cur_start;
            cur.count = 0;
            cur.lod_ptr = items[i].lod_ptr;
            cur.mat_ptr = items[i].mat_ptr;
            cur.tex_key = items[i].tex_key;
            cur.mat_cutout = items[i].mat_cutout;
            cur.mat_blend = items[i].mat_blend;
            cur.mat_doublesided = items[i].mat_doublesided;
            cur.alpha_cutoff = items[i].alpha_cutoff;
            cur.albedo_tex = items[i].albedo_tex;
            have_cur = 1;
            cur_blend = item_blend;
        }
        else
        {
            int same_model = ihandle_eq(cur.model, items[i].model);
            int same_mesh = (cur.mesh_index == items[i].mesh_index);
            int same_lod = (cur.lod == items[i].lod);
            int same_key = (same_model && same_mesh && same_lod);

            if (cur_blend || item_blend || !same_key)
            {
                cur.count = mats->size - cur_start;
                if (cur.count)
                    vector_push_back(batches, &cur);

                cur_start = mats->size;
                cur.model = items[i].model;
                cur.mesh_index = items[i].mesh_index;
                cur.lod = items[i].lod;
                cur.start = cur_start;
                cur.count = 0;
                cur.lod_ptr = items[i].lod_ptr;
                cur.mat_ptr = items[i].mat_ptr;
                cur.tex_key = items[i].tex_key;
                cur.mat_cutout = items[i].mat_cutout;
                cur.mat_blend = items[i].mat_blend;
                cur.mat_doublesided = items[i].mat_doublesided;
                cur.alpha_cutoff = items[i].alpha_cutoff;
                cur.albedo_tex = items[i].albedo_tex;
                cur_blend = item_blend;
            }
        }

        instance_gpu_t ig;
        memset(&ig, 0, sizeof(ig));
        ig.m = items[i].m;
        ig.fade01 = items[i].fade01;

        vector_push_back(mats, &ig);

        if (item_blend)
        {
            cur.count = mats->size - cur_start;
            if (cur.count)
                vector_push_back(batches, &cur);
            have_cur = 0;
            cur_blend = 0;
        }
    }

    if (have_cur)
    {
        cur.count = mats->size - cur_start;
        if (cur.count)
            vector_push_back(batches, &cur);
    }
}

static void R_build_instancing(renderer_t *r)
{
    vector_clear(&r->inst_batches);
    vector_clear(&r->fwd_inst_batches);
    vector_clear(&r->inst_mats);

    frustum_t fr;
    R_frustum_build(&fr, r);

    uint32_t max_items = 0;

    for (uint32_t i = 0; i < r->models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        float max_scale = R_mat4_max_scale_xyz(&pm->model_matrix);
        const model_bounds_entry_t *mb = model_bounds_get_or_build(pm->model, mdl);
        if (mb)
        {
            vec3 wc = R_transform_point(pm->model_matrix, mb->c_local);
            float wr = mb->r_local * max_scale;
            if (!R_frustum_sphere_visible(&fr, wc, wr))
                continue;
        }

        max_items += mdl->meshes.size * 2u;
    }

    for (uint32_t i = 0; i < r->fwd_models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        float max_scale = R_mat4_max_scale_xyz(&pm->model_matrix);
        const model_bounds_entry_t *mb = model_bounds_get_or_build(pm->model, mdl);
        if (mb)
        {
            vec3 wc = R_transform_point(pm->model_matrix, mb->c_local);
            float wr = mb->r_local * max_scale;
            if (!R_frustum_sphere_visible(&fr, wc, wr))
                continue;
        }

        max_items += mdl->meshes.size * 2u;
    }

    if (!max_items)
        return;

    inst_item_t *items = R_inst_item_scratch(max_items);
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

        float max_scale = R_mat4_max_scale_xyz(&pm->model_matrix);
        const model_bounds_entry_t *mb = model_bounds_get_or_build(pm->model, mdl);
        if (mb)
        {
            vec3 wc = R_transform_point(pm->model_matrix, mb->c_local);
            float wr = mb->r_local * max_scale;
            if (!R_frustum_sphere_visible(&fr, wc, wr))
                continue;
        }

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            if (!R_mesh_visible_frustum(&fr, mesh, &pm->model_matrix, max_scale))
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            int mat_cutout = 0;
            int mat_blend = 0;
            int mat_doublesided = 0;
            float alpha_cutoff = 0.0f;
            uint32_t albedo_tex = 0;
            R_material_state(r, mat, &mat_cutout, &mat_blend, &mat_doublesided, &alpha_cutoff, &albedo_tex);
            uint64_t tex_key = R_material_tex_key(mat);

            float fade01 = 0.0f;
            int xfade01 = 0;
            uint32_t lod = R_pick_lod_level_for_mesh_fade01(r, mesh, &pm->model_matrix, max_scale, pm->model, mi, &fade01, &xfade01);

            if (xfade01 && mesh->lods.size >= 2)
            {
                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 0;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 0);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 1;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 1);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
            else
            {
                float f = 0.0f;
                if (lod == 1)
                    f = 1.0f;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = lod;
                items[n].fade01 = f;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, lod);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
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

        float max_scale = R_mat4_max_scale_xyz(&pm->model_matrix);
        const model_bounds_entry_t *mb = model_bounds_get_or_build(pm->model, mdl);
        if (mb)
        {
            vec3 wc = R_transform_point(pm->model_matrix, mb->c_local);
            float wr = mb->r_local * max_scale;
            if (!R_frustum_sphere_visible(&fr, wc, wr))
                continue;
        }

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            if (!R_mesh_visible_frustum(&fr, mesh, &pm->model_matrix, max_scale))
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            int mat_cutout = 0;
            int mat_blend = 0;
            int mat_doublesided = 0;
            float alpha_cutoff = 0.0f;
            uint32_t albedo_tex = 0;
            R_material_state(r, mat, &mat_cutout, &mat_blend, &mat_doublesided, &alpha_cutoff, &albedo_tex);
            uint64_t tex_key = R_material_tex_key(mat);

            float fade01 = 0.0f;
            int xfade01 = 0;
            uint32_t lod = R_pick_lod_level_for_mesh_fade01(r, mesh, &pm->model_matrix, max_scale, pm->model, mi, &fade01, &xfade01);

            if (xfade01 && mesh->lods.size >= 2)
            {
                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 0;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 0);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 1;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 1);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
            else
            {
                float f = 0.0f;
                if (lod == 1)
                    f = 1.0f;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = lod;
                items[n].fade01 = f;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, lod);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
        }
    }

    if (n)
        R_emit_batches_from_items(r, items, n, &r->inst_batches, &r->inst_mats);
}

static void R_build_shadow_instancing(renderer_t *r)
{
    vector_clear(&r->shadow_inst_batches);
    vector_clear(&r->shadow_inst_mats);

    uint32_t max_items = 0;

    for (uint32_t i = 0; i < r->models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        max_items += mdl->meshes.size * 2u;
    }

    for (uint32_t i = 0; i < r->fwd_models.size; ++i)
    {
        pushed_model_t *pm = (pushed_model_t *)vector_at(&r->fwd_models, i);
        if (!pm || !ihandle_is_valid(pm->model))
            continue;

        asset_model_t *mdl = R_resolve_model(r, pm->model);
        if (!mdl)
            continue;

        max_items += mdl->meshes.size * 2u;
    }

    if (!max_items)
        return;

    inst_item_t *items = R_inst_item_scratch(max_items);
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

        float max_scale = R_mat4_max_scale_xyz(&pm->model_matrix);
        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            int mat_cutout = 0;
            int mat_blend = 0;
            int mat_doublesided = 0;
            float alpha_cutoff = 0.0f;
            uint32_t albedo_tex = 0;
            R_material_state(r, mat, &mat_cutout, &mat_blend, &mat_doublesided, &alpha_cutoff, &albedo_tex);
            uint64_t tex_key = R_material_tex_key(mat);

            float fade01 = 0.0f;
            int xfade01 = 0;
            uint32_t lod = R_pick_lod_level_for_mesh_fade01(r, mesh, &pm->model_matrix, max_scale, pm->model, mi, &fade01, &xfade01);

            if (xfade01 && mesh->lods.size >= 2)
            {
                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 0;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 0);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 1;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 1);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
            else
            {
                float f = 0.0f;
                if (lod == 1)
                    f = 1.0f;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = lod;
                items[n].fade01 = f;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, lod);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
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

        float max_scale = R_mat4_max_scale_xyz(&pm->model_matrix);

        for (uint32_t mi = 0; mi < mdl->meshes.size; ++mi)
        {
            mesh_t *mesh = (mesh_t *)vector_at((vector_t *)&mdl->meshes, mi);
            if (!mesh)
                continue;

            asset_material_t *mat = R_resolve_material(r, mesh->material);
            int mat_cutout = 0;
            int mat_blend = 0;
            int mat_doublesided = 0;
            float alpha_cutoff = 0.0f;
            uint32_t albedo_tex = 0;
            R_material_state(r, mat, &mat_cutout, &mat_blend, &mat_doublesided, &alpha_cutoff, &albedo_tex);
            uint64_t tex_key = R_material_tex_key(mat);

            float fade01 = 0.0f;
            int xfade01 = 0;
            uint32_t lod = R_pick_lod_level_for_mesh_fade01(r, mesh, &pm->model_matrix, max_scale, pm->model, mi, &fade01, &xfade01);

            if (xfade01 && mesh->lods.size >= 2)
            {
                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 0;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 0);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = 1;
                items[n].fade01 = fade01;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, 1);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
            else
            {
                float f = 0.0f;
                if (lod == 1)
                    f = 1.0f;

                items[n].model = pm->model;
                items[n].mesh_index = mi;
                items[n].lod = lod;
                items[n].fade01 = f;
                items[n].m = pm->model_matrix;
                items[n].lod_ptr = (const mesh_lod_t *)vector_at((vector_t *)&mesh->lods, lod);
                items[n].mat_ptr = mat;
                items[n].tex_key = tex_key;
                items[n].mat_cutout = (uint8_t)(mat_cutout ? 1 : 0);
                items[n].mat_blend = (uint8_t)(mat_blend ? 1 : 0);
                items[n].mat_doublesided = (uint8_t)(mat_doublesided ? 1 : 0);
                items[n].alpha_cutoff = alpha_cutoff;
                items[n].albedo_tex = albedo_tex;
                n++;
            }
        }
    }

    if (n)
        R_emit_batches_from_items(r, items, n, &r->shadow_inst_batches, &r->shadow_inst_mats);
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

    R_fp_ensure_lights_capacity(r, count);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->fp.lights_ssbo);

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

static void R_depth_prepass(renderer_t *r)
{
    shader_t *depth = (r->depth_shader_id != 0xFF) ? R_get_shader(r, r->depth_shader_id) : NULL;
    if (!r || !depth)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, R_scene_draw_fbo(r));
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_mask(&r->gl, 1);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
    gl_state_enable(&r->gl, GL_MULTISAMPLE);
    gl_state_disable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    shader_bind(depth);
    shader_set_mat4(depth, "u_View", r->camera.view);
    shader_set_mat4(depth, "u_Proj", r->camera.proj);
    shader_set_int(depth, "u_UseInstancing", 1);

#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
    if (r->inst_mats.size)
        R_upload_instances_full(r, (const instance_gpu_t *)r->inst_mats.data, r->inst_mats.size);
#endif

    for (uint32_t bi = 0; bi < r->inst_batches.size; ++bi)
    {
        inst_batch_t *b = (inst_batch_t *)vector_at(&r->inst_batches, bi);
        if (!b || b->count == 0)
            continue;

        const mesh_lod_t *lod = b->lod_ptr;
        if (!lod)
        {
            asset_material_t *mat = NULL;
            if (!R_resolve_batch_resources(r, b, NULL, NULL, &lod, &mat))
                continue;
        }

        if (b->mat_blend)
            continue;

        if (b->mat_doublesided)
        {
            gl_state_disable(&r->gl, GL_CULL_FACE);
        }
        else
        {
            gl_state_enable(&r->gl, GL_CULL_FACE);
            glCullFace(GL_BACK);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->albedo_tex ? b->albedo_tex : r->black_tex);
        shader_set_int(depth, "u_AlbedoTex", 0);
        shader_set_float(depth, "u_AlphaCutoff", b->mat_cutout ? b->alpha_cutoff : 0.0f);

        int xfade_enabled = (b->lod == 0 || b->lod == 1) ? 1 : 0;
        int xfade_mode = (b->lod == 1) ? 1 : 0;
        shader_set_int(depth, "u_LodXFadeEnabled", xfade_enabled);
        shader_set_int(depth, "u_LodXFadeMode", xfade_mode);
        if (xfade_enabled)
            gl_state_enable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);
        else
            gl_state_disable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);

        R_mesh_ensure_instance_attribs(r, lod->vao);

        glBindVertexArray(lod->vao);
#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
        glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count, (GLuint)b->start);
#else
        instance_gpu_t *inst = (instance_gpu_t *)vector_at(&r->inst_mats, b->start);
        if (!inst)
        {
            glBindVertexArray(0);
            continue;
        }
        R_upload_instances(r, inst, b->count);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
#endif
        glBindVertexArray(0);
    }

    shader_unbind();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_state_disable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);

    shader_memory_barrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
}

static void R_camera_extract_near_far(const camera_t *c, float *out_near, float *out_far)
{
    float nearZ = 0.1f;
    float farZ = 1000.0f;

    if (c)
    {
        float m22 = c->proj.m[10];
        float m32 = c->proj.m[14];

        float dn = (m22 - 1.0f);
        float df = (m22 + 1.0f);

        if (fabsf(dn) > 1e-8f && fabsf(df) > 1e-8f)
        {
            float n = m32 / dn;
            float f = m32 / df;
            if (n > 1e-6f && f > n)
            {
                nearZ = n;
                farZ = f;
            }
        }
    }

    if (out_near)
        *out_near = nearZ;
    if (out_far)
        *out_far = farZ;
}

static int R_shadow_pick_light_index(const renderer_t *r)
{
    if (!r)
        return -1;

    for (uint32_t i = 0; i < r->lights.size; ++i)
    {
        const light_t *l = (const light_t *)vector_at((vector_t *)&r->lights, i);
        if (!l)
            continue;
        if (l->type == LIGHT_DIRECTIONAL)
            return (int)i;
    }

    return -1;
}

static vec3 R_shadow_pick_up(vec3 dir)
{
    dir = vec3_norm_safe(dir, 1e-6f);
    if (fabsf(dir.y) > 0.99f)
        return (vec3){0.0f, 0.0f, 1.0f};
    return (vec3){0.0f, 1.0f, 0.0f};
}

static void R_shadow_build_cascades(renderer_t *r, vec3 light_dir)
{
    if (!r)
        return;

    int cascades = r->shadow.cascades;
    if (cascades < 1)
        cascades = 1;
    if (cascades > R_SHADOW_MAX_CASCADES)
        cascades = R_SHADOW_MAX_CASCADES;

    float nearZ = 0.1f;
    float farZ = 1000.0f;
    R_camera_extract_near_far(&r->camera, &nearZ, &farZ);

    float max_dist = r->scene.shadow_max_dist;
    if (max_dist < nearZ)
        max_dist = nearZ;
    if (max_dist > farZ)
        max_dist = farZ;

    float lambda = r->scene.shadow_split_lambda;
    if (lambda < 0.0f)
        lambda = 0.0f;
    if (lambda > 1.0f)
        lambda = 1.0f;

    float splits[R_SHADOW_MAX_CASCADES] = {0};
    for (int i = 0; i < cascades; ++i)
    {
        float p = (float)(i + 1) / (float)cascades;
        float logd = nearZ * powf(max_dist / nearZ, p);
        float unid = nearZ + (max_dist - nearZ) * p;
        splits[i] = lambda * logd + (1.0f - lambda) * unid;
    }

    for (int i = 0; i < R_SHADOW_MAX_CASCADES; ++i)
        r->shadow.splits[i] = (i < cascades) ? splits[i] : 0.0f;

    float proj_fy = r->camera.proj.m[5];
    float proj_fx = r->camera.proj.m[0];
    if (fabsf(proj_fy) < 1e-6f)
        proj_fy = 1.0f;
    if (fabsf(proj_fx) < 1e-6f)
        proj_fx = 1.0f;

    float tan_half_fovy = 1.0f / proj_fy;
    float aspect = proj_fy / proj_fx;

    vec3 dir = vec3_norm_safe(light_dir, 1e-6f);
    if (vec3_len_sq(dir) < 1e-12f)
        dir = (vec3){0.0f, -1.0f, 0.0f};

    for (int ci = 0; ci < cascades; ++ci)
    {
        float split_near = (ci == 0) ? nearZ : splits[ci - 1];
        float split_far = splits[ci];

        vec3 corners_ws[8];
        {
            float zn = split_near;
            float zf = split_far;

            float yn = zn * tan_half_fovy;
            float xn = yn * aspect;

            float yf = zf * tan_half_fovy;
            float xf = yf * aspect;

            vec4 v0 = vec4_make(-xn, -yn, -zn, 1.0f);
            vec4 v1 = vec4_make(xn, -yn, -zn, 1.0f);
            vec4 v2 = vec4_make(xn, yn, -zn, 1.0f);
            vec4 v3 = vec4_make(-xn, yn, -zn, 1.0f);

            vec4 v4 = vec4_make(-xf, -yf, -zf, 1.0f);
            vec4 v5 = vec4_make(xf, -yf, -zf, 1.0f);
            vec4 v6 = vec4_make(xf, yf, -zf, 1.0f);
            vec4 v7 = vec4_make(-xf, yf, -zf, 1.0f);

            vec4 w0 = mat4_mul_vec4(r->camera.inv_view, v0);
            vec4 w1 = mat4_mul_vec4(r->camera.inv_view, v1);
            vec4 w2 = mat4_mul_vec4(r->camera.inv_view, v2);
            vec4 w3 = mat4_mul_vec4(r->camera.inv_view, v3);
            vec4 w4 = mat4_mul_vec4(r->camera.inv_view, v4);
            vec4 w5 = mat4_mul_vec4(r->camera.inv_view, v5);
            vec4 w6 = mat4_mul_vec4(r->camera.inv_view, v6);
            vec4 w7 = mat4_mul_vec4(r->camera.inv_view, v7);

            corners_ws[0] = (vec3){w0.x / w0.w, w0.y / w0.w, w0.z / w0.w};
            corners_ws[1] = (vec3){w1.x / w1.w, w1.y / w1.w, w1.z / w1.w};
            corners_ws[2] = (vec3){w2.x / w2.w, w2.y / w2.w, w2.z / w2.w};
            corners_ws[3] = (vec3){w3.x / w3.w, w3.y / w3.w, w3.z / w3.w};
            corners_ws[4] = (vec3){w4.x / w4.w, w4.y / w4.w, w4.z / w4.w};
            corners_ws[5] = (vec3){w5.x / w5.w, w5.y / w5.w, w5.z / w5.w};
            corners_ws[6] = (vec3){w6.x / w6.w, w6.y / w6.w, w6.z / w6.w};
            corners_ws[7] = (vec3){w7.x / w7.w, w7.y / w7.w, w7.z / w7.w};
        }

        vec3 center = (vec3){0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 8; ++i)
            center = vec3_add(center, corners_ws[i]);
        center = vec3_div_f(center, 8.0f);

        vec3 up = R_shadow_pick_up(dir);
        vec3 light_pos = vec3_sub(center, vec3_mul_f(dir, R_SHADOW_LIGHT_DISTANCE));
        mat4 V = mat4_lookat(light_pos, center, up);

        vec3 bmin = (vec3){1e30f, 1e30f, 1e30f};
        vec3 bmax = (vec3){-1e30f, -1e30f, -1e30f};

        for (int i = 0; i < 8; ++i)
        {
            vec4 p = mat4_mul_vec4(V, vec4_make(corners_ws[i].x, corners_ws[i].y, corners_ws[i].z, 1.0f));
            vec3 q = (vec3){p.x / p.w, p.y / p.w, p.z / p.w};
            bmin = vec3_min(bmin, q);
            bmax = vec3_max(bmax, q);
        }

        float pad_xy = R_SHADOW_PAD_XY;
        float pad_z = R_SHADOW_PAD_Z;

        bmin.x -= pad_xy;
        bmin.y -= pad_xy;
        bmax.x += pad_xy;
        bmax.y += pad_xy;
        bmin.z -= pad_z;
        bmax.z += pad_z;

        float w = bmax.x - bmin.x;
        float h = bmax.y - bmin.y;
        if (w < 1e-3f)
            w = 1e-3f;
        if (h < 1e-3f)
            h = 1e-3f;

        int size_ci = r->shadow.size;
        float texel_x = w / (float)MAX(size_ci, 1);
        float texel_y = h / (float)MAX(size_ci, 1);

        if (texel_x > 1e-8f && texel_y > 1e-8f)
        {
            bmin.x = floorf(bmin.x / texel_x) * texel_x;
            bmin.y = floorf(bmin.y / texel_y) * texel_y;
            bmax.x = bmin.x + w;
            bmax.y = bmin.y + h;
        }

        float zn = -bmax.z;
        float zf = -bmin.z;
        if (zn < 0.001f)
            zn = 0.001f;
        if (zf < zn + 0.001f)
            zf = zn + 0.001f;

        mat4 P = mat4_ortho(bmin.x, bmax.x, bmin.y, bmax.y, zn, zf);

        r->shadow.vp[ci] = mat4_mul(P, V);
    }

    for (int ci = cascades; ci < R_SHADOW_MAX_CASCADES; ++ci)
        r->shadow.vp[ci] = mat4_identity();
}

static void R_shadow_pass(renderer_t *r)
{
    if (!r || !r->cfg.shadows)
        return;

    shader_t *shadow = (r->shadow_shader_id != 0xFF) ? R_get_shader(r, r->shadow_shader_id) : NULL;
    if (!shadow)
        return;

    R_shadow_ensure(r);
    if (!r->shadow.tex || !r->shadow.fbo || r->shadow.cascades < 1)
        return;

    int li = R_shadow_pick_light_index(r);
    r->shadow.light_index = li;
    if (li < 0)
        return;

    light_t *l = (light_t *)vector_at(&r->lights, (uint32_t)li);
    if (!l)
        return;

    R_shadow_build_cascades(r, l->direction);

    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow.fbo);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glViewport(0, 0, r->shadow.size, r->shadow.size);
    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_mask(&r->gl, 1);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
    gl_state_disable(&r->gl, GL_MULTISAMPLE);
    gl_state_disable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    gl_state_enable(&r->gl, GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    shader_bind(shadow);
    shader_set_int(shadow, "u_UseInstancing", 1);
    shader_set_mat4(shadow, "u_View", mat4_identity());

#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
    if (r->shadow_inst_mats.size)
        R_upload_instances_full(r, (const instance_gpu_t *)r->shadow_inst_mats.data, r->shadow_inst_mats.size);
#endif

    for (int ci = 0; ci < r->shadow.cascades; ++ci)
    {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, r->shadow.tex, 0, ci);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        shader_set_mat4(shadow, "u_Proj", r->shadow.vp[ci]);

        for (uint32_t bi = 0; bi < r->shadow_inst_batches.size; ++bi)
        {
            inst_batch_t *b = (inst_batch_t *)vector_at(&r->shadow_inst_batches, bi);
            if (!b || b->count == 0)
                continue;

            const mesh_lod_t *lod = b->lod_ptr;
            if (!lod)
            {
                asset_material_t *mat = NULL;
                if (!R_resolve_batch_resources(r, b, NULL, NULL, &lod, &mat))
                    continue;
            }

            if (b->mat_blend)
                continue;

            if (b->mat_doublesided)
            {
                gl_state_disable(&r->gl, GL_CULL_FACE);
            }
            else
            {
                gl_state_enable(&r->gl, GL_CULL_FACE);
                glCullFace(GL_BACK);
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, b->albedo_tex ? b->albedo_tex : r->black_tex);
            shader_set_int(shadow, "u_AlbedoTex", 0);
            shader_set_float(shadow, "u_AlphaCutoff", b->mat_cutout ? b->alpha_cutoff : 0.0f);

            int xfade_enabled = (b->lod == 0 || b->lod == 1) ? 1 : 0;
            int xfade_mode = (b->lod == 1) ? 1 : 0;
            shader_set_int(shadow, "u_LodXFadeEnabled", xfade_enabled);
            shader_set_int(shadow, "u_LodXFadeMode", xfade_mode);

            R_mesh_ensure_instance_attribs(r, lod->vao);

            glBindVertexArray(lod->vao);
#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
            glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count, (GLuint)b->start);
#else
            instance_gpu_t *inst = (instance_gpu_t *)vector_at(&r->shadow_inst_mats, b->start);
            if (!inst)
            {
                glBindVertexArray(0);
                continue;
            }
            R_upload_instances(r, inst, b->count);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
#endif
            glBindVertexArray(0);
        }
    }

    shader_unbind();
    gl_state_disable(&r->gl, GL_POLYGON_OFFSET_FILL);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void R_fp_dispatch(renderer_t *r)
{
    if (!r)
        return;

    shader_t *init = (r->fp.shader_init_id != 0xFF) ? R_get_shader(r, r->fp.shader_init_id) : NULL;
    shader_t *cull = (r->fp.shader_cull_id != 0xFF) ? R_get_shader(r, r->fp.shader_cull_id) : NULL;
    shader_t *fin = (r->fp.shader_finalize_id != 0xFF) ? R_get_shader(r, r->fp.shader_finalize_id) : NULL;

    if (!init || !cull || !fin)
        return;

    uint32_t light_count = r->lights.size;
    uint32_t non_dir = 0u;
    for (uint32_t i = 0; i < light_count; ++i)
    {
        const light_t *l = (const light_t *)vector_at((vector_t *)&r->lights, i);
        if (!l)
            continue;
        if (l->type != LIGHT_DIRECTIONAL)
            non_dir++;
    }

    R_fp_ensure_lights_capacity(r, light_count ? light_count : 1u);
    (void)R_fp_upload_lights(r);

    if (non_dir == 0u || non_dir <= 16u)
        return;

    R_fp_resize_tile_buffers(r, r->fb_size, light_count ? light_count : 1u);
    uint32_t uploaded = light_count;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, r->fp.lights_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, r->fp.tile_index_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, r->fp.tile_list_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, r->fp.tile_depth_ssbo);

    float nearZ = 0.1f;
    float farZ = 1000.0f;
    R_camera_extract_near_far(&r->camera, &nearZ, &farZ);

    shader_bind(init);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    shader_set_int(init, "u_DepthTex", 0);
    shader_set_ivec2(init, "u_ScreenSize", r->fb_size);
    shader_set_ivec2(init, "u_TileCount", (vec2i){r->fp.tile_count_x, r->fp.tile_count_y});
    shader_set_int(init, "u_TileSize", FP_TILE_SIZE);
    shader_set_int(init, "u_TileMax", (int)r->fp.tile_max);
    shader_set_float(init, "u_Near", nearZ);
    shader_set_float(init, "u_Far", farZ);

    {
        uint32_t gx = (uint32_t)r->fp.tile_count_x;
        uint32_t gy = (uint32_t)r->fp.tile_count_y;
        if (gx < 1)
            gx = 1;
        if (gy < 1)
            gy = 1;
        shader_dispatch_compute(init, gx, gy, 1);
    }

    shader_memory_barrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    shader_bind(cull);
    shader_set_mat4(cull, "u_View", r->camera.view);
    shader_set_mat4(cull, "u_Proj", r->camera.proj);
    shader_set_ivec2(cull, "u_ScreenSize", r->fb_size);
    shader_set_ivec2(cull, "u_TileCount", (vec2i){r->fp.tile_count_x, r->fp.tile_count_y});
    shader_set_int(cull, "u_TileSize", FP_TILE_SIZE);
    shader_set_int(cull, "u_LightCount", (int)uploaded);
    shader_set_int(cull, "u_TileMax", (int)r->fp.tile_max);

    {
        uint32_t gx = (uint32_t)((uploaded + 63u) / 64u);
        if (gx < 1)
            gx = 1;
        shader_dispatch_compute(cull, gx, 1, 1);
    }

    shader_memory_barrier(GL_SHADER_STORAGE_BARRIER_BIT);

    shader_bind(fin);
    shader_set_int(fin, "u_TileCount", r->fp.tiles);
    shader_set_int(fin, "u_TileMax", (int)r->fp.tile_max);

    {
        uint32_t gx = (uint32_t)((r->fp.tiles + 255) / 256);
        if (gx < 1)
            gx = 1;
        shader_dispatch_compute(fin, gx, 1, 1);
    }

    shader_unbind();
    shader_memory_barrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

static void R_sky_pass(renderer_t *r)
{
    shader_t *sky = (r->sky_shader_id != 0xFF) ? R_get_shader(r, r->sky_shader_id) : NULL;
    if (!sky)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, R_scene_draw_fbo(r));
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    gl_state_disable(&r->gl, GL_CULL_FACE);
    gl_state_disable(&r->gl, GL_BLEND);

    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_mask(&r->gl, 0);
    gl_state_depth_func(&r->gl, GL_ALWAYS);

    shader_bind(sky);

    uint32_t env = ibl_get_env(r);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env ? env : r->black_cube);
    shader_set_int(sky, "u_Env", 0);
    shader_set_int(sky, "u_HasEnv", env ? 1 : 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    shader_set_int(sky, "u_Depth", 1);

    shader_set_mat4(sky, "u_InvProj", r->camera.inv_proj);
    shader_set_mat4(sky, "u_InvView", r->camera.inv_view);

    R_draw_fs_tri(r);

    gl_state_depth_mask(&r->gl, 1);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
}

typedef struct blend_batch_ref_t
{
    inst_batch_t *b;
    float depth2;
} blend_batch_ref_t;

static blend_batch_ref_t *g_blend_scratch = NULL;
static uint32_t g_blend_scratch_cap = 0;

static blend_batch_ref_t *R_blend_scratch(uint32_t need)
{
    if (need <= g_blend_scratch_cap && g_blend_scratch)
        return g_blend_scratch;

    blend_batch_ref_t *p = (blend_batch_ref_t *)realloc(g_blend_scratch, sizeof(blend_batch_ref_t) * (size_t)need);
    if (!p)
        return NULL;

    g_blend_scratch = p;
    g_blend_scratch_cap = need;
    return g_blend_scratch;
}

static int R_blend_sort_back_to_front(const void *a, const void *b)
{
    const blend_batch_ref_t *aa = (const blend_batch_ref_t *)a;
    const blend_batch_ref_t *bb = (const blend_batch_ref_t *)b;
    if (aa->depth2 < bb->depth2)
        return -1;
    if (aa->depth2 > bb->depth2)
        return 1;
    return 0;
}

static void R_forward_draw_filtered(renderer_t *r, shader_t *fwd, int draw_blend, int debug_mode)
{
    blend_batch_ref_t *blend_list = NULL;
    uint32_t blend_count = 0;

    if (draw_blend && r->inst_batches.size)
    {
        blend_list = R_blend_scratch(r->inst_batches.size);
    }

#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
    if (r->inst_mats.size)
        R_upload_instances_full(r, (const instance_gpu_t *)r->inst_mats.data, r->inst_mats.size);
#endif

    for (uint32_t bi = 0; bi < r->inst_batches.size; ++bi)
    {
        inst_batch_t *b = (inst_batch_t *)vector_at(&r->inst_batches, bi);
        if (!b || b->count == 0)
            continue;

        if (draw_blend)
        {
            if (!b->mat_blend)
                continue;
            if (blend_list && blend_count < r->inst_batches.size)
            {
                asset_model_t *mdl = R_resolve_model(r, b->model);
                mesh_t *mesh = (mdl && b->mesh_index < mdl->meshes.size) ? (mesh_t *)vector_at((vector_t *)&mdl->meshes, b->mesh_index) : NULL;
                if (!mesh)
                    continue;
                blend_list[blend_count].b = b;
                blend_list[blend_count].depth2 = R_batch_view_z(r, b, mesh);
                blend_count++;
            }
            continue;
        }

        const mesh_lod_t *lod = b->lod_ptr;
        const asset_material_t *mat = b->mat_ptr;
        if (!lod || !mat)
        {
            asset_material_t *mat_mut = NULL;
            mesh_t *mesh = NULL;
            if (!R_resolve_batch_resources(r, b, NULL, &mesh, &lod, &mat_mut))
                continue;
            mat = mat_mut;
        }

        if (b->mat_blend)
            continue;

        gl_state_disable(&r->gl, GL_BLEND);
        gl_state_depth_mask(&r->gl, 1);
        gl_state_enable(&r->gl, GL_MULTISAMPLE);

        if (b->mat_doublesided)
        {
            gl_state_disable(&r->gl, GL_CULL_FACE);
        }
        else
        {
            gl_state_enable(&r->gl, GL_CULL_FACE);
            glCullFace(GL_BACK);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->albedo_tex ? b->albedo_tex : r->black_tex);

        int lodp1 = (debug_mode == 1) ? (int)(b->lod + 1u) : 0;
        int packed = (debug_mode & 255) | ((lodp1 & 255) << 8);
        shader_set_int(fwd, "u_DebugMode", packed);

        int xfade_enabled = (b->lod == 0 || b->lod == 1) ? 1 : 0;
        int xfade_mode = (b->lod == 1) ? 1 : 0;
        shader_set_int(fwd, "u_LodXFadeEnabled", xfade_enabled);
        shader_set_int(fwd, "u_LodXFadeMode", xfade_mode);
        if (r->cfg.msaa && r->msaa_samples > 1 && b->mat_cutout)
            gl_state_enable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);
        else
            gl_state_disable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);

        R_mesh_ensure_instance_attribs(r, lod->vao);
        R_apply_material_or_default(r, fwd, (asset_material_t *)mat);
        shader_set_int(fwd, "u_MatAlphaBlend", b->mat_blend ? 1 : 0);
        shader_set_int(fwd, "u_MatAlphaCutout", b->mat_cutout ? 1 : 0);
        shader_set_int(fwd, "u_AlphaTest", b->mat_cutout ? 1 : 0);
        shader_set_float(fwd, "u_AlphaCutoff", b->mat_cutout ? b->alpha_cutoff : 0.0f);

        glBindVertexArray(lod->vao);
        R_stats_add_draw_instanced(r, lod->index_count, b->count);
#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
        glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count, (GLuint)b->start);
#else
        instance_gpu_t *inst = (instance_gpu_t *)vector_at(&r->inst_mats, b->start);
        if (inst)
        {
            R_upload_instances(r, inst, b->count);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
        }
#endif
        glBindVertexArray(0);
    }

    if (draw_blend && blend_count)
    {
        qsort(blend_list, blend_count, sizeof(blend_batch_ref_t), R_blend_sort_back_to_front);

        gl_state_enable(&r->gl, GL_BLEND);
        gl_state_blend_func(&r->gl, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl_state_depth_mask(&r->gl, 0);
        gl_state_disable(&r->gl, GL_SAMPLE_ALPHA_TO_COVERAGE);

        for (uint32_t i = 0; i < blend_count; ++i)
        {
            inst_batch_t *b = blend_list[i].b;
            if (!b)
                continue;

            const mesh_lod_t *lod = b->lod_ptr;
            const asset_material_t *mat = b->mat_ptr;
            if (!lod || !mat)
            {
                asset_material_t *mat_mut = NULL;
                if (!R_resolve_batch_resources(r, b, NULL, NULL, &lod, &mat_mut))
                    continue;
                mat = mat_mut;
            }

            if (!b->mat_blend)
                continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, b->albedo_tex ? b->albedo_tex : r->black_tex);

            int lodp1 = (debug_mode == 1) ? (int)(b->lod + 1u) : 0;
            int packed = (debug_mode & 255) | ((lodp1 & 255) << 8);
            shader_set_int(fwd, "u_DebugMode", packed);

            int xfade_enabled = (b->lod == 0 || b->lod == 1) ? 1 : 0;
            int xfade_mode = (b->lod == 1) ? 1 : 0;
            shader_set_int(fwd, "u_LodXFadeEnabled", xfade_enabled);
            shader_set_int(fwd, "u_LodXFadeMode", xfade_mode);

            R_mesh_ensure_instance_attribs(r, lod->vao);
            R_apply_material_or_default(r, fwd, (asset_material_t *)mat);
            shader_set_int(fwd, "u_MatAlphaBlend", b->mat_blend ? 1 : 0);
            shader_set_int(fwd, "u_MatAlphaCutout", b->mat_cutout ? 1 : 0);
            shader_set_int(fwd, "u_AlphaTest", b->mat_cutout ? 1 : 0);
            shader_set_float(fwd, "u_AlphaCutoff", b->mat_cutout ? b->alpha_cutoff : 0.0f);

            glBindVertexArray(lod->vao);

            if (b->mat_doublesided)
            {

                gl_state_enable(&r->gl, GL_CULL_FACE);

                glCullFace(GL_FRONT);
                R_stats_add_draw_instanced(r, lod->index_count, b->count);
#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
                glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count, (GLuint)b->start);
#else
                instance_gpu_t *inst = (instance_gpu_t *)vector_at(&r->inst_mats, b->start);
                if (inst)
                {
                    R_upload_instances(r, inst, b->count);
                    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
                }
#endif

                glCullFace(GL_BACK);
                R_stats_add_draw_instanced(r, lod->index_count, b->count);
#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
                glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count, (GLuint)b->start);
#else
                inst = (instance_gpu_t *)vector_at(&r->inst_mats, b->start);
                if (inst)
                {
                    R_upload_instances(r, inst, b->count);
                    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
                }
#endif
            }
            else
            {
                gl_state_enable(&r->gl, GL_CULL_FACE);
                glCullFace(GL_BACK);
                R_stats_add_draw_instanced(r, lod->index_count, b->count);
#if !defined(__APPLE__) && (defined(GLEW_ARB_base_instance) || defined(GLEW_VERSION_4_2))
                glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count, (GLuint)b->start);
#else
                instance_gpu_t *inst = (instance_gpu_t *)vector_at(&r->inst_mats, b->start);
                if (inst)
                {
                    R_upload_instances(r, inst, b->count);
                    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)lod->index_count, GL_UNSIGNED_INT, 0, (GLsizei)b->count);
                }
#endif
            }

            glBindVertexArray(0);
        }
    }

    (void)blend_list;
}

static void R_forward_one_pass(renderer_t *r)
{
    shader_t *fwd = (r->default_shader_id != 0xFF) ? R_get_shader(r, r->default_shader_id) : NULL;
    if (!fwd)
        return;

    ibl_ensure(r);
    R_per_frame_ubo_update(r);
    R_per_frame_ubo_bind(r);

    uint32_t irr = ibl_get_irradiance(r);
    uint32_t pre = ibl_get_prefilter(r);
    uint32_t brdf = ibl_get_brdf_lut(r);
    int has_ibl = (irr && pre && brdf) ? 1 : 0;

    glBindFramebuffer(GL_FRAMEBUFFER, R_scene_draw_fbo(r));
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_func(&r->gl, GL_LEQUAL);

    glFrontFace(GL_CCW);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, r->fp.lights_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, r->fp.tile_index_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, r->fp.tile_list_ssbo);

    shader_bind(fwd);

    int dir_indices[4] = {-1, -1, -1, -1};
    int dir_count = 0;
    int non_dir_indices[16];
    for (int i = 0; i < 16; ++i)
        non_dir_indices[i] = -1;
    int non_dir_count = 0;
    int non_dir_total = 0;

    for (uint32_t i = 0; i < r->lights.size; ++i)
    {
        const light_t *l = (const light_t *)vector_at((vector_t *)&r->lights, i);
        if (!l)
            continue;
        if (l->type == LIGHT_DIRECTIONAL)
        {
            if (dir_count < 4)
                dir_indices[dir_count++] = (int)i;
        }
        else
        {
            non_dir_total++;
            if (non_dir_count < 16)
                non_dir_indices[non_dir_count++] = (int)i;
        }
    }
    shader_set_int(fwd, "u_DirLightCount", dir_count);
    shader_set_int_array(fwd, "u_DirLightIndices", dir_indices, 4);
    shader_set_int(fwd, "u_NonDirLightCount", non_dir_count);
    shader_set_int_array(fwd, "u_NonDirLightIndices", non_dir_indices, 16);

    shader_set_int(fwd, "u_UseLightTiles", non_dir_total > 16 ? 1 : 0);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irr ? irr : r->black_cube);
    shader_set_int(fwd, "u_IrradianceMap", 8);

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_CUBE_MAP, pre ? pre : r->black_cube);
    shader_set_int(fwd, "u_PrefilterMap", 9);

    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, brdf ? brdf : r->black_tex);
    shader_set_int(fwd, "u_BRDFLUT", 10);

    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D_ARRAY, r->shadow.tex ? r->shadow.tex : 0);
    shader_set_int(fwd, "u_ShadowMap", 11);

    shader_set_int(fwd, "u_HasIBL", has_ibl);
    // u_IBLIntensity is in PerFrame UBO.

    shader_set_int(fwd, "u_TileSize", FP_TILE_SIZE);
    shader_set_int(fwd, "u_TileCountX", r->fp.tile_count_x);
    shader_set_int(fwd, "u_TileCountY", r->fp.tile_count_y);
    shader_set_int(fwd, "u_TileMax", (int)r->fp.tile_max);

    shader_set_int(fwd, "u_UseInstancing", 1);
    // Per-frame uniforms (view/proj/camera + shadow arrays) come from the PerFrame UBO.

    int mode = r->cfg.debug_mode;
    if (mode < 0)
        mode = 0;
    if (mode > 255)
        mode = 255;

    R_forward_draw_filtered(r, fwd, 0, mode);
    R_forward_draw_filtered(r, fwd, 1, mode);

    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_depth_mask(&r->gl, 1);

    if (r->cfg.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    gl_state_enable(&r->gl, GL_CULL_FACE);
    glCullFace(GL_BACK);
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

uint32_t R_get_final_color_tex(const renderer_t *r)
{
    if (!r)
        return 0;
    return r->final_color_tex;
}

int R_init(renderer_t *r, asset_manager_t *assets)
{
    if (!r)
        return 1;

    memset(r, 0, sizeof(*r));
    r->assets = assets;

    r->fg = fg_create();
    if (!r->fg)
        return 1;

    r->gbuf_shader_id = 0xFF;
    r->light_shader_id = 0xFF;
    r->default_shader_id = 0xFF;
    r->sky_shader_id = 0xFF;
    r->present_shader_id = 0xFF;
    r->depth_shader_id = 0xFF;
    r->shadow_shader_id = 0xFF;
    r->line3d_shader_id = 0xFF;

    r->fp.shader_init_id = 0xFF;
    r->fp.shader_cull_id = 0xFF;
    r->fp.shader_finalize_id = 0xFF;
    r->exposure_reduce_tex_shader_id = 0xFF;
    r->exposure_reduce_buf_shader_id = 0xFF;

    r->scene = R_scene_settings_default();
    r->exposure_adapted = r->scene.exposure;
    r->exposure_adapted_valid = false;
    r->exposure_readback_accum = 0.0f;
    r->exposure_pbo[0] = 0;
    r->exposure_pbo[1] = 0;
    r->exposure_pbo_index = 0;
    r->exposure_pbo_valid = 0;
    r->per_frame_ubo = 0;
    r->per_frame_ubo_valid = 0;
    r->exposure_reduce_ssbo[0] = 0;
    r->exposure_reduce_ssbo[1] = 0;
    r->exposure_reduce_cap_vec4 = 0;
    R_user_cfg_pull_from_cvars(r);

    r->shadow.fbo = 0;
    r->shadow.tex = 0;
    r->shadow.size = 0;
    r->shadow.cascades = 0;
    r->shadow.light_index = -1;

    r->clear_color = (vec4){0.02f, 0.02f, 0.02f, 1.0f};
    r->fb_size = (vec2i){1, 1};

    r->hdri_tex = ihandle_invalid();

    gl_state_reset(&r->gl);
    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
    gl_state_disable(&r->gl, GL_BLEND);

    glGenVertexArrays(1, &r->fs_vao);

    r->cpu_timings.valid = 0;
    for (int p = 0; p < (int)R_CPU_PHASE_COUNT; ++p)
        r->cpu_timings.ms[p] = 0.0;

    if (R_gpu_timers_supported())
    {
        for (int p = 0; p < (int)R_GPU_PHASE_COUNT; ++p)
            glGenQueries(16, r->gpu_queries[p]);
        r->gpu_query_index = 0;
        r->gpu_timer_active = 0;
        r->gpu_timings.valid = 0;
        for (int p = 0; p < (int)R_GPU_PHASE_COUNT; ++p)
        {
            r->gpu_timings.ms[p] = 0.0;
        }
    }

    R_make_black_tex(r);
    R_make_black_cube(r);

    r->lights = create_vector(light_t);
    r->models = create_vector(pushed_model_t);
    r->fwd_models = create_vector(pushed_model_t);
    r->shaders = create_vector(shader_t *);
    r->lines3d = create_vector(line3d_t);

    R_instance_stream_init(r);

    shader_t *fp_init = R_new_compute_shader_from_file("res/shaders/Forward/fp_init.comp");
    shader_t *fp_cull = R_new_compute_shader_from_file("res/shaders/Forward/fp_cull.comp");
    shader_t *fp_fin = R_new_compute_shader_from_file("res/shaders/Forward/fp_finalize.comp");

    if (!fp_init || !fp_cull || !fp_fin)
        return 1;

    r->fp.shader_init_id = R_add_shader(r, fp_init);
    r->fp.shader_cull_id = R_add_shader(r, fp_cull);
    r->fp.shader_finalize_id = R_add_shader(r, fp_fin);

    R_create_targets(r);

    memset(r->stats, 0, sizeof(r->stats));
    r->stats_write = 0;

    shader_t *depth_shader = R_new_shader_from_files("res/shaders/Forward/depth.vert", "res/shaders/Forward/depth.frag");
    if (!depth_shader)
    {
        LOG_WARN("Failed to load depth shader");
    }

    r->depth_shader_id = R_add_shader(r, depth_shader);

    shader_t *shadow_shader = R_new_shader_from_files("res/shaders/Forward/depth.vert", "res/shaders/Forward/shadow.frag");
    if (!shadow_shader)
    {
        LOG_WARN("Failed to load shadow shader");
    }

    r->shadow_shader_id = R_add_shader(r, shadow_shader);

    shader_t *forward_shader = R_new_shader_from_files("res/shaders/Forward/Forward.vert", "res/shaders/Forward/Forward.frag");
    if (!forward_shader)
        return 1;
    shader_bind_uniform_block(forward_shader, "PerFrame", 0);
    r->default_shader_id = R_add_shader(r, forward_shader);

    shader_t *sky_shader = R_new_shader_from_files("res/shaders/sky.vert", "res/shaders/sky.frag");
    if (!sky_shader)
        return 1;
    r->sky_shader_id = R_add_shader(r, sky_shader);

    shader_t *present_shader = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/present.frag");
    if (!present_shader)
        return 1;
    r->present_shader_id = R_add_shader(r, present_shader);

    shader_t *ae_tex = R_new_compute_shader_from_file("res/shaders/auto_exposure_reduce_tex.comp");
    shader_t *ae_buf = R_new_compute_shader_from_file("res/shaders/auto_exposure_reduce_buf.comp");
    if (!ae_tex || !ae_buf)
    {
        LOG_WARN("Failed to load auto exposure reduction shaders");
    }
    else
    {
        r->exposure_reduce_tex_shader_id = R_add_shader(r, ae_tex);
        r->exposure_reduce_buf_shader_id = R_add_shader(r, ae_buf);
    }

    shader_t *line3d_shader = R_new_shader_from_files("res/shaders/line3d.vert", "res/shaders/line3d.frag");
    if (!line3d_shader)
    {
        LOG_WARN("Failed to load line3d shader");
    }
    else
    {
        r->line3d_shader_id = R_add_shader(r, line3d_shader);
    }

    if (!ibl_init(r))
        LOG_ERROR("IBL init failed");

    if (!ssr_init(r))
        LOG_ERROR("SSR init failed");

    if (!bloom_init(r))
        LOG_ERROR("Bloom init failed");

    cvar_set_callback_name("cl_bloom", R_on_bloom_change);
    cvar_set_callback_name("cl_msaa_enabled", R_on_bloom_change);
    cvar_set_callback_name("cl_msaa_samples", R_on_bloom_change);
    cvar_set_callback_name("cl_render_debug", R_on_debug_mode_change);
    cvar_set_callback_name("cl_r_shadows", R_on_bloom_change);

    cvar_set_callback_name("cl_r_wireframe", R_on_wireframe_change);

    return 0;
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
    vector_free(&r->lines3d);

    if (r->line3d_vao)
        glDeleteVertexArrays(1, &r->line3d_vao);
    r->line3d_vao = 0;

    if (r->line3d_vbo)
        glDeleteBuffers(1, &r->line3d_vbo);
    r->line3d_vbo = 0;
    r->line3d_vbo_cap_vertices = 0;

    R_shadow_delete(r);

    R_gl_delete_targets(r);

    r->assets = NULL;

    if (r->black_tex)
        glDeleteTextures(1, &r->black_tex);
    r->black_tex = 0;

    if (r->black_cube)
        glDeleteTextures(1, &r->black_cube);
    r->black_cube = 0;

    R_fp_delete_buffers(r);

    if (r->per_frame_ubo)
        glDeleteBuffers(1, &r->per_frame_ubo);
    r->per_frame_ubo = 0;
    r->per_frame_ubo_valid = 0;

    if (r->exposure_pbo[0] || r->exposure_pbo[1])
        glDeleteBuffers(2, r->exposure_pbo);
    r->exposure_pbo[0] = 0;
    r->exposure_pbo[1] = 0;
    r->exposure_pbo_valid = 0;

    if (r->exposure_reduce_ssbo[0] || r->exposure_reduce_ssbo[1])
        glDeleteBuffers(2, r->exposure_reduce_ssbo);
    r->exposure_reduce_ssbo[0] = 0;
    r->exposure_reduce_ssbo[1] = 0;
    r->exposure_reduce_cap_vec4 = 0;

    free(g_inst_item_scratch);
    g_inst_item_scratch = NULL;
    g_inst_item_scratch_cap = 0;

    free(g_blend_scratch);
    g_blend_scratch = NULL;
    g_blend_scratch_cap = 0;

    free(g_line3d_vert_scratch);
    g_line3d_vert_scratch = NULL;
    g_line3d_vert_scratch_cap = 0;

    u32_set_free(&g_instanced_vao_set);
    model_bounds_free();

    if (R_gpu_timers_supported())
    {
        for (int p = 0; p < (int)R_GPU_PHASE_COUNT; ++p)
        {
            if (r->gpu_queries[p][0])
                glDeleteQueries(16, r->gpu_queries[p]);
            for (int i = 0; i < 16; ++i)
                r->gpu_queries[p][i] = 0;
        }
        r->gpu_timings.valid = 0;
    }

    fg_destroy(r->fg);
    r->fg = NULL;
}

void R_resize(renderer_t *r, vec2i size)
{
    if (!r)
        return;

    if (size.x < 1)
        size.x = 1;
    if (size.y < 1)
        size.y = 1;

    r->fb_size = size;
}

void R_update_resize(renderer_t *r)
{
    if (!r)
        return;

    if (r->fb_size.x < 1)
        r->fb_size.x = 1;
    if (r->fb_size.y < 1)
        r->fb_size.y = 1;

    if (r->fb_size_last.x == r->fb_size.x && r->fb_size_last.y == r->fb_size.y)
        return;

    r->fb_size_last = r->fb_size;

    R_create_targets(r);
    bloom_ensure(r);
    ssr_on_resize(r);
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

    // External callers may mutate GL state between frames; treat it as unknown at frame start.
    gl_state_reset(&r->gl);

    R_update_resize(r);

    r->scene = R_scene_settings_default();

    vector_clear(&r->lights);
    vector_clear(&r->models);
    vector_clear(&r->fwd_models);
    vector_clear(&r->lines3d);

    if (cvar_get_bool_name("cl_r_restore_gl_state"))
    {
        GLint prev_draw_fbo = 0;
        GLint prev_read_fbo = 0;
        GLint prev_viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
        glGetIntegerv(GL_VIEWPORT, prev_viewport);

        GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLint prev_scissor_box[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_SCISSOR_BOX, prev_scissor_box);

        GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);

        GLboolean prev_depth_mask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);

        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);

        glBindFramebuffer(GL_FRAMEBUFFER, R_scene_draw_fbo(r));
        glViewport(0, 0, r->fb_size.x, r->fb_size.y);

        glClearColor(r->clear_color.x, r->clear_color.y, r->clear_color.z, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev_draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_read_fbo);
        glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

        glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
        glDepthMask(prev_depth_mask);
        glScissor(prev_scissor_box[0], prev_scissor_box[1], prev_scissor_box[2], prev_scissor_box[3]);
        if (prev_scissor)
            glEnable(GL_SCISSOR_TEST);
    }
    else
    {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);

        glBindFramebuffer(GL_FRAMEBUFFER, R_scene_draw_fbo(r));
        glViewport(0, 0, r->fb_size.x, r->fb_size.y);

        glClearColor(r->clear_color.x, r->clear_color.y, r->clear_color.z, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    R_stats_begin_frame(r);
}

static void R_end_frame_legacy(renderer_t *r)
{
    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_SHADOW);
        R_shadow_pass(r);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_SHADOW] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_DEPTH_PREPASS);
        R_depth_prepass(r);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_DEPTH_PREPASS] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_RESOLVE_DEPTH);
        R_msaa_resolve(r, GL_DEPTH_BUFFER_BIT);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_RESOLVE_DEPTH] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_FP_CULL);
        R_fp_dispatch(r);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_FP_CULL] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_SKY);
        R_sky_pass(r);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_SKY] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_FORWARD);
        R_forward_one_pass(r);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_FORWARD] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    R_line3d_render(r);

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_RESOLVE_COLOR);
        R_msaa_resolve(r, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_RESOLVE_COLOR] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_AUTO_EXPOSURE);
        R_auto_exposure_update(r);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_AUTO_EXPOSURE] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_BLOOM);
        bloom_run(r, r->light_color_tex, r->black_tex);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_BLOOM] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    uint32_t bloom_tex = (r->cfg.bloom && r->bloom.mips) ? r->bloom.tex_up[0] : 0;
    {
        double t0 = R_time_now_ms();
        R_gpu_timer_begin(r, R_GPU_COMPOSITE);
        bloom_composite_to_final(r, r->light_color_tex, bloom_tex, r->gbuf_depth, r->black_tex);
        R_gpu_timer_end(r);
        r->cpu_timings.ms[R_CPU_COMPOSITE] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }
}

static void R_fg_shadow_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_SHADOW);
    R_shadow_pass(r);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_SHADOW] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_depth_prepass_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_DEPTH_PREPASS);
    R_depth_prepass(r);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_DEPTH_PREPASS] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_resolve_depth_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_RESOLVE_DEPTH);
    R_msaa_resolve(r, GL_DEPTH_BUFFER_BIT);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_RESOLVE_DEPTH] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_fp_cull_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_FP_CULL);
    R_fp_dispatch(r);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_FP_CULL] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_sky_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_SKY);
    R_sky_pass(r);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_SKY] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_forward_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_FORWARD);
    R_forward_one_pass(r);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_FORWARD] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_lines_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    R_line3d_render(r);
}

static void R_fg_resolve_color_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_RESOLVE_COLOR);
    R_msaa_resolve(r, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_RESOLVE_COLOR] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_auto_exposure_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_AUTO_EXPOSURE);
    R_auto_exposure_update(r);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_AUTO_EXPOSURE] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_bloom_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_BLOOM);
    bloom_run(r, r->light_color_tex, r->black_tex);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_BLOOM] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

static void R_fg_composite_exec(void *user)
{
    renderer_t *r = (renderer_t *)user;
    uint32_t bloom_tex = (r->cfg.bloom && r->bloom.mips) ? r->bloom.tex_up[0] : 0;
    double t0 = R_time_now_ms();
    R_gpu_timer_begin(r, R_GPU_COMPOSITE);
    bloom_composite_to_final(r, r->light_color_tex, bloom_tex, r->gbuf_depth, r->black_tex);
    R_gpu_timer_end(r);
    r->cpu_timings.ms[R_CPU_COMPOSITE] = R_time_now_ms() - t0;
    r->cpu_timings.valid = 1;
}

void R_end_frame(renderer_t *r)
{
    if (!r)
        return;

    R_gpu_timers_resolve(r);

    ibl_ensure(r);

    {
        double t0 = R_time_now_ms();
        R_build_instancing(r);
        r->cpu_timings.ms[R_CPU_BUILD_INSTANCING] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    {
        double t0 = R_time_now_ms();
        R_build_shadow_instancing(r);
        r->cpu_timings.ms[R_CPU_BUILD_SHADOW_INSTANCING] = R_time_now_ms() - t0;
        r->cpu_timings.valid = 1;
    }

    int used_frame_graph = 0;
    if (r->fg)
    {
        fg_begin(r->fg);

        fg_handle_t res_shadow = fg_import_texture2d_array(r->fg, "shadow_map", r->shadow.tex);
        fg_handle_t res_depth_msaa = fg_create_virtual(r->fg, "depth_msaa");
        fg_handle_t res_color_msaa = fg_create_virtual(r->fg, "color_msaa");
        fg_handle_t res_depth_resolved = fg_import_texture2d(r->fg, "depth_resolved", r->gbuf_depth);
        fg_handle_t res_color_resolved = fg_import_texture2d(r->fg, "color_resolved", r->light_color_tex);
        fg_handle_t res_bloom = fg_create_virtual(r->fg, "bloom");
        fg_handle_t res_final = fg_import_texture2d(r->fg, "final_color", r->final_color_tex);
        fg_handle_t res_fp = fg_create_virtual(r->fg, "fp_tiles");

        uint16_t p_shadow = fg_add_pass(r->fg, "shadow", R_fg_shadow_exec, r);
        fg_pass_use(r->fg, p_shadow, res_shadow, FG_ACCESS_WRITE);

        uint16_t p_depth = fg_add_pass(r->fg, "depth_prepass", R_fg_depth_prepass_exec, r);
        fg_pass_use(r->fg, p_depth, res_depth_msaa, FG_ACCESS_WRITE);

        uint16_t p_res_depth = fg_add_pass(r->fg, "resolve_depth", R_fg_resolve_depth_exec, r);
        fg_pass_use(r->fg, p_res_depth, res_depth_msaa, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_res_depth, res_depth_resolved, FG_ACCESS_WRITE);

        uint16_t p_fp = fg_add_pass(r->fg, "fp_cull", R_fg_fp_cull_exec, r);
        fg_pass_use(r->fg, p_fp, res_depth_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_fp, res_fp, FG_ACCESS_WRITE);

        uint16_t p_sky = fg_add_pass(r->fg, "sky", R_fg_sky_exec, r);
        fg_pass_use(r->fg, p_sky, res_depth_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_sky, res_color_msaa, FG_ACCESS_WRITE);

        uint16_t p_forward = fg_add_pass(r->fg, "forward", R_fg_forward_exec, r);
        fg_pass_use(r->fg, p_forward, res_shadow, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_forward, res_depth_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_forward, res_fp, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_forward, res_color_msaa, FG_ACCESS_WRITE);

        uint16_t p_lines = fg_add_pass(r->fg, "lines3d", R_fg_lines_exec, r);
        fg_pass_use(r->fg, p_lines, res_depth_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_lines, res_color_msaa, FG_ACCESS_WRITE);

        uint16_t p_res_color = fg_add_pass(r->fg, "resolve_color", R_fg_resolve_color_exec, r);
        fg_pass_use(r->fg, p_res_color, res_color_msaa, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_res_color, res_depth_msaa, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_res_color, res_color_resolved, FG_ACCESS_WRITE);
        fg_pass_use(r->fg, p_res_color, res_depth_resolved, FG_ACCESS_WRITE);

        uint16_t p_exposure = fg_add_pass(r->fg, "auto_exposure", R_fg_auto_exposure_exec, r);
        fg_pass_use(r->fg, p_exposure, res_color_resolved, FG_ACCESS_READ);

        uint16_t p_bloom = fg_add_pass(r->fg, "bloom", R_fg_bloom_exec, r);
        fg_pass_use(r->fg, p_bloom, res_color_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_bloom, res_bloom, FG_ACCESS_WRITE);

        uint16_t p_comp = fg_add_pass(r->fg, "composite", R_fg_composite_exec, r);
        fg_pass_use(r->fg, p_comp, res_color_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_comp, res_depth_resolved, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_comp, res_bloom, FG_ACCESS_READ);
        fg_pass_use(r->fg, p_comp, res_final, FG_ACCESS_WRITE);

        if (fg_compile(r->fg) && fg_execute(r->fg))
        {
            used_frame_graph = 1;
        }
    }

    if (!used_frame_graph)
    {
        LOG_WARN("Frame graph failed; using legacy render order");
        R_end_frame_legacy(r);
    }

    r->gpu_query_index = (r->gpu_query_index + 1u) & 15u;
}

void R_push_scene_settings(renderer_t *r, const renderer_scene_settings_t *settings)
{
    if (!r || !settings)
        return;

    r->scene = *settings;
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

void R_push_line3d(renderer_t *r, line3d_t line)
{
    if (!r)
        return;
    vector_push_back(&r->lines3d, &line);
}

void R_push_hdri(renderer_t *r, ihandle_t tex)
{
    if (!r)
        return;
    r->hdri_tex = tex;
}

const render_stats_t *R_get_stats(const renderer_t *r)
{
    return r ? &r->stats[r->stats_write ? 1u : 0u] : NULL;
}

const render_gpu_timings_t *R_get_gpu_timings(const renderer_t *r)
{
    return r ? &r->gpu_timings : NULL;
}

const render_cpu_timings_t *R_get_cpu_timings(const renderer_t *r)
{
    return r ? &r->cpu_timings : NULL;
}
