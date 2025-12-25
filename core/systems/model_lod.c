#include "systems/model_lod.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "utils/logger.h"

/*
    This file keeps your existing "cluster LOD" generator for intermediate LODs,
    and replaces ONLY the LAST LOD with a boundary-protected EDGE-COLLAPSE simplifier.

    Why: Your last LOD was getting "holes" because triangles were being dropped after
    aggressive clustering + min-area filtering. Edge collapse can reduce triangles
    without punching random holes (when boundary edges are protected and flips rejected).
*/

static uint32_t u32_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

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

static void v3_norm(float *x, float *y, float *z)
{
    float len2 = (*x) * (*x) + (*y) * (*y) + (*z) * (*z);
    if (len2 > 1e-20f)
    {
        float inv = 1.0f / sqrtf(len2);
        *x *= inv; *y *= inv; *z *= inv;
    }
    else
    {
        *x = 0.0f; *y = 1.0f; *z = 0.0f;
    }
}

static float v3_dot(float ax, float ay, float az, float bx, float by, float bz)
{
    return ax * bx + ay * by + az * bz;
}

static void v3_cross(float ax, float ay, float az, float bx, float by, float bz, float *cx, float *cy, float *cz)
{
    *cx = ay * bz - az * by;
    *cy = az * bx - ax * bz;
    *cz = ax * by - ay * bx;
}

static float tri_cross_len2_pos3(const model_vertex_t *v0, const model_vertex_t *v1, const model_vertex_t *v2)
{
    float ax = v1->px - v0->px;
    float ay = v1->py - v0->py;
    float az = v1->pz - v0->pz;

    float bx = v2->px - v0->px;
    float by = v2->py - v0->py;
    float bz = v2->pz - v0->pz;

    float cx, cy, cz;
    v3_cross(ax, ay, az, bx, by, bz, &cx, &cy, &cz);
    return cx * cx + cy * cy + cz * cz;
}

static float min_len2_threshold_from_min_px(float min_px, float px_per_world_at_ref)
{
    float s = px_per_world_at_ref;
    if (min_px <= 0.0f || s <= 1e-20f) return 0.0f;
    float min_px2 = min_px * min_px;
    float min_px4 = min_px2 * min_px2;
    float s2 = s * s;
    float s4 = s2 * s2;
    return (4.0f * min_px4) / s4;
}

static uint32_t lod_res_from_ratio(float ratio)
{
    if (ratio > 0.90f) return 512u;
    if (ratio > 0.75f) return 384u;
    if (ratio > 0.55f) return 256u;
    if (ratio > 0.35f) return 192u;
    if (ratio > 0.20f) return 128u;
    if (ratio > 0.12f) return 96u;
    if (ratio > 0.07f) return 72u;
    if (ratio > 0.04f) return 56u;
    return 40u;
}

static uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* -------------------------- CLUSTER LOD (your original) -------------------------- */

typedef struct lod_scratch_t
{
    /* clustering scratch */
    uint64_t *keys;
    uint64_t *keys_tmp;
    uint32_t *order;
    uint32_t *order_tmp;
    uint32_t *remap;
    uint32_t *new_counts;
    model_vertex_t *new_vtx;
    uint32_t *tmp_idx;

    uint32_t cap_v;
    uint32_t cap_i;
    uint32_t cap_new;

    /* edge-collapse scratch (separate, allocated on demand) */
    uint32_t *tmp_u32;
    uint8_t  *tmp_u8;

    uint32_t cap_tmp_u32;
    uint32_t cap_tmp_u8;
} lod_scratch_t;

static void scratch_free(lod_scratch_t *sc)
{
    if (!sc) return;
    free(sc->keys);
    free(sc->keys_tmp);
    free(sc->order);
    free(sc->order_tmp);
    free(sc->remap);
    free(sc->new_counts);
    free(sc->new_vtx);
    free(sc->tmp_idx);
    free(sc->tmp_u32);
    free(sc->tmp_u8);
    memset(sc, 0, sizeof(*sc));
}

static int scratch_ensure_v(lod_scratch_t *sc, uint32_t vcount)
{
    if (sc->cap_v >= vcount && sc->keys && sc->keys_tmp && sc->order && sc->order_tmp && sc->remap)
        return 1;

    uint32_t cap = sc->cap_v ? sc->cap_v : 1u;
    while (cap < vcount) cap <<= 1u;

    uint64_t *k  = (uint64_t *)realloc(sc->keys,      (size_t)cap * sizeof(uint64_t));
    uint64_t *kt = (uint64_t *)realloc(sc->keys_tmp,  (size_t)cap * sizeof(uint64_t));
    uint32_t *o  = (uint32_t *)realloc(sc->order,     (size_t)cap * sizeof(uint32_t));
    uint32_t *ot = (uint32_t *)realloc(sc->order_tmp, (size_t)cap * sizeof(uint32_t));
    uint32_t *r  = (uint32_t *)realloc(sc->remap,     (size_t)cap * sizeof(uint32_t));

    if (!k || !kt || !o || !ot || !r)
    {
        sc->keys = k; sc->keys_tmp = kt; sc->order = o; sc->order_tmp = ot; sc->remap = r;
        return 0;
    }

    sc->keys = k; sc->keys_tmp = kt; sc->order = o; sc->order_tmp = ot; sc->remap = r;
    sc->cap_v = cap;
    return 1;
}

static int scratch_ensure_i(lod_scratch_t *sc, uint32_t icount)
{
    if (sc->cap_i >= icount && sc->tmp_idx) return 1;

    uint32_t cap = sc->cap_i ? sc->cap_i : 1u;
    while (cap < icount) cap <<= 1u;

    uint32_t *ti = (uint32_t *)realloc(sc->tmp_idx, (size_t)cap * sizeof(uint32_t));
    if (!ti) return 0;

    sc->tmp_idx = ti;
    sc->cap_i = cap;
    return 1;
}

static int scratch_ensure_new(lod_scratch_t *sc, uint32_t new_cap)
{
    if (sc->cap_new >= new_cap && sc->new_vtx && sc->new_counts) return 1;

    uint32_t cap = sc->cap_new ? sc->cap_new : 1u;
    while (cap < new_cap) cap <<= 1u;

    model_vertex_t *nv = (model_vertex_t *)realloc(sc->new_vtx, (size_t)cap * sizeof(model_vertex_t));
    uint32_t *nc = (uint32_t *)realloc(sc->new_counts, (size_t)cap * sizeof(uint32_t));
    if (!nv || !nc)
    {
        sc->new_vtx = nv; sc->new_counts = nc;
        return 0;
    }

    sc->new_vtx = nv; sc->new_counts = nc;
    sc->cap_new = cap;
    return 1;
}

static int scratch_ensure_tmp_u32(lod_scratch_t *sc, uint32_t n)
{
    if (sc->cap_tmp_u32 >= n && sc->tmp_u32) return 1;
    uint32_t cap = sc->cap_tmp_u32 ? sc->cap_tmp_u32 : 1u;
    while (cap < n) cap <<= 1u;
    uint32_t *p = (uint32_t *)realloc(sc->tmp_u32, (size_t)cap * sizeof(uint32_t));
    if (!p) return 0;
    sc->tmp_u32 = p;
    sc->cap_tmp_u32 = cap;
    return 1;
}

static int scratch_ensure_tmp_u8(lod_scratch_t *sc, uint32_t n)
{
    if (sc->cap_tmp_u8 >= n && sc->tmp_u8) return 1;
    uint32_t cap = sc->cap_tmp_u8 ? sc->cap_tmp_u8 : 1u;
    while (cap < n) cap <<= 1u;
    uint8_t *p = (uint8_t *)realloc(sc->tmp_u8, (size_t)cap * sizeof(uint8_t));
    if (!p) return 0;
    sc->tmp_u8 = p;
    sc->cap_tmp_u8 = cap;
    return 1;
}

/* radix sort "order" indices by u64 key */
static void radix_sort_u64_by_order(uint64_t *keys, uint32_t *order, uint64_t *keys_tmp, uint32_t *order_tmp, uint32_t n)
{
    uint32_t counts[256];

    for (uint32_t pass = 0; pass < 8u; ++pass)
    {
        memset(counts, 0, sizeof(counts));
        uint32_t shift = pass * 8u;

        for (uint32_t i = 0; i < n; ++i)
        {
            uint64_t k = keys[order[i]];
            uint32_t b = (uint32_t)((k >> shift) & 0xffu);
            counts[b] += 1u;
        }

        uint32_t sum = 0u;
        for (uint32_t i = 0; i < 256u; ++i)
        {
            uint32_t c = counts[i];
            counts[i] = sum;
            sum += c;
        }

        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t v = order[i];
            uint64_t k = keys[v];
            uint32_t b = (uint32_t)((k >> shift) & 0xffu);
            uint32_t dst = counts[b]++;
            order_tmp[dst] = v;
        }

        uint32_t *swp = order;
        order = order_tmp;
        order_tmp = swp;
    }

    /* ensure caller's "order" receives final result */
    memcpy(order_tmp, order, (size_t)n * sizeof(uint32_t));
    memcpy(order, order_tmp, (size_t)n * sizeof(uint32_t));
    (void)keys_tmp;
}

static uint64_t make_cell_key(uint32_t cx, uint32_t cy, uint32_t cz)
{
    uint64_t x = (uint64_t)(cx & 0x1fffffu);
    uint64_t y = (uint64_t)(cy & 0x1fffffu);
    uint64_t z = (uint64_t)(cz & 0x1fffffu);
    return x | (y << 21u) | (z << 42u);
}

static int build_lod_cluster_fast(const model_cpu_lod_t *src,
                                 float ratio,
                                 uint32_t res,
                                 int enforce_min_px,
                                 float min_len2_threshold,
                                 float minx, float miny, float minz,
                                 float inv_cell,
                                 lod_scratch_t *sc,
                                 model_cpu_lod_t *out)
{
    memset(out, 0, sizeof(*out));

    uint32_t vcount = src->vertex_count;
    uint32_t icount = src->index_count;

    if (!scratch_ensure_v(sc, vcount)) return 0;
    if (!scratch_ensure_i(sc, icount)) return 0;

    for (uint32_t i = 0; i < vcount; ++i)
    {
        const model_vertex_t *v = &src->vertices[i];

        float fx = (v->px - minx) * inv_cell;
        float fy = (v->py - miny) * inv_cell;
        float fz = (v->pz - minz) * inv_cell;

        uint32_t cx = (uint32_t)((fx < 0.0f) ? 0u : (uint32_t)fx);
        uint32_t cy = (uint32_t)((fy < 0.0f) ? 0u : (uint32_t)fy);
        uint32_t cz = (uint32_t)((fz < 0.0f) ? 0u : (uint32_t)fz);

        cx = clamp_u32(cx, 0u, res - 1u);
        cy = clamp_u32(cy, 0u, res - 1u);
        cz = clamp_u32(cz, 0u, res - 1u);

        sc->keys[i]  = make_cell_key(cx, cy, cz);
        sc->order[i] = i;
    }

    radix_sort_u64_by_order(sc->keys, sc->order, sc->keys_tmp, sc->order_tmp, vcount);

    uint32_t new_vcount = 0;
    if (!scratch_ensure_new(sc, vcount)) return 0;

    uint32_t run_begin = 0;
    while (run_begin < vcount)
    {
        uint32_t vi0 = sc->order[run_begin];
        uint64_t k = sc->keys[vi0];

        uint32_t run_end = run_begin + 1u;
        while (run_end < vcount)
        {
            uint32_t vik = sc->order[run_end];
            if (sc->keys[vik] != k) break;
            run_end += 1u;
        }

        float sum_px = 0.0f, sum_py = 0.0f, sum_pz = 0.0f;
        float sum_nx = 0.0f, sum_ny = 0.0f, sum_nz = 0.0f;
        float sum_tx = 0.0f, sum_ty = 0.0f, sum_tz = 0.0f, sum_tw = 0.0f;
        float sum_u = 0.0f, sum_v = 0.0f;
        uint32_t cnt = 0u;

        for (uint32_t r = run_begin; r < run_end; ++r)
        {
            uint32_t vi = sc->order[r];
            const model_vertex_t *v = &src->vertices[vi];

            sc->remap[vi] = new_vcount;

            sum_px += v->px; sum_py += v->py; sum_pz += v->pz;
            sum_nx += v->nx; sum_ny += v->ny; sum_nz += v->nz;
            sum_tx += v->tx; sum_ty += v->ty; sum_tz += v->tz;
            sum_tw += v->tw;
            sum_u  += v->u;  sum_v  += v->v;

            cnt += 1u;
        }

        float inv = 1.0f / (float)cnt;
        model_vertex_t *nv = &sc->new_vtx[new_vcount];

        nv->px = sum_px * inv; nv->py = sum_py * inv; nv->pz = sum_pz * inv;

        nv->nx = sum_nx * inv; nv->ny = sum_ny * inv; nv->nz = sum_nz * inv;
        v3_norm(&nv->nx, &nv->ny, &nv->nz);

        nv->tx = sum_tx * inv; nv->ty = sum_ty * inv; nv->tz = sum_tz * inv;
        v3_norm(&nv->tx, &nv->ty, &nv->tz);

        nv->tw = (sum_tw * inv) >= 0.0f ? 1.0f : -1.0f;

        nv->u = sum_u * inv; nv->v = sum_v * inv;

        sc->new_counts[new_vcount] = cnt;
        new_vcount += 1u;

        run_begin = run_end;
    }

    uint32_t tri_src = icount / 3u;
    uint32_t w = 0;

    for (uint32_t t = 0; t < tri_src; ++t)
    {
        uint32_t i0 = src->indices[t * 3u + 0u];
        uint32_t i1 = src->indices[t * 3u + 1u];
        uint32_t i2 = src->indices[t * 3u + 2u];

        if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;

        uint32_t a = sc->remap[i0];
        uint32_t b = sc->remap[i1];
        uint32_t c = sc->remap[i2];

        if (a == b || b == c || a == c) continue;

        if (enforce_min_px)
        {
            const model_vertex_t *v0 = &sc->new_vtx[a];
            const model_vertex_t *v1 = &sc->new_vtx[b];
            const model_vertex_t *v2 = &sc->new_vtx[c];
            float len2 = tri_cross_len2_pos3(v0, v1, v2);
            if (len2 < min_len2_threshold) continue;
        }

        sc->tmp_idx[w++] = a;
        sc->tmp_idx[w++] = b;
        sc->tmp_idx[w++] = c;
    }

    if (w < 3u || new_vcount < 3u) return 0;

    out->vertex_count = new_vcount;
    out->index_count  = w;

    out->vertices = (model_vertex_t *)malloc((size_t)new_vcount * sizeof(model_vertex_t));
    out->indices  = (uint32_t *)malloc((size_t)w * sizeof(uint32_t));
    if (!out->vertices || !out->indices)
    {
        free(out->vertices);
        free(out->indices);
        memset(out, 0, sizeof(*out));
        return 0;
    }

    memcpy(out->vertices, sc->new_vtx, (size_t)new_vcount * sizeof(model_vertex_t));
    memcpy(out->indices,  sc->tmp_idx, (size_t)w * sizeof(uint32_t));

    (void)ratio;
    return 1;
}

/* -------------------------- EDGE COLLAPSE (last LOD) -------------------------- */

/*
    This is a conservative edge-collapse simplifier:

    - Protect boundary edges (edges with only 1 incident triangle) -> avoids opening holes.
    - Reject collapses that would flip affected triangles (normal dot < flip_threshold).
    - Cost = squared edge length (optionally + normal/uv penalty if you want to add later).
    - Uses a lazy min-heap: we push candidate edges, and validate on pop.

    NOTE: This is not full QEM. It's intentionally simpler and robust.
*/

typedef struct ec_incident_list_t
{
    uint32_t *tris;
    uint32_t count;
    uint32_t cap;
} ec_incident_list_t;

static int ec_incident_push(ec_incident_list_t *lst, uint32_t tri_id)
{
    if (lst->count == lst->cap)
    {
        uint32_t nc = lst->cap ? (lst->cap * 2u) : 8u;
        uint32_t *p = (uint32_t *)realloc(lst->tris, (size_t)nc * sizeof(uint32_t));
        if (!p) return 0;
        lst->tris = p;
        lst->cap = nc;
    }
    lst->tris[lst->count++] = tri_id;
    return 1;
}

typedef struct ec_edgekey_t
{
    uint64_t key;     /* (min<<32)|max */
    uint32_t a, b;    /* endpoints */
    uint32_t count;   /* number of incident triangles */
} ec_edgekey_t;

static uint64_t ec_make_edge_key(uint32_t i, uint32_t j)
{
    uint32_t a = i < j ? i : j;
    uint32_t b = i < j ? j : i;
    return ((uint64_t)a << 32u) | (uint64_t)b;
}

static void ec_edgekey_sort(ec_edgekey_t *edges, uint32_t n, lod_scratch_t *sc)
{
    /* sort by key using the existing radix sorter by building key array + order */
    if (!scratch_ensure_v(sc, n)) return; /* reuses sc->keys/sc->order; safe */
    for (uint32_t i = 0; i < n; ++i)
    {
        sc->keys[i]  = edges[i].key;
        sc->order[i] = i;
    }
    radix_sort_u64_by_order(sc->keys, sc->order, sc->keys_tmp, sc->order_tmp, n);

    /* reorder edges into tmp_u32 as indices then permute in-place via a temp copy */
    /* We'll allocate a temp copy of edges (n) using tmp_u32 as raw bytes isn't safe; just malloc. */
    ec_edgekey_t *tmp = (ec_edgekey_t *)malloc((size_t)n * sizeof(ec_edgekey_t));
    if (!tmp) return;
    for (uint32_t i = 0; i < n; ++i) tmp[i] = edges[sc->order[i]];
    memcpy(edges, tmp, (size_t)n * sizeof(ec_edgekey_t));
    free(tmp);
}

typedef struct ec_heap_item_t
{
    uint32_t a, b;
    float cost;
} ec_heap_item_t;

typedef struct ec_heap_t
{
    ec_heap_item_t *items;
    uint32_t count;
    uint32_t cap;
} ec_heap_t;

static void ec_heap_free(ec_heap_t *h)
{
    free(h->items);
    memset(h, 0, sizeof(*h));
}

static int ec_heap_push(ec_heap_t *h, ec_heap_item_t it)
{
    if (h->count == h->cap)
    {
        uint32_t nc = h->cap ? h->cap * 2u : 256u;
        ec_heap_item_t *p = (ec_heap_item_t *)realloc(h->items, (size_t)nc * sizeof(ec_heap_item_t));
        if (!p) return 0;
        h->items = p;
        h->cap = nc;
    }
    uint32_t i = h->count++;
    h->items[i] = it;

    /* up-heap */
    while (i > 0u)
    {
        uint32_t p = (i - 1u) >> 1u;
        if (h->items[p].cost <= h->items[i].cost) break;
        ec_heap_item_t tmp = h->items[p];
        h->items[p] = h->items[i];
        h->items[i] = tmp;
        i = p;
    }
    return 1;
}

static int ec_heap_pop_min(ec_heap_t *h, ec_heap_item_t *out)
{
    if (h->count == 0u) return 0;
    *out = h->items[0];
    h->count--;
    if (h->count == 0u) return 1;
    h->items[0] = h->items[h->count];

    /* down-heap */
    uint32_t i = 0u;
    for (;;)
    {
        uint32_t l = i * 2u + 1u;
        uint32_t r = l + 1u;
        if (l >= h->count) break;

        uint32_t m = l;
        if (r < h->count && h->items[r].cost < h->items[l].cost) m = r;

        if (h->items[i].cost <= h->items[m].cost) break;

        ec_heap_item_t tmp = h->items[i];
        h->items[i] = h->items[m];
        h->items[m] = tmp;
        i = m;
    }
    return 1;
}

static float ec_edge_cost_len2(const model_vertex_t *v, uint32_t a, uint32_t b)
{
    float dx = v[b].px - v[a].px;
    float dy = v[b].py - v[a].py;
    float dz = v[b].pz - v[a].pz;
    return dx * dx + dy * dy + dz * dz;
}

static void ec_tri_normal_from_pos(const model_vertex_t *v, uint32_t i0, uint32_t i1, uint32_t i2, float *nx, float *ny, float *nz)
{
    float ax = v[i1].px - v[i0].px;
    float ay = v[i1].py - v[i0].py;
    float az = v[i1].pz - v[i0].pz;

    float bx = v[i2].px - v[i0].px;
    float by = v[i2].py - v[i0].py;
    float bz = v[i2].pz - v[i0].pz;

    v3_cross(ax, ay, az, bx, by, bz, nx, ny, nz);
    v3_norm(nx, ny, nz);
}

static int ec_tri_contains(const uint32_t *tri, uint32_t v)
{
    return tri[0] == v || tri[1] == v || tri[2] == v;
}

/* Build boundary-vertex flags: vertex is boundary if it touches any boundary edge. */
static int ec_build_boundary_flags(const uint32_t *idx, uint32_t tri_count, uint32_t vcount,
                                  uint8_t *is_boundary_v, lod_scratch_t *sc)
{
    memset(is_boundary_v, 0, (size_t)vcount * sizeof(uint8_t));

    /* emit all edges (3 per tri) */
    uint32_t ecount = tri_count * 3u;
    ec_edgekey_t *edges = (ec_edgekey_t *)malloc((size_t)ecount * sizeof(ec_edgekey_t));
    if (!edges) return 0;

    for (uint32_t t = 0; t < tri_count; ++t)
    {
        uint32_t i0 = idx[t * 3u + 0u];
        uint32_t i1 = idx[t * 3u + 1u];
        uint32_t i2 = idx[t * 3u + 2u];
        edges[t * 3u + 0u].key = ec_make_edge_key(i0, i1);
        edges[t * 3u + 1u].key = ec_make_edge_key(i1, i2);
        edges[t * 3u + 2u].key = ec_make_edge_key(i2, i0);
        edges[t * 3u + 0u].a = i0; edges[t * 3u + 0u].b = i1;
        edges[t * 3u + 1u].a = i1; edges[t * 3u + 1u].b = i2;
        edges[t * 3u + 2u].a = i2; edges[t * 3u + 2u].b = i0;
        edges[t * 3u + 0u].count = 1u;
        edges[t * 3u + 1u].count = 1u;
        edges[t * 3u + 2u].count = 1u;
    }

    ec_edgekey_sort(edges, ecount, sc);

    /* run-length count; boundary edges have count==1 */
    uint32_t run = 0u;
    while (run < ecount)
    {
        uint64_t k = edges[run].key;
        uint32_t end = run + 1u;
        while (end < ecount && edges[end].key == k) end++;

        if ((end - run) == 1u)
        {
            uint32_t a = (uint32_t)(k >> 32u);
            uint32_t b = (uint32_t)(k & 0xffffffffu);
            if (a < vcount) is_boundary_v[a] = 1u;
            if (b < vcount) is_boundary_v[b] = 1u;
        }

        run = end;
    }

    free(edges);
    return 1;
}

static int ec_build_incident_lists(const uint32_t *idx, uint32_t tri_count, uint32_t vcount,
                                  ec_incident_list_t *inc)
{
    for (uint32_t i = 0; i < vcount; ++i)
        memset(&inc[i], 0, sizeof(inc[i]));

    for (uint32_t t = 0; t < tri_count; ++t)
    {
        uint32_t i0 = idx[t * 3u + 0u];
        uint32_t i1 = idx[t * 3u + 1u];
        uint32_t i2 = idx[t * 3u + 2u];
        if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;
        if (!ec_incident_push(&inc[i0], t)) return 0;
        if (!ec_incident_push(&inc[i1], t)) return 0;
        if (!ec_incident_push(&inc[i2], t)) return 0;
    }
    return 1;
}

static void ec_free_incident_lists(ec_incident_list_t *inc, uint32_t vcount)
{
    if (!inc) return;
    for (uint32_t i = 0; i < vcount; ++i) free(inc[i].tris);
    free(inc);
}

/* Attempt a collapse (r -> k). Returns 1 if applied, 0 if rejected. */
static int ec_try_collapse(uint32_t k, uint32_t r,
                           model_vertex_t *v, uint32_t vcount,
                           uint32_t *idx, uint32_t tri_count,
                           uint8_t *v_alive, uint8_t *t_alive,
                           uint8_t *is_boundary_v,
                           ec_incident_list_t *inc,
                           float flip_threshold_dot)
{
    if (k >= vcount || r >= vcount) return 0;
    if (!v_alive[k] || !v_alive[r]) return 0;

    /* Boundary protection: don't collapse boundary vertices (simple & safe). */
    if (is_boundary_v[k] || is_boundary_v[r]) return 0;

    /* Proposed new vertex (midpoint + averaged attributes). */
    model_vertex_t newv = v[k];
    newv.px = 0.5f * (v[k].px + v[r].px);
    newv.py = 0.5f * (v[k].py + v[r].py);
    newv.pz = 0.5f * (v[k].pz + v[r].pz);

    newv.nx = 0.5f * (v[k].nx + v[r].nx);
    newv.ny = 0.5f * (v[k].ny + v[r].ny);
    newv.nz = 0.5f * (v[k].nz + v[r].nz);
    v3_norm(&newv.nx, &newv.ny, &newv.nz);

    newv.tx = 0.5f * (v[k].tx + v[r].tx);
    newv.ty = 0.5f * (v[k].ty + v[r].ty);
    newv.tz = 0.5f * (v[k].tz + v[r].tz);
    v3_norm(&newv.tx, &newv.ty, &newv.tz);

    newv.tw = (v[k].tw + v[r].tw) >= 0.0f ? 1.0f : -1.0f;
    newv.u  = 0.5f * (v[k].u + v[r].u);
    newv.v  = 0.5f * (v[k].v + v[r].v);

    /* Validate all triangles incident to r (and also those incident to k, because k moves). */
    ec_incident_list_t *Lr = &inc[r];
    ec_incident_list_t *Lk = &inc[k];

    /* Helper lambda-ish: validate triangle t with the proposed new vertex, and r->k remap. */
    for (uint32_t pass = 0; pass < 2u; ++pass)
    {
        ec_incident_list_t *L = (pass == 0u) ? Lr : Lk;
        for (uint32_t ii = 0; ii < L->count; ++ii)
        {
            uint32_t t = L->tris[ii];
            if (t >= tri_count) continue;
            if (!t_alive[t]) continue;

            uint32_t tri[3];
            tri[0] = idx[t * 3u + 0u];
            tri[1] = idx[t * 3u + 1u];
            tri[2] = idx[t * 3u + 2u];

            if (!ec_tri_contains(tri, r) && !ec_tri_contains(tri, k)) continue;

            /* Original normal */
            float onx, ony, onz;
            ec_tri_normal_from_pos(v, tri[0], tri[1], tri[2], &onx, &ony, &onz);

            /* Build remapped triangle */
            uint32_t ntri[3] = { tri[0], tri[1], tri[2] };
            for (uint32_t c = 0; c < 3u; ++c)
                if (ntri[c] == r) ntri[c] = k;

            /* Degenerate after collapse: allowed (it will be removed), no need to flip-check. */
            if (ntri[0] == ntri[1] || ntri[1] == ntri[2] || ntri[0] == ntri[2])
                continue;

            /* Compute new normal using "newv" for vertex k. */
            model_vertex_t saved_k = v[k];
            v[k] = newv;

            float nnx, nny, nnz;
            ec_tri_normal_from_pos(v, ntri[0], ntri[1], ntri[2], &nnx, &nny, &nnz);

            v[k] = saved_k;

            float d = v3_dot(onx, ony, onz, nnx, nny, nnz);
            if (d < flip_threshold_dot)
                return 0; /* reject collapse due to flip */
        }
    }

    /* Apply: move k to newv, kill r, update triangles incident to r and k. */
    v[k] = newv;
    v_alive[r] = 0u;

    /* Update triangles in Lr: replace r->k, drop degenerates, and add those tris to Lk for future queries. */
    for (uint32_t ii = 0; ii < Lr->count; ++ii)
    {
        uint32_t t = Lr->tris[ii];
        if (t >= tri_count) continue;
        if (!t_alive[t]) continue;

        uint32_t *tri = &idx[t * 3u];

        int touched = 0;
        for (uint32_t c = 0; c < 3u; ++c)
        {
            if (tri[c] == r)
            {
                tri[c] = k;
                touched = 1;
            }
        }
        if (!touched) continue;

        /* remove degenerates */
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2])
        {
            t_alive[t] = 0u;
            continue;
        }

        /* maintain incident list lazily: append to k's list */
        (void)ec_incident_push(Lk, t);
    }

    return 1;
}

static int build_lod_edge_collapse(const model_cpu_lod_t *src,
                                  float ratio,
                                  lod_scratch_t *sc,
                                  model_cpu_lod_t *out)
{
    memset(out, 0, sizeof(*out));

    if (!src || !src->vertices || !src->indices) return 0;
    if (src->index_count < 3u || src->vertex_count < 3u) return 0;

    uint32_t vcount = src->vertex_count;
    uint32_t tri_count = src->index_count / 3u;

    uint32_t target_tris = (uint32_t)floorf((float)tri_count * clamp01(ratio));
    if (target_tris < 1u) target_tris = 1u;

    /* Working copies */
    model_vertex_t *v = (model_vertex_t *)malloc((size_t)vcount * sizeof(model_vertex_t));
    uint32_t *idx = (uint32_t *)malloc((size_t)tri_count * 3u * sizeof(uint32_t));
    if (!v || !idx)
    {
        free(v); free(idx);
        return 0;
    }
    memcpy(v, src->vertices, (size_t)vcount * sizeof(model_vertex_t));
    memcpy(idx, src->indices, (size_t)tri_count * 3u * sizeof(uint32_t));

    /* Alive flags */
    if (!scratch_ensure_tmp_u8(sc, (uint32_t)(vcount + tri_count + vcount))) /* v_alive + t_alive + boundary */
    {
        free(v); free(idx);
        return 0;
    }
    uint8_t *v_alive = sc->tmp_u8;
    uint8_t *t_alive = sc->tmp_u8 + vcount;
    uint8_t *is_boundary_v = sc->tmp_u8 + vcount + tri_count;

    memset(v_alive, 1, (size_t)vcount * sizeof(uint8_t));
    memset(t_alive, 1, (size_t)tri_count * sizeof(uint8_t));

    if (!ec_build_boundary_flags(idx, tri_count, vcount, is_boundary_v, sc))
    {
        free(v); free(idx);
        return 0;
    }

    /* Incident lists */
    ec_incident_list_t *inc = (ec_incident_list_t *)calloc((size_t)vcount, sizeof(ec_incident_list_t));
    if (!inc)
    {
        free(v); free(idx);
        return 0;
    }
    if (!ec_build_incident_lists(idx, tri_count, vcount, inc))
    {
        ec_free_incident_lists(inc, vcount);
        free(v); free(idx);
        return 0;
    }

    /* Seed heap with all edges from all alive triangles. */
    ec_heap_t heap;
    memset(&heap, 0, sizeof(heap));

    for (uint32_t t = 0; t < tri_count; ++t)
    {
        if (!t_alive[t]) continue;
        uint32_t i0 = idx[t * 3u + 0u];
        uint32_t i1 = idx[t * 3u + 1u];
        uint32_t i2 = idx[t * 3u + 2u];
        if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;

        /* push 3 directed candidates (we'll decide keep/remove on pop) */
        ec_heap_item_t e01 = { i0, i1, ec_edge_cost_len2(v, i0, i1) };
        ec_heap_item_t e12 = { i1, i2, ec_edge_cost_len2(v, i1, i2) };
        ec_heap_item_t e20 = { i2, i0, ec_edge_cost_len2(v, i2, i0) };
        if (!ec_heap_push(&heap, e01) || !ec_heap_push(&heap, e12) || !ec_heap_push(&heap, e20))
        {
            ec_heap_free(&heap);
            ec_free_incident_lists(inc, vcount);
            free(v); free(idx);
            return 0;
        }
    }

    /* Collapse loop */
    uint32_t alive_tris = tri_count;
    const float flip_threshold_dot = 0.0f; /* 0 = forbid >90deg flips; raise to 0.2 for stricter */

    /* Helper: recompute alive triangle count occasionally (lazy) */
    uint32_t step = 0u;

    while (alive_tris > target_tris)
    {
        ec_heap_item_t it;
        if (!ec_heap_pop_min(&heap, &it))
            break;

        uint32_t a = it.a;
        uint32_t b = it.b;
        if (a >= vcount || b >= vcount) continue;
        if (!v_alive[a] || !v_alive[b]) continue;
        if (a == b) continue;

        /* boundary protection: handled inside try_collapse as vertex-based */
        /* choose keep vertex: keep the one with higher incident count (heuristic) */
        uint32_t ka = a, rb = b;
        if (inc[b].count > inc[a].count) { ka = b; rb = a; }

        if (!ec_try_collapse(ka, rb, v, vcount, idx, tri_count, v_alive, t_alive, is_boundary_v, inc, flip_threshold_dot))
            continue;

        /* after a successful collapse, push some fresh local edges from ka's incident tris */
        ec_incident_list_t *Lk = &inc[ka];
        for (uint32_t ii = 0; ii < Lk->count; ++ii)
        {
            uint32_t t = Lk->tris[ii];
            if (t >= tri_count) continue;
            if (!t_alive[t]) continue;

            uint32_t i0 = idx[t * 3u + 0u];
            uint32_t i1 = idx[t * 3u + 1u];
            uint32_t i2 = idx[t * 3u + 2u];

            if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;
            if (!v_alive[i0] || !v_alive[i1] || !v_alive[i2]) continue;

            ec_heap_item_t e01 = { i0, i1, ec_edge_cost_len2(v, i0, i1) };
            ec_heap_item_t e12 = { i1, i2, ec_edge_cost_len2(v, i1, i2) };
            ec_heap_item_t e20 = { i2, i0, ec_edge_cost_len2(v, i2, i0) };
            (void)ec_heap_push(&heap, e01);
            (void)ec_heap_push(&heap, e12);
            (void)ec_heap_push(&heap, e20);
        }

        /* lazy alive tris recount every so often */
        step++;
        if ((step & 0x3ffu) == 0u)
        {
            uint32_t c = 0u;
            for (uint32_t t = 0; t < tri_count; ++t) if (t_alive[t]) c++;
            alive_tris = c;
        }
    }

    /* Final alive tris count */
    {
        uint32_t c = 0u;
        for (uint32_t t = 0; t < tri_count; ++t) if (t_alive[t]) c++;
        alive_tris = c;
    }

    /* Compact vertices: build remap old->new for alive vertices actually referenced by alive triangles */
    if (!scratch_ensure_tmp_u32(sc, vcount))
    {
        ec_heap_free(&heap);
        ec_free_incident_lists(inc, vcount);
        free(v); free(idx);
        return 0;
    }
    uint32_t *remap = sc->tmp_u32;
    for (uint32_t i = 0; i < vcount; ++i) remap[i] = 0xffffffffu;

    uint32_t used_count = 0u;
    for (uint32_t t = 0; t < tri_count; ++t)
    {
        if (!t_alive[t]) continue;
        for (uint32_t c = 0; c < 3u; ++c)
        {
            uint32_t vi = idx[t * 3u + c];
            if (vi >= vcount) continue;
            if (!v_alive[vi]) continue;
            if (remap[vi] == 0xffffffffu)
                remap[vi] = used_count++;
        }
    }

    if (alive_tris < 1u || used_count < 3u)
    {
        ec_heap_free(&heap);
        ec_free_incident_lists(inc, vcount);
        free(v); free(idx);
        return 0;
    }

    out->vertex_count = used_count;
    out->index_count  = alive_tris * 3u;
    out->vertices = (model_vertex_t *)malloc((size_t)used_count * sizeof(model_vertex_t));
    out->indices  = (uint32_t *)malloc((size_t)out->index_count * sizeof(uint32_t));
    if (!out->vertices || !out->indices)
    {
        free(out->vertices);
        free(out->indices);
        memset(out, 0, sizeof(*out));

        ec_heap_free(&heap);
        ec_free_incident_lists(inc, vcount);
        free(v); free(idx);
        return 0;
    }

    for (uint32_t i = 0; i < vcount; ++i)
    {
        uint32_t ni = remap[i];
        if (ni != 0xffffffffu)
            out->vertices[ni] = v[i];
    }

    uint32_t w = 0u;
    for (uint32_t t = 0; t < tri_count; ++t)
    {
        if (!t_alive[t]) continue;

        uint32_t a = idx[t * 3u + 0u];
        uint32_t b = idx[t * 3u + 1u];
        uint32_t c = idx[t * 3u + 2u];

        uint32_t na = (a < vcount) ? remap[a] : 0xffffffffu;
        uint32_t nb = (b < vcount) ? remap[b] : 0xffffffffu;
        uint32_t nc = (c < vcount) ? remap[c] : 0xffffffffu;

        if (na == 0xffffffffu || nb == 0xffffffffu || nc == 0xffffffffu) continue;
        if (na == nb || nb == nc || na == nc) continue;

        out->indices[w++] = na;
        out->indices[w++] = nb;
        out->indices[w++] = nc;
    }

    out->index_count = w;
    if (out->index_count < 3u)
    {
        free(out->vertices);
        free(out->indices);
        memset(out, 0, sizeof(*out));

        ec_heap_free(&heap);
        ec_free_incident_lists(inc, vcount);
        free(v); free(idx);
        return 0;
    }

    ec_heap_free(&heap);
    ec_free_incident_lists(inc, vcount);
    free(v);
    free(idx);
    return 1;
}

/* -------------------------- Common cleanup helpers -------------------------- */

static void cpu_lod_free(model_cpu_lod_t *l)
{
    if (!l) return;
    free(l->vertices);
    free(l->indices);
    memset(l, 0, sizeof(*l));
}

/* -------------------------- Public entry point -------------------------- */

bool model_raw_generate_lods(model_raw_t *raw, const model_lod_settings_t *s)
{
    clock_t t0 = clock();

    if (!raw || !s) return false;

    uint8_t lod_count = clamp_lod_count(s->lod_count);
    raw->lod_count = lod_count;

    bool ok_all = true;

    lod_scratch_t sc;
    memset(&sc, 0, sizeof(sc));

    for (uint32_t smi = 0; smi < raw->submeshes.size; ++smi)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&raw->submeshes, smi);
        if (!sm) { ok_all = false; continue; }
        if (sm->lods.size == 0) { ok_all = false; continue; }

        /* keep only LOD0 in the vector, free existing others */
        while (sm->lods.size > 1)
        {
            model_cpu_lod_t *l = (model_cpu_lod_t *)vector_impl_at(&sm->lods, sm->lods.size - 1);
            cpu_lod_free(l);
            sm->lods.size -= 1;
        }

        model_cpu_lod_t *lod0 = (model_cpu_lod_t *)vector_impl_at(&sm->lods, 0);
        if (!lod0 || !lod0->vertices || !lod0->indices || lod0->vertex_count == 0 || lod0->index_count < 3)
        {
            ok_all = false;
            continue;
        }

        /* bounds for clustering LODs */
        float minx = lod0->vertices[0].px, miny = lod0->vertices[0].py, minz = lod0->vertices[0].pz;
        float maxx = minx, maxy = miny, maxz = minz;

        for (uint32_t i = 1; i < lod0->vertex_count; ++i)
        {
            const model_vertex_t *v = &lod0->vertices[i];
            if (v->px < minx) minx = v->px;
            if (v->py < miny) miny = v->py;
            if (v->pz < minz) minz = v->pz;
            if (v->px > maxx) maxx = v->px;
            if (v->py > maxy) maxy = v->py;
            if (v->pz > maxz) maxz = v->pz;
        }

        float ex = maxx - minx;
        float ey = maxy - miny;
        float ez = maxz - minz;

        float eps = 1e-6f;
        if (ex < eps) ex = eps;
        if (ey < eps) ey = eps;
        if (ez < eps) ez = eps;

        float extent = ex;
        if (ey > extent) extent = ey;
        if (ez > extent) extent = ez;
        if (extent < 1e-6f) extent = 1e-6f;

        /* used only for your old "min px" filter; we keep it conservative */
        float fov_y = 60.0f * 3.1415926535f / 180.0f;
        float screen_h = 1080.0f;
        float ref_distance = extent * 6.0f;
        if (ref_distance < 1e-3f) ref_distance = 1e-3f;

        float px_per_world_at_ref = (screen_h * 0.5f) / tanf(fov_y * 0.5f);
        px_per_world_at_ref = px_per_world_at_ref / ref_distance;

        for (uint8_t li = 1; li < lod_count; ++li)
        {
            float ratio = s->triangle_ratio[li];
            if (ratio <= 0.0f) ratio = 0.5f;
            ratio = clamp01(ratio);

            const uint8_t last_lod = (uint8_t)(lod_count - 1u);

            model_cpu_lod_t out;
            int ok = 0;

            if (li == last_lod)
            {
                /* LAST LOD: use edge-collapse (no triangle "growth", no random holes if boundaries protected). */
                float rlast = ratio;
                /* if user passes something tiny, clamp to avoid collapsing everything */
                if (rlast < 0.01f) rlast = 0.01f;

                ok = build_lod_edge_collapse(lod0, rlast, &sc, &out);
            }
            else
            {
                /* Intermediate LODs: keep your fast clustering. */
                uint32_t res = lod_res_from_ratio(ratio);
                if (ratio <= 0.08f) res = u32_min(res, 72u);
                if (ratio <= 0.04f) res = u32_min(res, 56u);
                if (ratio <= 0.02f) res = u32_min(res, 40u);
                if (res < 8u) res = 8u;
                if (res > 512u) res = 512u;

                float cell = extent / (float)res;
                if (cell < 1e-6f) cell = 1e-6f;
                float inv_cell = 1.0f / cell;

                /* Don't do aggressive triangle dropping here; it can open holes. */
                int enforce_min_px = 0;
                float min_len2_threshold = 0.0f;

                /* If you want a tiny degenerate filter for mid LODs, use ~0.25 px: */
                /* enforce_min_px = 1;
                   min_len2_threshold = min_len2_threshold_from_min_px(0.25f, px_per_world_at_ref); */

                ok = build_lod_cluster_fast(lod0, ratio, res,
                                            enforce_min_px, min_len2_threshold,
                                            minx, miny, minz, inv_cell, &sc, &out);
            }

            if (!ok)
            {
                ok_all = false;
                continue;
            }

            vector_impl_push_back(&sm->lods, &out);
        }
    }

    scratch_free(&sc);

    clock_t t1 = clock();
    double secs = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    LOG_OK("model_raw_generate_lods took %.6f seconds", secs);

    return ok_all;
}
