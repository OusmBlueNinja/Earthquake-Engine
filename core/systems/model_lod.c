#include "systems/model_lod.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t u32_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint8_t clamp_lod_count(uint8_t c)
{
    if (c < 1)
        return 1;
    if (c > MODEL_LOD_MAX)
        return MODEL_LOD_MAX;
    return c;
}

static float clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

static uint32_t u32_hash_mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t u32_hash3(int32_t x, int32_t y, int32_t z)
{
    uint32_t h = 2166136261u;
    h = (h ^ (uint32_t)x) * 16777619u;
    h = (h ^ (uint32_t)y) * 16777619u;
    h = (h ^ (uint32_t)z) * 16777619u;
    return u32_hash_mix(h);
}

static void v3_norm(float *x, float *y, float *z)
{
    float len2 = (*x) * (*x) + (*y) * (*y) + (*z) * (*z);
    if (len2 > 1e-20f)
    {
        float inv = 1.0f / sqrtf(len2);
        *x *= inv;
        *y *= inv;
        *z *= inv;
    }
    else
    {
        *x = 0.0f;
        *y = 1.0f;
        *z = 0.0f;
    }
}

static float v3_dot(float ax, float ay, float az, float bx, float by, float bz)
{
    return ax * bx + ay * by + az * bz;
}

static void tri_normal_from_pos(const model_vertex_t *v0, const model_vertex_t *v1, const model_vertex_t *v2, float *nx, float *ny, float *nz)
{
    float ax = v1->px - v0->px;
    float ay = v1->py - v0->py;
    float az = v1->pz - v0->pz;

    float bx = v2->px - v0->px;
    float by = v2->py - v0->py;
    float bz = v2->pz - v0->pz;

    float cx = ay * bz - az * by;
    float cy = az * bx - ax * bz;
    float cz = ax * by - ay * bx;

    float len2 = cx * cx + cy * cy + cz * cz;
    if (len2 > 1e-24f)
    {
        float inv = 1.0f / sqrtf(len2);
        cx *= inv;
        cy *= inv;
        cz *= inv;
    }
    else
    {
        cx = 0.0f;
        cy = 0.0f;
        cz = 0.0f;
    }

    *nx = cx;
    *ny = cy;
    *nz = cz;
}

static float tri_area_from_pos(const model_vertex_t *v0, const model_vertex_t *v1, const model_vertex_t *v2)
{
    float ax = v1->px - v0->px;
    float ay = v1->py - v0->py;
    float az = v1->pz - v0->pz;

    float bx = v2->px - v0->px;
    float by = v2->py - v0->py;
    float bz = v2->pz - v0->pz;

    float cx = ay * bz - az * by;
    float cy = az * bx - ax * bz;
    float cz = ax * by - ay * bx;

    float len2 = cx * cx + cy * cy + cz * cz;
    if (len2 <= 0.0f)
        return 0.0f;
    return 0.5f * sqrtf(len2);
}

static uint32_t lod_grid_res_from_ratio(float ratio)
{
    if (ratio > 0.90f)
        return 256u;
    if (ratio > 0.75f)
        return 192u;
    if (ratio > 0.50f)
        return 128u;
    if (ratio > 0.30f)
        return 96u;
    if (ratio > 0.15f)
        return 72u;
    return 56u;
}

typedef struct tri_rank_ctx_t
{
    float *score;
} tri_rank_ctx_t;

static int tri_score_desc_cmp(const void *a, const void *b, void *ud)
{
    const tri_rank_ctx_t *ctx = (const tri_rank_ctx_t *)ud;
    uint32_t ia = *(const uint32_t *)a;
    uint32_t ib = *(const uint32_t *)b;
    float sa = ctx->score[ia];
    float sb = ctx->score[ib];
    if (sa > sb)
        return -1;
    if (sa < sb)
        return 1;
    return (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
}

static void qsort_r_compat(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *, void *), void *ud)
{
#if defined(_WIN32)
    typedef int(__cdecl *cmp_fn)(void *, const void *, const void *);
    cmp_fn f = (cmp_fn)cmp;
    qsort_s(base, nmemb, size, f, ud);
#else
#if defined(__APPLE__) || defined(__MACH__)
    typedef int (*cmp_fn)(void *, const void *, const void *);
    cmp_fn f = (cmp_fn)cmp;
    qsort_r(base, nmemb, size, ud, f);
#else
    qsort_r(base, nmemb, size, cmp, ud);
#endif
#endif
}

static bool select_top_triangles_by_importance(const model_vertex_t *vtx, const uint32_t *idx, uint32_t tri_count, uint32_t target_tris, uint32_t *out_idx, uint32_t *out_w)
{
    if (!vtx || !idx || tri_count == 0 || target_tris == 0 || !out_idx || !out_w)
        return false;

    if (target_tris >= tri_count)
    {
        memcpy(out_idx, idx, (size_t)tri_count * 3u * sizeof(uint32_t));
        *out_w = tri_count * 3u;
        return true;
    }

    float *score = (float *)malloc((size_t)tri_count * sizeof(float));
    uint32_t *order = (uint32_t *)malloc((size_t)tri_count * sizeof(uint32_t));
    if (!score || !order)
    {
        free(score);
        free(order);
        return false;
    }

    for (uint32_t t = 0; t < tri_count; ++t)
    {
        uint32_t i0 = idx[t * 3u + 0u];
        uint32_t i1 = idx[t * 3u + 1u];
        uint32_t i2 = idx[t * 3u + 2u];

        const model_vertex_t *v0 = &vtx[i0];
        const model_vertex_t *v1 = &vtx[i1];
        const model_vertex_t *v2 = &vtx[i2];

        float tn_x, tn_y, tn_z;
        tri_normal_from_pos(v0, v1, v2, &tn_x, &tn_y, &tn_z);

        float an_x = v0->nx + v1->nx + v2->nx;
        float an_y = v0->ny + v1->ny + v2->ny;
        float an_z = v0->nz + v1->nz + v2->nz;
        v3_norm(&an_x, &an_y, &an_z);

        float d = fabsf(v3_dot(tn_x, tn_y, tn_z, an_x, an_y, an_z));
        float curvature = 1.0f - clamp01(d);

        float a = tri_area_from_pos(v0, v1, v2);

        float s = a * (1.0f + 3.0f * curvature);
        score[t] = s;
        order[t] = t;
    }

    tri_rank_ctx_t ctx;
    ctx.score = score;
    qsort_r_compat(order, (size_t)tri_count, sizeof(uint32_t), tri_score_desc_cmp, &ctx);

    uint32_t wi = 0;
    for (uint32_t k = 0; k < target_tris; ++k)
    {
        uint32_t t = order[k];
        out_idx[wi++] = idx[t * 3u + 0u];
        out_idx[wi++] = idx[t * 3u + 1u];
        out_idx[wi++] = idx[t * 3u + 2u];
    }

    free(score);
    free(order);

    *out_w = wi;
    return true;
}

static bool build_lod_by_triangle_sampling(const model_cpu_lod_t *src, float ratio, model_cpu_lod_t *out)
{
    memset(out, 0, sizeof(*out));

    if (!src || !src->vertices || !src->indices || src->vertex_count == 0 || src->index_count < 3)
        return false;

    uint32_t tri_src = src->index_count / 3u;
    if (tri_src == 0)
        return false;

    ratio = clamp01(ratio);

    if (ratio >= 0.999f)
    {
        out->vertex_count = src->vertex_count;
        out->index_count = src->index_count;

        out->vertices = (model_vertex_t *)malloc((size_t)out->vertex_count * sizeof(model_vertex_t));
        out->indices = (uint32_t *)malloc((size_t)out->index_count * sizeof(uint32_t));
        if (!out->vertices || !out->indices)
        {
            free(out->vertices);
            free(out->indices);
            memset(out, 0, sizeof(*out));
            return false;
        }

        memcpy(out->vertices, src->vertices, (size_t)out->vertex_count * sizeof(model_vertex_t));
        memcpy(out->indices, src->indices, (size_t)out->index_count * sizeof(uint32_t));
        return true;
    }

    float minx = src->vertices[0].px, miny = src->vertices[0].py, minz = src->vertices[0].pz;
    float maxx = minx, maxy = miny, maxz = minz;

    for (uint32_t i = 1; i < src->vertex_count; ++i)
    {
        const model_vertex_t *v = &src->vertices[i];
        if (v->px < minx)
            minx = v->px;
        if (v->py < miny)
            miny = v->py;
        if (v->pz < minz)
            minz = v->pz;
        if (v->px > maxx)
            maxx = v->px;
        if (v->py > maxy)
            maxy = v->py;
        if (v->pz > maxz)
            maxz = v->pz;
    }

    float ex = maxx - minx;
    float ey = maxy - miny;
    float ez = maxz - minz;

    float eps = 1e-6f;
    if (ex < eps)
        ex = eps;
    if (ey < eps)
        ey = eps;
    if (ez < eps)
        ez = eps;

    float extent = ex;
    if (ey > extent)
        extent = ey;
    if (ez > extent)
        extent = ez;

    uint32_t res = lod_grid_res_from_ratio(ratio);
    if (ratio <= 0.08f)
        res = 32u;
    else if (ratio <= 0.12f)
        res = 40u;
    else if (ratio <= 0.18f)
        res = u32_min(res, 56u);

    float cell = extent / (float)res;
    if (cell < 1e-6f)
        cell = 1e-6f;

    uint32_t table_cap = 1u;
    while (table_cap < src->vertex_count * 2u)
        table_cap <<= 1u;

    typedef struct cell_entry_t
    {
        uint32_t hash;
        uint32_t used;
        int32_t cx, cy, cz;
        uint32_t out_index;
        uint32_t count;
        float sum_px, sum_py, sum_pz;
        float sum_nx, sum_ny, sum_nz;
        float sum_tx, sum_ty, sum_tz, sum_tw;
        float sum_u, sum_v;
    } cell_entry_t;

    cell_entry_t *table = (cell_entry_t *)calloc((size_t)table_cap, sizeof(cell_entry_t));
    if (!table)
        return false;

    uint32_t *remap = (uint32_t *)malloc((size_t)src->vertex_count * sizeof(uint32_t));
    if (!remap)
    {
        free(table);
        return false;
    }

    uint32_t out_vcount = 0;

    for (uint32_t i = 0; i < src->vertex_count; ++i)
    {
        const model_vertex_t *v = &src->vertices[i];

        int32_t cx = (int32_t)((v->px - minx) / cell);
        int32_t cy = (int32_t)((v->py - miny) / cell);
        int32_t cz = (int32_t)((v->pz - minz) / cell);

        uint32_t h = u32_hash3(cx, cy, cz);
        uint32_t slot = h & (table_cap - 1u);

        for (;;)
        {
            cell_entry_t *e = &table[slot];

            if (!e->used)
            {
                e->used = 1u;
                e->hash = h;
                e->cx = cx;
                e->cy = cy;
                e->cz = cz;
                e->out_index = out_vcount++;
                e->count = 0u;
                e->sum_px = e->sum_py = e->sum_pz = 0.0f;
                e->sum_nx = e->sum_ny = e->sum_nz = 0.0f;
                e->sum_tx = e->sum_ty = e->sum_tz = e->sum_tw = 0.0f;
                e->sum_u = e->sum_v = 0.0f;
            }

            if (e->hash == h && e->cx == cx && e->cy == cy && e->cz == cz)
            {
                remap[i] = e->out_index;
                e->count += 1u;

                e->sum_px += v->px;
                e->sum_py += v->py;
                e->sum_pz += v->pz;

                e->sum_nx += v->nx;
                e->sum_ny += v->ny;
                e->sum_nz += v->nz;

                e->sum_tx += v->tx;
                e->sum_ty += v->ty;
                e->sum_tz += v->tz;
                e->sum_tw += v->tw;

                e->sum_u += v->u;
                e->sum_v += v->v;
                break;
            }

            slot = (slot + 1u) & (table_cap - 1u);
        }
    }

    model_vertex_t *new_vtx = (model_vertex_t *)malloc((size_t)out_vcount * sizeof(model_vertex_t));
    if (!new_vtx)
    {
        free(table);
        free(remap);
        return false;
    }

    for (uint32_t i = 0; i < out_vcount; ++i)
        memset(&new_vtx[i], 0, sizeof(model_vertex_t));

    for (uint32_t slot = 0; slot < table_cap; ++slot)
    {
        cell_entry_t *e = &table[slot];
        if (!e->used || e->count == 0u)
            continue;

        float inv = 1.0f / (float)e->count;
        model_vertex_t *v = &new_vtx[e->out_index];

        v->px = e->sum_px * inv;
        v->py = e->sum_py * inv;
        v->pz = e->sum_pz * inv;

        v->nx = e->sum_nx * inv;
        v->ny = e->sum_ny * inv;
        v->nz = e->sum_nz * inv;
        v3_norm(&v->nx, &v->ny, &v->nz);

        v->tx = e->sum_tx * inv;
        v->ty = e->sum_ty * inv;
        v->tz = e->sum_tz * inv;
        v3_norm(&v->tx, &v->ty, &v->tz);

        v->tw = (e->sum_tw * inv) >= 0.0f ? 1.0f : -1.0f;

        v->u = e->sum_u * inv;
        v->v = e->sum_v * inv;
    }

    free(table);

    uint32_t *tmp_idx = (uint32_t *)malloc((size_t)src->index_count * sizeof(uint32_t));
    if (!tmp_idx)
    {
        free(remap);
        free(new_vtx);
        return false;
    }

    uint32_t w = 0;

    for (uint32_t t = 0; t < tri_src; ++t)
    {
        uint32_t i0 = src->indices[t * 3u + 0u];
        uint32_t i1 = src->indices[t * 3u + 1u];
        uint32_t i2 = src->indices[t * 3u + 2u];

        if (i0 >= src->vertex_count || i1 >= src->vertex_count || i2 >= src->vertex_count)
            continue;

        uint32_t a = remap[i0];
        uint32_t b = remap[i1];
        uint32_t c = remap[i2];

        if (a == b || b == c || a == c)
            continue;

        float rn_x, rn_y, rn_z;
        tri_normal_from_pos(&src->vertices[i0], &src->vertices[i1], &src->vertices[i2], &rn_x, &rn_y, &rn_z);

        float nn_x, nn_y, nn_z;
        tri_normal_from_pos(&new_vtx[a], &new_vtx[b], &new_vtx[c], &nn_x, &nn_y, &nn_z);

        if (v3_dot(rn_x, rn_y, rn_z, nn_x, nn_y, nn_z) < 0.0f)
        {
            uint32_t tmp = b;
            b = c;
            c = tmp;

            tri_normal_from_pos(&new_vtx[a], &new_vtx[b], &new_vtx[c], &nn_x, &nn_y, &nn_z);
            if (v3_dot(rn_x, rn_y, rn_z, nn_x, nn_y, nn_z) < 0.0f)
                continue;
        }

        tmp_idx[w++] = a;
        tmp_idx[w++] = b;
        tmp_idx[w++] = c;
    }

    free(remap);

    if (w < 3u)
    {
        free(tmp_idx);
        free(new_vtx);
        return false;
    }

    uint32_t kept_tris = w / 3u;
    uint32_t target_tris = (uint32_t)((float)tri_src * ratio);
    if (target_tris < 1u)
        target_tris = 1u;
    if (target_tris > kept_tris)
        target_tris = kept_tris;

    uint32_t *new_idx = (uint32_t *)malloc((size_t)target_tris * 3u * sizeof(uint32_t));
    if (!new_idx)
    {
        free(tmp_idx);
        free(new_vtx);
        return false;
    }

    uint32_t out_w = 0;
    if (!select_top_triangles_by_importance(new_vtx, tmp_idx, kept_tris, target_tris, new_idx, &out_w))
    {
        free(new_idx);
        free(tmp_idx);
        free(new_vtx);
        return false;
    }

    free(tmp_idx);

    out->vertices = new_vtx;
    out->vertex_count = out_vcount;
    out->indices = new_idx;
    out->index_count = out_w;

    return true;
}

static void cpu_lod_free(model_cpu_lod_t *l)
{
    if (!l)
        return;
    free(l->vertices);
    free(l->indices);
    memset(l, 0, sizeof(*l));
}

bool model_raw_generate_lods(model_raw_t *raw, const model_lod_settings_t *s)
{
    if (!raw || !s)
        return false;

    uint8_t lod_count = clamp_lod_count(s->lod_count);
    raw->lod_count = lod_count;

    bool ok_all = true;

    for (uint32_t smi = 0; smi < raw->submeshes.size; ++smi)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&raw->submeshes, smi);
        if (!sm)
        {
            ok_all = false;
            continue;
        }

        if (sm->lods.size == 0)
        {
            ok_all = false;
            continue;
        }

        while (sm->lods.size > 1)
        {
            model_cpu_lod_t *l = (model_cpu_lod_t *)vector_impl_at(&sm->lods, sm->lods.size - 1);
            cpu_lod_free(l);
            sm->lods.size -= 1;
        }

        model_cpu_lod_t *lod0 = (model_cpu_lod_t *)vector_impl_at(&sm->lods, 0);
        if (!lod0 || !lod0->vertices || !lod0->indices || lod0->index_count < 3)
        {
            ok_all = false;
            continue;
        }

        for (uint8_t li = 1; li < lod_count; ++li)
        {
            float ratio = s->triangle_ratio[li];
            if (ratio <= 0.0f)
                ratio = 0.5f;

            ratio = clamp01(ratio);

            if (li == (uint8_t)(lod_count - 1))
            {
                ratio *= 0.35f;
                if (ratio < 0.01f)
                    ratio = 0.01f;
            }

            model_cpu_lod_t out;
            if (!build_lod_by_triangle_sampling(lod0, ratio, &out))
            {
                ok_all = false;
                continue;
            }

            vector_impl_push_back(&sm->lods, &out);
        }
    }

    return ok_all;
}
