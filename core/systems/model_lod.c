#include "systems/model_lod.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "vector.h"
#include "utils/logger.h"

#include "meshoptimizer.h"

#ifndef MODEL_LOD_MAX
#define MODEL_LOD_MAX 8
#endif

static uint32_t u32_max(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static void cpu_lod_destroy(model_cpu_lod_t *l)
{
    if (!l)
        return;
    free(l->vertices);
    free(l->indices);
    l->vertices = NULL;
    l->indices = NULL;
    l->vertex_count = 0;
    l->index_count = 0;
}

static int cpu_lod_clone(const model_cpu_lod_t *src, model_cpu_lod_t *dst)
{
    if (!src || !dst || !src->vertices || !src->indices)
        return 0;

    memset(dst, 0, sizeof(*dst));
    dst->vertex_count = src->vertex_count;
    dst->index_count = src->index_count;

    dst->vertices = (model_vertex_t *)malloc(sizeof(model_vertex_t) * (size_t)dst->vertex_count);
    dst->indices = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)dst->index_count);

    if (!dst->vertices || !dst->indices)
    {
        cpu_lod_destroy(dst);
        return 0;
    }

    memcpy(dst->vertices, src->vertices, sizeof(model_vertex_t) * (size_t)dst->vertex_count);
    memcpy(dst->indices, src->indices, sizeof(uint32_t) * (size_t)dst->index_count);
    return 1;
}

static int meshopt_build_lod(const model_cpu_lod_t *src, uint32_t target_tris, model_cpu_lod_t *out)
{
    if (!src || !out)
        return 0;
    if (!src->vertices || !src->indices)
        return 0;

    uint32_t src_tris = src->index_count / 3u;
    if (src_tris < 1u)
        return 0;

    if (target_tris >= src_tris)
        return cpu_lod_clone(src, out);

    size_t index_count = (size_t)src->index_count;
    size_t target_index_count = (size_t)(target_tris * 3u);

    uint32_t *simp = (uint32_t *)malloc(sizeof(uint32_t) * index_count);
    if (!simp)
        return 0;

    float target_error = 1e-2f;
    float lod_error = 0.0f;

    size_t new_index_count = meshopt_simplify(
        (unsigned int *)simp,
        (const unsigned int *)src->indices, index_count,
        (const float *)&src->vertices[0].px, (size_t)src->vertex_count, sizeof(model_vertex_t),
        target_index_count, target_error, 0, &lod_error);

    if (new_index_count == 0 || new_index_count >= index_count)
    {
        new_index_count = meshopt_simplifySloppy(
            (unsigned int *)simp,
            (const unsigned int *)src->indices, index_count,
            (const float *)&src->vertices[0].px, (size_t)src->vertex_count, sizeof(model_vertex_t),
            NULL,
            target_index_count, target_error, &lod_error);
    }

    if (new_index_count == 0 || new_index_count >= index_count)
    {
        free(simp);
        return 0;
    }

    meshopt_optimizeVertexCache((unsigned int *)simp, (const unsigned int *)simp, new_index_count, (size_t)src->vertex_count);
    meshopt_optimizeOverdraw((unsigned int *)simp, (const unsigned int *)simp, new_index_count, (const float *)&src->vertices[0].px, (size_t)src->vertex_count, sizeof(model_vertex_t), 1.05f);

    unsigned int *remap = (unsigned int *)malloc(sizeof(unsigned int) * (size_t)src->vertex_count);
    if (!remap)
    {
        free(simp);
        return 0;
    }

    size_t unique_vertices = meshopt_generateVertexRemap(
        remap,
        (const unsigned int *)simp, new_index_count,
        &src->vertices[0], (size_t)src->vertex_count, sizeof(model_vertex_t));

    if (unique_vertices < 3 || unique_vertices > (size_t)src->vertex_count)
    {
        free(remap);
        free(simp);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->vertex_count = (uint32_t)unique_vertices;
    out->index_count = (uint32_t)new_index_count;

    out->vertices = (model_vertex_t *)malloc(sizeof(model_vertex_t) * unique_vertices);
    out->indices = (uint32_t *)malloc(sizeof(uint32_t) * new_index_count);

    if (!out->vertices || !out->indices)
    {
        cpu_lod_destroy(out);
        free(remap);
        free(simp);
        return 0;
    }

    meshopt_remapVertexBuffer(out->vertices, src->vertices, (size_t)src->vertex_count, sizeof(model_vertex_t), remap);
    meshopt_remapIndexBuffer((unsigned int *)out->indices, (const unsigned int *)simp, new_index_count, remap);

    free(remap);
    free(simp);

    if (out->index_count < 3u)
    {
        cpu_lod_destroy(out);
        return 0;
    }

    return 1;
}

static uint32_t pick_min_tris(uint32_t tri0)
{
    uint32_t min_tris = 64u;
    if (tri0 < 256u)
        min_tris = 16u;
    if (tri0 < 96u)
        min_tris = 8u;
    min_tris = clamp_u32(min_tris, 4u, tri0);
    return min_tris;
}

bool model_raw_generate_lods(model_raw_t *raw)
{
    if (!raw)
        return false; 

    uint8_t max_lods = MODEL_LOD_MAX;
    uint8_t max_seen = 0;

    for (uint32_t smi = 0; smi < raw->submeshes.size; ++smi)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&raw->submeshes, smi);
        if (!sm || sm->lods.size == 0)
            continue;

        if (sm->lods.size > 1)
        {
            for (uint32_t li = 1; li < sm->lods.size; ++li)
            {
                model_cpu_lod_t *lod = (model_cpu_lod_t *)vector_impl_at(&sm->lods, li);
                if (lod)
                    cpu_lod_destroy(lod);
            }
            sm->lods.size = 1;
        }

        model_cpu_lod_t *lod0 = (model_cpu_lod_t *)vector_impl_at(&sm->lods, 0);
        if (!lod0 || !lod0->vertices || !lod0->indices || lod0->index_count < 3u || lod0->vertex_count < 3u)
            continue;

        uint32_t tri0 = lod0->index_count / 3u;
        if (tri0 < 4u)
            continue;

        uint32_t min_tris = pick_min_tris(tri0);

        uint8_t lod_count = 1;

        while (lod_count < max_lods)
        {
            model_cpu_lod_t *prev = (model_cpu_lod_t *)vector_impl_at(&sm->lods, (uint32_t)(lod_count - 1u));
            if (!prev || !prev->vertices || !prev->indices)
                break;

            uint32_t prev_tris = prev->index_count / 3u;
            if (prev_tris <= min_tris)
                break;

            uint32_t target_tris = u32_max(prev_tris / 2u, min_tris);
            if (target_tris >= prev_tris)
                break;

            model_cpu_lod_t out;
            memset(&out, 0, sizeof(out));

            if (!meshopt_build_lod(prev, target_tris, &out))
                break;

            uint32_t out_tris = out.index_count / 3u;

            if (out_tris >= prev_tris || out_tris < 1u)
            {
                cpu_lod_destroy(&out);
                break;
            }

            if (out_tris > prev_tris - u32_max(2u, prev_tris / 50u))
            {
                cpu_lod_destroy(&out);
                break;
            }

            vector_impl_push_back(&sm->lods, &out);
            lod_count++;
        }

        if (lod_count > max_seen)
            max_seen = lod_count;
    }

    raw->lod_count = max_seen ? max_seen : 1;

    return true;
}
