#include "systems/model_lod.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <float.h>

#include "utils/logger.h"

#ifndef MODEL_LOD_MAX
#define MODEL_LOD_MAX 8
#endif

static uint32_t u32_min(uint32_t a, uint32_t b) { return a < b ? a : b; }
static uint32_t u32_max(uint32_t a, uint32_t b) { return a > b ? a : b; }

static uint8_t clamp_lod_count(uint8_t c)
{
    if (c < 1) return 1;
    if (c > MODEL_LOD_MAX) return MODEL_LOD_MAX;
    return c;
}

static float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float settings_value_to_ratio(float v)
{
    if (v <= 0.0f) return 0.0f;
    if (v > 1.0f) return clamp01(v / 100.0f);
    return clamp01(v);
}

static int size_mul_ok(size_t a, size_t b, size_t* out)
{
    if (!out) return 0;
    if (a == 0 || b == 0) { *out = 0; return 1; }
    if (a > SIZE_MAX / b) return 0;
    *out = a * b;
    return 1;
}

static float safe_f(float x) { return isfinite(x) ? x : 0.0f; }

static void cpu_lod_free(model_cpu_lod_t *l)
{
    if (!l) return;
    free(l->vertices);
    free(l->indices);
    memset(l, 0, sizeof(*l));
}

static int cpu_lod_clone(const model_cpu_lod_t* src, model_cpu_lod_t* out)
{
    if (!src || !out || !src->vertices || !src->indices) return 0;
    if (src->vertex_count < 3u || src->index_count < 3u) return 0;

    memset(out, 0, sizeof(*out));

    size_t vb = 0, ib = 0;
    if (!size_mul_ok((size_t)src->vertex_count, sizeof(model_vertex_t), &vb)) return 0;
    if (!size_mul_ok((size_t)src->index_count, sizeof(uint32_t), &ib)) return 0;

    out->vertices = (model_vertex_t*)malloc(vb);
    out->indices  = (uint32_t*)malloc(ib);
    if (!out->vertices || !out->indices)
    {
        cpu_lod_free(out);
        return 0;
    }

    memcpy(out->vertices, src->vertices, vb);
    memcpy(out->indices,  src->indices,  ib);
    out->vertex_count = src->vertex_count;
    out->index_count  = src->index_count;
    return 1;
}

static uint32_t sanitize_tris(const uint32_t *src_idx, uint32_t src_icount, uint32_t vcount, uint32_t *dst_idx)
{
    if (!src_idx || !dst_idx) return 0;

    uint32_t w = 0u;
    uint32_t tri = src_icount / 3u;

    for (uint32_t t = 0; t < tri; ++t)
    {
        uint32_t i0 = src_idx[t * 3u + 0u];
        uint32_t i1 = src_idx[t * 3u + 1u];
        uint32_t i2 = src_idx[t * 3u + 2u];

        if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;
        if (i0 == i1 || i1 == i2 || i0 == i2) continue;

        dst_idx[w++] = i0;
        dst_idx[w++] = i1;
        dst_idx[w++] = i2;
    }

    return w;
}

typedef struct u64u32_map_t
{
    uint64_t* keys;
    uint32_t* vals;
    uint32_t cap;
    uint32_t mask;
} u64u32_map_t;

static uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

static uint32_t next_pow2_u32_safe(uint32_t x)
{
    if (x <= 1u) return 1u;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1u;
}

static int map_init(u64u32_map_t* m, uint32_t desired)
{
    if (!m) return 0;
    memset(m, 0, sizeof(*m));

    uint32_t cap = desired < 16u ? 32u : next_pow2_u32_safe(desired * 2u);
    if (cap < 32u) cap = 32u;

    size_t kb = 0, vb = 0;
    if (!size_mul_ok((size_t)cap, sizeof(uint64_t), &kb)) return 0;
    if (!size_mul_ok((size_t)cap, sizeof(uint32_t), &vb)) return 0;

    m->keys = (uint64_t*)malloc(kb);
    m->vals = (uint32_t*)malloc(vb);
    if (!m->keys || !m->vals)
    {
        free(m->keys);
        free(m->vals);
        memset(m, 0, sizeof(*m));
        return 0;
    }

    for (uint32_t i = 0; i < cap; ++i)
    {
        m->keys[i] = UINT64_MAX;
        m->vals[i] = 0xFFFFFFFFu;
    }

    m->cap = cap;
    m->mask = cap - 1u;
    return 1;
}

static void map_free(u64u32_map_t* m)
{
    if (!m) return;
    free(m->keys);
    free(m->vals);
    memset(m, 0, sizeof(*m));
}

static int map_get_or_insert(u64u32_map_t* m, uint64_t key, uint32_t* io_val, int* inserted)
{
    uint32_t h = (uint32_t)mix64(key);
    uint32_t i = h & m->mask;

    for (uint32_t step = 0; step < m->cap; ++step)
    {
        uint32_t slot = (i + step) & m->mask;

        if (m->keys[slot] == key)
        {
            if (io_val) *io_val = m->vals[slot];
            if (inserted) *inserted = 0;
            return 1;
        }

        if (m->keys[slot] == UINT64_MAX)
        {
            m->keys[slot] = key;
            m->vals[slot] = io_val ? *io_val : 0u;
            if (inserted) *inserted = 1;
            return 1;
        }
    }

    return 0;
}

static uint64_t edge_key_u64(uint32_t a, uint32_t b)
{
    uint32_t lo = u32_min(a, b);
    uint32_t hi = u32_max(a, b);
    uint64_t k = ((uint64_t)lo << 32) | (uint64_t)hi;
    if (k == UINT64_MAX) k -= 1ULL;
    return k;
}

static float uv_wrap_dist(float a, float b)
{
    float d0 = fabsf(a - b);
    float d1 = fabsf((a + 1.0f) - b);
    float d2 = fabsf((a - 1.0f) - b);
    float d = d0;
    if (d1 < d) d = d1;
    if (d2 < d) d = d2;
    return d;
}

static float tri_area3(const model_vertex_t* v0, const model_vertex_t* v1, const model_vertex_t* v2)
{
    float x0 = safe_f(v0->px), y0 = safe_f(v0->py), z0 = safe_f(v0->pz);
    float x1 = safe_f(v1->px), y1 = safe_f(v1->py), z1 = safe_f(v1->pz);
    float x2 = safe_f(v2->px), y2 = safe_f(v2->py), z2 = safe_f(v2->pz);

    float ax = x1 - x0, ay = y1 - y0, az = z1 - z0;
    float bx = x2 - x0, by = y2 - y0, bz = z2 - z0;

    float cx = ay*bz - az*by;
    float cy = az*bx - ax*bz;
    float cz = ax*by - ay*bx;

    float a2 = cx*cx + cy*cy + cz*cz;
    if (!isfinite(a2) || a2 <= 0.0f) return 0.0f;
    return 0.5f * sqrtf(a2);
}

static float ndot_edge(const model_vertex_t* a, const model_vertex_t* b)
{
    float anx = safe_f(a->nx), any = safe_f(a->ny), anz = safe_f(a->nz);
    float bnx = safe_f(b->nx), bny = safe_f(b->ny), bnz = safe_f(b->nz);
    float al2 = anx*anx + any*any + anz*anz;
    float bl2 = bnx*bnx + bny*bny + bnz*bnz;
    if (al2 <= 1e-20f || bl2 <= 1e-20f) return 1.0f;
    float inva = 1.0f / sqrtf(al2);
    float invb = 1.0f / sqrtf(bl2);
    return (anx*bnx + any*bny + anz*bnz) * inva * invb;
}

static int uv_seam_edge(const model_vertex_t* a, const model_vertex_t* b, float uv_max)
{
    if (uv_max <= 0.0f) return 0;
    float au = safe_f(a->u), av = safe_f(a->v);
    float bu = safe_f(b->u), bv = safe_f(b->v);
    float du = uv_wrap_dist(au, bu);
    float dv = uv_wrap_dist(av, bv);
    return (du > uv_max || dv > uv_max) ? 1 : 0;
}

typedef struct tri_rank_t
{
    float area;
    uint32_t t;
} tri_rank_t;

static int tri_rank_cmp(const void* pa, const void* pb)
{
    const tri_rank_t* a = (const tri_rank_t*)pa;
    const tri_rank_t* b = (const tri_rank_t*)pb;
    if (a->area < b->area) return -1;
    if (a->area > b->area) return 1;
    return 0;
}

typedef struct adj_node_t
{
    uint32_t to;
    uint32_t next;
    uint32_t eid;
} adj_node_t;

static int build_boundary_loops(const uint32_t* idx, uint32_t icount, uint32_t vcount,
                               uint32_t** out_loops_flat, uint32_t** out_loop_starts, uint32_t* out_loop_count)
{
    *out_loops_flat = NULL;
    *out_loop_starts = NULL;
    *out_loop_count = 0u;

    uint32_t tri = icount / 3u;
    uint32_t edge_est = tri * 3u;

    u64u32_map_t ecount;
    if (!map_init(&ecount, edge_est)) return 0;

    for (uint32_t t = 0; t < tri; ++t)
    {
        uint32_t i0 = idx[t*3u+0u];
        uint32_t i1 = idx[t*3u+1u];
        uint32_t i2 = idx[t*3u+2u];
        uint32_t vs[3] = { i0, i1, i2 };

        for (int e = 0; e < 3; ++e)
        {
            uint32_t a = vs[e];
            uint32_t b = vs[(e+1)%3];
            if (a >= vcount || b >= vcount || a == b) continue;
            uint64_t k = edge_key_u64(a, b);
            uint32_t val = 1u;
            int ins = 0;
            uint32_t cur = 0u;
            if (!map_get_or_insert(&ecount, k, &cur, &ins)) { map_free(&ecount); return 0; }
            if (!ins)
            {
                val = cur + 1u;
                if (!map_get_or_insert(&ecount, k, &val, &ins)) { map_free(&ecount); return 0; }
            }
        }
    }

    uint32_t bcap = edge_est * 2u;
    size_t nb = 0;
    if (!size_mul_ok((size_t)bcap, sizeof(adj_node_t), &nb)) { map_free(&ecount); return 0; }
    adj_node_t* nodes = (adj_node_t*)malloc(nb);
    if (!nodes) { map_free(&ecount); return 0; }
    uint32_t* head = (uint32_t*)malloc((size_t)vcount * sizeof(uint32_t));
    uint8_t* used = (uint8_t*)malloc((size_t)bcap);
    if (!head || !used)
    {
        free(nodes);
        free(head);
        free(used);
        map_free(&ecount);
        return 0;
    }
    for (uint32_t i = 0; i < vcount; ++i) head[i] = UINT32_MAX;

    uint32_t bcount = 0u;

    for (uint32_t t = 0; t < tri; ++t)
    {
        uint32_t i0 = idx[t*3u+0u];
        uint32_t i1 = idx[t*3u+1u];
        uint32_t i2 = idx[t*3u+2u];
        uint32_t vs[3] = { i0, i1, i2 };

        for (int e = 0; e < 3; ++e)
        {
            uint32_t a = vs[e];
            uint32_t b = vs[(e+1)%3];
            if (a >= vcount || b >= vcount || a == b) continue;

            uint64_t k = edge_key_u64(a, b);
            uint32_t cnt = 0u;
            int ins = 0;
            if (!map_get_or_insert(&ecount, k, &cnt, &ins)) continue;
            if (ins) continue;
            if (cnt != 1u) continue;

            if (bcount + 2u > bcap)
            {
                uint32_t new_cap = bcap < 1024u ? 2048u : bcap * 2u;
                size_t nb2 = 0;
                if (!size_mul_ok((size_t)new_cap, sizeof(adj_node_t), &nb2)) break;
                adj_node_t* np = (adj_node_t*)realloc(nodes, nb2);
                uint8_t* up = (uint8_t*)realloc(used, (size_t)new_cap);
                if (!np || !up) break;
                nodes = np;
                used = up;
                bcap = new_cap;
            }

            uint32_t eid = bcount;

            nodes[bcount].to = b;
            nodes[bcount].eid = eid;
            nodes[bcount].next = head[a];
            head[a] = bcount;
            bcount++;

            nodes[bcount].to = a;
            nodes[bcount].eid = eid;
            nodes[bcount].next = head[b];
            head[b] = bcount;
            bcount++;
        }
    }

    map_free(&ecount);

    memset(used, 0, (size_t)bcap);

    uint32_t loops_cap = 16u;
    uint32_t* loop_starts = (uint32_t*)malloc((size_t)(loops_cap + 1u) * sizeof(uint32_t));
    if (!loop_starts)
    {
        free(nodes); free(head); free(used);
        return 0;
    }

    uint32_t flat_cap = 256u;
    uint32_t* flat = (uint32_t*)malloc((size_t)flat_cap * sizeof(uint32_t));
    if (!flat)
    {
        free(loop_starts);
        free(nodes); free(head); free(used);
        return 0;
    }

    uint32_t loop_count = 0u;
    uint32_t flat_count = 0u;

    for (uint32_t vi = 0; vi < vcount; ++vi)
    {
        uint32_t n0 = head[vi];
        if (n0 == UINT32_MAX) continue;

        for (uint32_t n = n0; n != UINT32_MAX; n = nodes[n].next)
        {
            uint32_t eid = nodes[n].eid;
            if (eid >= bcap) continue;
            if (used[eid]) continue;

            uint32_t start_v = vi;
            uint32_t prev_v = UINT32_MAX;
            uint32_t cur_v = start_v;

            if (loop_count + 1u >= loops_cap)
            {
                uint32_t new_cap = loops_cap * 2u;
                uint32_t* np = (uint32_t*)realloc(loop_starts, (size_t)(new_cap + 1u) * sizeof(uint32_t));
                if (!np) break;
                loop_starts = np;
                loops_cap = new_cap;
            }

            loop_starts[loop_count] = flat_count;

            for (uint32_t steps = 0; steps < 1u<<30; ++steps)
            {
                if (flat_count + 1u > flat_cap)
                {
                    uint32_t new_cap = flat_cap * 2u;
                    uint32_t* np = (uint32_t*)realloc(flat, (size_t)new_cap * sizeof(uint32_t));
                    if (!np) break;
                    flat = np;
                    flat_cap = new_cap;
                }

                flat[flat_count++] = cur_v;

                uint32_t best_to = UINT32_MAX;
                uint32_t best_eid = UINT32_MAX;

                uint32_t h = head[cur_v];
                uint32_t deg = 0u;
                for (uint32_t it = h; it != UINT32_MAX; it = nodes[it].next) deg++;

                if (deg != 2u) break;

                for (uint32_t it = h; it != UINT32_MAX; it = nodes[it].next)
                {
                    uint32_t to = nodes[it].to;
                    uint32_t eid2 = nodes[it].eid;
                    if (to == prev_v) continue;
                    best_to = to;
                    best_eid = eid2;
                    break;
                }

                if (best_to == UINT32_MAX || best_eid == UINT32_MAX) break;

                used[best_eid] = 1u;

                prev_v = cur_v;
                cur_v = best_to;

                if (cur_v == start_v) break;
            }

            if (flat_count - loop_starts[loop_count] >= 3u) loop_count++;
            else flat_count = loop_starts[loop_count];
        }
    }

    loop_starts[loop_count] = flat_count;

    free(nodes);
    free(head);
    free(used);

    if (loop_count == 0u)
    {
        free(loop_starts);
        free(flat);
        return 1;
    }

    *out_loops_flat = flat;
    *out_loop_starts = loop_starts;
    *out_loop_count = loop_count;
    return 1;
}

static void add_tri_u32(uint32_t** idx, uint32_t* count, uint32_t* cap, uint32_t a, uint32_t b, uint32_t c)
{
    if (*count + 3u > *cap)
    {
        uint32_t new_cap = (*cap < 1024u) ? 2048u : (*cap * 2u);
        uint32_t* np = (uint32_t*)realloc(*idx, (size_t)new_cap * sizeof(uint32_t));
        if (!np) return;
        *idx = np;
        *cap = new_cap;
    }
    (*idx)[(*count)++] = a;
    (*idx)[(*count)++] = b;
    (*idx)[(*count)++] = c;
}

static uint32_t add_vertex(model_vertex_t** v, uint32_t* count, uint32_t* cap, model_vertex_t nv)
{
    if (*count + 1u > *cap)
    {
        uint32_t new_cap = (*cap < 1024u) ? 2048u : (*cap * 2u);
        model_vertex_t* np = (model_vertex_t*)realloc(*v, (size_t)new_cap * sizeof(model_vertex_t));
        if (!np) return 0xFFFFFFFFu;
        *v = np;
        *cap = new_cap;
    }
    uint32_t id = *count;
    (*v)[(*count)++] = nv;
    return id;
}

static void compact_mesh(model_cpu_lod_t* out)
{
    if (!out || !out->vertices || !out->indices) return;

    uint32_t vcount = out->vertex_count;
    uint32_t icount = out->index_count;

    uint8_t* used = (uint8_t*)calloc((size_t)vcount, 1);
    if (!used) return;

    for (uint32_t i = 0; i < icount; ++i)
    {
        uint32_t v = out->indices[i];
        if (v < vcount) used[v] = 1u;
    }

    uint32_t* remap = (uint32_t*)malloc((size_t)vcount * sizeof(uint32_t));
    if (!remap) { free(used); return; }

    uint32_t new_v = 0u;
    for (uint32_t i = 0; i < vcount; ++i)
    {
        if (used[i]) remap[i] = new_v++;
        else remap[i] = 0xFFFFFFFFu;
    }

    if (new_v < 3u)
    {
        free(remap);
        free(used);
        return;
    }

    model_vertex_t* nv = (model_vertex_t*)malloc((size_t)new_v * sizeof(model_vertex_t));
    if (!nv)
    {
        free(remap);
        free(used);
        return;
    }

    for (uint32_t i = 0; i < vcount; ++i)
    {
        if (!used[i]) continue;
        nv[remap[i]] = out->vertices[i];
    }

    for (uint32_t i = 0; i < icount; ++i)
    {
        uint32_t v = out->indices[i];
        out->indices[i] = (v < vcount) ? remap[v] : 0u;
    }

    free(out->vertices);
    out->vertices = nv;
    out->vertex_count = new_v;

    free(remap);
    free(used);
}

static float lod_effective_total_ratio(const model_lod_settings_t *s, uint8_t li, uint8_t lod_count)
{
    float total = settings_value_to_ratio(s->triangle_ratio[li]);
    if (total <= 0.0f) total = powf(0.5f, (float)li);
    total = clamp01(total);
    if (total < 0.001f) total = 0.001f;
    if (li == (uint8_t)(lod_count - 1u) && total > 0.5f) total = 0.5f;
    return total;
}

static int build_lod_drop_and_patch(const model_cpu_lod_t* src, float total_ratio, model_cpu_lod_t* out)
{
    memset(out, 0, sizeof(*out));
    if (!src || !src->vertices || !src->indices) return 0;
    if (src->vertex_count < 3u || src->index_count < 3u) return 0;

    uint32_t vcount0 = src->vertex_count;
    uint32_t icount0 = src->index_count - (src->index_count % 3u);
    if (icount0 < 3u) return 0;

    size_t idxb0 = 0;
    if (!size_mul_ok((size_t)icount0, sizeof(uint32_t), &idxb0)) return 0;

    uint32_t* clean_idx = (uint32_t*)malloc(idxb0);
    if (!clean_idx) return 0;

    uint32_t clean_icount = sanitize_tris(src->indices, icount0, vcount0, clean_idx);
    if (clean_icount < 3u) { free(clean_idx); return 0; }

    uint32_t tri0 = clean_icount / 3u;
    float r = clamp01(total_ratio);
    if (r <= 0.0f) r = 0.5f;

    uint32_t target_tri = (uint32_t)floorf((float)tri0 * r);
    if (target_tri < 1u) target_tri = 1u;
    if (target_tri >= tri0) target_tri = tri0;

    if (target_tri == tri0)
    {
        out->vertex_count = src->vertex_count;
        out->index_count = clean_icount;

        size_t vb = 0, ib = 0;
        if (!size_mul_ok((size_t)src->vertex_count, sizeof(model_vertex_t), &vb) ||
            !size_mul_ok((size_t)clean_icount, sizeof(uint32_t), &ib))
        {
            free(clean_idx);
            return 0;
        }

        out->vertices = (model_vertex_t*)malloc(vb);
        out->indices = (uint32_t*)malloc(ib);
        if (!out->vertices || !out->indices)
        {
            cpu_lod_free(out);
            free(clean_idx);
            return 0;
        }

        memcpy(out->vertices, src->vertices, vb);
        memcpy(out->indices, clean_idx, ib);

        free(clean_idx);
        return 1;
    }

    uint32_t edge_est = tri0 * 3u;
    u64u32_map_t edge_counts;
    if (!map_init(&edge_counts, edge_est))
    {
        free(clean_idx);
        return 0;
    }

    for (uint32_t t = 0; t < tri0; ++t)
    {
        uint32_t i0 = clean_idx[t*3u+0u];
        uint32_t i1 = clean_idx[t*3u+1u];
        uint32_t i2 = clean_idx[t*3u+2u];
        uint32_t vs[3] = { i0, i1, i2 };

        for (int e = 0; e < 3; ++e)
        {
            uint32_t a = vs[e];
            uint32_t b = vs[(e+1)%3];
            uint64_t k = edge_key_u64(a, b);
            uint32_t cur = 1u;
            int ins = 0;
            uint32_t prev = 0u;
            if (!map_get_or_insert(&edge_counts, k, &prev, &ins)) continue;
            if (!ins)
            {
                cur = prev + 1u;
                map_get_or_insert(&edge_counts, k, &cur, &ins);
            }
        }
    }

    float sharp_ndot = 0.35f;
    float uv_max = 1.0f / 64.0f;

    uint8_t* protect = (uint8_t*)calloc((size_t)tri0, 1);
    tri_rank_t* ranks = (tri_rank_t*)malloc((size_t)tri0 * sizeof(tri_rank_t));
    if (!protect || !ranks)
    {
        free(protect);
        free(ranks);
        map_free(&edge_counts);
        free(clean_idx);
        return 0;
    }

    uint32_t keep_forced = 0u;
    for (uint32_t t = 0; t < tri0; ++t)
    {
        uint32_t i0 = clean_idx[t*3u+0u];
        uint32_t i1 = clean_idx[t*3u+1u];
        uint32_t i2 = clean_idx[t*3u+2u];

        const model_vertex_t* v0 = &src->vertices[i0];
        const model_vertex_t* v1 = &src->vertices[i1];
        const model_vertex_t* v2 = &src->vertices[i2];

        float a = tri_area3(v0, v1, v2);
        if (!isfinite(a) || a < 0.0f) a = 0.0f;

        uint32_t vs[3] = { i0, i1, i2 };
        uint8_t p = 0u;

        for (int e = 0; e < 3; ++e)
        {
            uint32_t ea = vs[e];
            uint32_t eb = vs[(e+1)%3];
            uint64_t k = edge_key_u64(ea, eb);

            uint32_t cnt = 0u;
            int ins = 0;
            if (!map_get_or_insert(&edge_counts, k, &cnt, &ins)) continue;
            if (!ins && cnt == 1u) { p = 1u; break; }

            const model_vertex_t* va = &src->vertices[ea];
            const model_vertex_t* vb = &src->vertices[eb];

            if (ndot_edge(va, vb) < sharp_ndot) { p = 1u; break; }
            if (uv_seam_edge(va, vb, uv_max)) { p = 1u; break; }
        }

        protect[t] = p;
        if (p) keep_forced++;

        ranks[t].area = a;
        ranks[t].t = t;
    }

    map_free(&edge_counts);

    uint32_t keep_target = target_tri;
    if (keep_target < keep_forced) keep_target = keep_forced;

    qsort(ranks, (size_t)tri0, sizeof(tri_rank_t), tri_rank_cmp);

    uint8_t* keep = (uint8_t*)calloc((size_t)tri0, 1);
    if (!keep)
    {
        free(keep);
        free(protect);
        free(ranks);
        free(clean_idx);
        return 0;
    }

    for (uint32_t t = 0; t < tri0; ++t) if (protect[t]) keep[t] = 1u;

    uint32_t kept = keep_forced;
    uint32_t needed = (keep_target > kept) ? (keep_target - kept) : 0u;

    for (uint32_t i = tri0; i-- > 0u && needed > 0u;)
    {
        uint32_t t = ranks[i].t;
        if (keep[t]) continue;
        keep[t] = 1u;
        needed--;
        kept++;
    }

    uint32_t out_vcap = src->vertex_count + 256u;
    model_vertex_t* out_v = (model_vertex_t*)malloc((size_t)out_vcap * sizeof(model_vertex_t));
    if (!out_v)
    {
        free(keep);
        free(protect);
        free(ranks);
        free(clean_idx);
        return 0;
    }
    memcpy(out_v, src->vertices, (size_t)src->vertex_count * sizeof(model_vertex_t));
    uint32_t out_vcount = src->vertex_count;

    uint32_t out_icap = u32_max(3u, keep_target * 3u + 512u);
    uint32_t* out_i = (uint32_t*)malloc((size_t)out_icap * sizeof(uint32_t));
    if (!out_i)
    {
        free(out_v);
        free(keep);
        free(protect);
        free(ranks);
        free(clean_idx);
        return 0;
    }
    uint32_t out_icount = 0u;

    for (uint32_t t = 0; t < tri0; ++t)
    {
        if (!keep[t]) continue;
        uint32_t i0 = clean_idx[t*3u+0u];
        uint32_t i1 = clean_idx[t*3u+1u];
        uint32_t i2 = clean_idx[t*3u+2u];
        add_tri_u32(&out_i, &out_icount, &out_icap, i0, i1, i2);
    }

    uint32_t* loops_flat = NULL;
    uint32_t* loop_starts = NULL;
    uint32_t loop_count = 0u;

    if (!build_boundary_loops(out_i, out_icount, out_vcount, &loops_flat, &loop_starts, &loop_count))
    {
        free(loops_flat);
        free(loop_starts);
        free(out_i);
        free(out_v);
        free(keep);
        free(protect);
        free(ranks);
        free(clean_idx);
        return 0;
    }

    uint32_t max_loop_edges = 24u;

    for (uint32_t li = 0; li < loop_count; ++li)
    {
        uint32_t s = loop_starts[li];
        uint32_t e = loop_starts[li + 1u];
        uint32_t n = (e > s) ? (e - s) : 0u;
        if (n < 3u) continue;

        uint32_t step = 1u;
        if (n > max_loop_edges) step = (uint32_t)ceilf((float)n / (float)max_loop_edges);
        if (step < 1u) step = 1u;

        double cpx = 0.0, cpy = 0.0, cpz = 0.0;
        double cnx = 0.0, cny = 0.0, cnz = 0.0;
        double cu = 0.0, cv = 0.0;
        uint32_t cnt = 0u;

        for (uint32_t k = 0; k < n; k += step)
        {
            uint32_t vid = loops_flat[s + k];
            if (vid >= out_vcount) continue;
            model_vertex_t v = out_v[vid];
            cpx += (double)safe_f(v.px);
            cpy += (double)safe_f(v.py);
            cpz += (double)safe_f(v.pz);
            cnx += (double)safe_f(v.nx);
            cny += (double)safe_f(v.ny);
            cnz += (double)safe_f(v.nz);
            cu  += (double)safe_f(v.u);
            cv  += (double)safe_f(v.v);
            cnt += 1u;
        }

        if (cnt < 3u) continue;

        double inv = 1.0 / (double)cnt;

        model_vertex_t center;
        memset(&center, 0, sizeof(center));
        center.px = (float)(cpx * inv);
        center.py = (float)(cpy * inv);
        center.pz = (float)(cpz * inv);

        float nx = (float)(cnx * inv);
        float ny = (float)(cny * inv);
        float nz = (float)(cnz * inv);
        float nl2 = nx*nx + ny*ny + nz*nz;
        if (nl2 > 1e-20f)
        {
            float invl = 1.0f / sqrtf(nl2);
            center.nx = nx * invl;
            center.ny = ny * invl;
            center.nz = nz * invl;
        }
        else
        {
            center.nx = 0.0f;
            center.ny = 0.0f;
            center.nz = 1.0f;
        }

        center.u = (float)(cu * inv);
        center.v = (float)(cv * inv);

        uint32_t cvid = add_vertex(&out_v, &out_vcount, &out_vcap, center);
        if (cvid == 0xFFFFFFFFu) continue;

        uint32_t first = loops_flat[s + 0u];
        uint32_t prev = first;

        uint32_t used_pts = 0u;
        for (uint32_t k = step; k < n; k += step)
        {
            uint32_t cur = loops_flat[s + k];
            if (cur >= out_vcount || prev >= out_vcount) { prev = cur; continue; }
            add_tri_u32(&out_i, &out_icount, &out_icap, cvid, prev, cur);
            prev = cur;
            used_pts++;
        }

        if (used_pts >= 1u && first < out_vcount && prev < out_vcount)
        {
            add_tri_u32(&out_i, &out_icount, &out_icap, cvid, prev, first);
        }
    }

    free(loops_flat);
    free(loop_starts);

    out->vertices = out_v;
    out->vertex_count = out_vcount;
    out->indices = out_i;
    out->index_count = out_icount - (out_icount % 3u);

    compact_mesh(out);

    free(keep);
    free(protect);
    free(ranks);
    free(clean_idx);

    if (!out->vertices || !out->indices || out->index_count < 3u || out->vertex_count < 3u)
    {
        cpu_lod_free(out);
        return 0;
    }

    return 1;
}

bool model_raw_generate_lods(model_raw_t *raw, const model_lod_settings_t *s)
{
    clock_t t0 = clock();

    if (!raw || !s) return false;

    const uint8_t target_lod_count = clamp_lod_count(s->lod_count);

    bool ok_all = true;
    uint32_t min_lods = UINT32_MAX;

    for (uint32_t smi = 0; smi < raw->submeshes.size; ++smi)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&raw->submeshes, smi);
        if (!sm) { ok_all = false; continue; }
        if (sm->lods.size == 0) { ok_all = false; continue; }

        while (sm->lods.size > 1)
        {
            model_cpu_lod_t *l = (model_cpu_lod_t *)vector_impl_at(&sm->lods, sm->lods.size - 1);
            cpu_lod_free(l);
            sm->lods.size -= 1;
        }

        model_cpu_lod_t *lod0 = (model_cpu_lod_t *)vector_impl_at(&sm->lods, 0);
        if (!lod0 || !lod0->vertices || !lod0->indices || lod0->vertex_count < 3u || lod0->index_count < 3u)
        {
            ok_all = false;
            min_lods = u32_min(min_lods, (uint32_t)sm->lods.size);
            continue;
        }

        for (uint8_t li = 1; li < target_lod_count; ++li)
        {
            float total_ratio = lod_effective_total_ratio(s, li, target_lod_count);

            model_cpu_lod_t out;
            int ok = build_lod_drop_and_patch(lod0, total_ratio, &out);

            if (!ok)
            {
                ok_all = false;
                model_cpu_lod_t* prev = (model_cpu_lod_t*)vector_impl_at(&sm->lods, sm->lods.size - 1);
                model_cpu_lod_t fb;
                if (!cpu_lod_clone(prev, &fb)) break;
                out = fb;
            }

            uint32_t before = sm->lods.size;
            vector_impl_push_back(&sm->lods, &out);

            if (sm->lods.size != before + 1u)
            {
                ok_all = false;
                cpu_lod_free(&out);
                break;
            }
        }

        while (sm->lods.size < target_lod_count)
        {
            ok_all = false;
            model_cpu_lod_t* prev = (model_cpu_lod_t*)vector_impl_at(&sm->lods, sm->lods.size - 1);
            model_cpu_lod_t fb;
            if (!cpu_lod_clone(prev, &fb)) break;

            uint32_t before = sm->lods.size;
            vector_impl_push_back(&sm->lods, &fb);
            if (sm->lods.size != before + 1u)
            {
                cpu_lod_free(&fb);
                break;
            }
        }

        min_lods = u32_min(min_lods, (uint32_t)sm->lods.size);
    }

    if (min_lods == UINT32_MAX) min_lods = 1u;
    if (min_lods < 1u) min_lods = 1u;
    if (min_lods > (uint32_t)MODEL_LOD_MAX) min_lods = MODEL_LOD_MAX;

    for (uint32_t smi = 0; smi < raw->submeshes.size; ++smi)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&raw->submeshes, smi);
        if (!sm) continue;

        while (sm->lods.size > min_lods)
        {
            model_cpu_lod_t *l = (model_cpu_lod_t *)vector_impl_at(&sm->lods, sm->lods.size - 1);
            cpu_lod_free(l);
            sm->lods.size -= 1;
        }
    }

    raw->lod_count = (uint8_t)min_lods;
    if (raw->lod_count != target_lod_count) ok_all = false;

    for (uint32_t smi = 0; smi < raw->submeshes.size; ++smi)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&raw->submeshes, smi);
        if (!sm) continue;
        if (sm->lods.size == 0) continue;

        model_cpu_lod_t *l0 = (model_cpu_lod_t *)vector_impl_at(&sm->lods, 0);
        model_cpu_lod_t *lN = (model_cpu_lod_t *)vector_impl_at(&sm->lods, sm->lods.size - 1);

        uint32_t tri0 = 0u;
        uint32_t triN = 0u;

        if (l0 && l0->indices) tri0 = (l0->index_count / 3u);
        if (lN && lN->indices) triN = (lN->index_count / 3u);

        uint32_t min_tri = tri0;
        uint32_t max_tri = tri0;

        for (uint32_t li = 0; li < sm->lods.size; ++li)
        {
            model_cpu_lod_t *l = (model_cpu_lod_t *)vector_impl_at(&sm->lods, li);
            if (!l || !l->indices) continue;
            uint32_t tr = l->index_count / 3u;
            if (tr < min_tri) min_tri = tr;
            if (tr > max_tri) max_tri = tr;
        }

        LOG_OK("Submesh %u: LOD0 tris=%u  LOD%u tris=%u  (min=%u max=%u lods=%u)",
               (unsigned)smi,
               (unsigned)tri0,
               (unsigned)(sm->lods.size ? (sm->lods.size - 1u) : 0u),
               (unsigned)triN,
               (unsigned)min_tri,
               (unsigned)max_tri,
               (unsigned)sm->lods.size);
    }

    clock_t t1 = clock();
    double secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    LOG_OK("model_raw_generate_lods took %.6f seconds", secs);
    LOG_OK("Generated %u LODs per submesh (target was %u)", (unsigned)raw->lod_count, (unsigned)target_lod_count);

    return ok_all;
}
