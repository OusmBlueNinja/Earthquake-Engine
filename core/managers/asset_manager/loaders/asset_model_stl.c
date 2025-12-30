#include "asset_model_stl.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include <float.h>
#include <math.h>

#include "asset_manager/asset_types/model.h"
#include "asset_manager/asset_types/material.h"
#include "vector.h"
#include "systems/model_lod.h"
#include "types/vec3.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static void mdl_vao_setup_model_vertex(void)
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(model_vertex_t), (void *)offsetof(model_vertex_t, px));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(model_vertex_t), (void *)offsetof(model_vertex_t, nx));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(model_vertex_t), (void *)offsetof(model_vertex_t, u));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(model_vertex_t), (void *)offsetof(model_vertex_t, tx));
}

static float stl_fabsf(float x) { return x < 0.0f ? -x : x; }

static vec3 stl_vec3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }

static vec3 stl_vec3_cross(vec3 a, vec3 b)
{
    return (vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static float stl_vec3_len2(vec3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

static vec3 stl_vec3_norm_safe(vec3 v)
{
    float l2 = stl_vec3_len2(v);
    if (l2 < 1e-20f)
        return (vec3){0.0f, 0.0f, 1.0f};
    float inv = 1.0f / sqrtf(l2);
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
}

static aabb_t stl_aabb_from_vertices(const model_vertex_t *vtx, uint32_t vcount)
{
    aabb_t b;
    b.min = (vec3){FLT_MAX, FLT_MAX, FLT_MAX};
    b.max = (vec3){-FLT_MAX, -FLT_MAX, -FLT_MAX};

    if (!vtx || vcount == 0)
    {
        b.min = (vec3){0, 0, 0};
        b.max = (vec3){0, 0, 0};
        return b;
    }

    for (uint32_t i = 0; i < vcount; ++i)
    {
        vec3 p = (vec3){vtx[i].px, vtx[i].py, vtx[i].pz};

        if (p.x < b.min.x)
            b.min.x = p.x;
        if (p.y < b.min.y)
            b.min.y = p.y;
        if (p.z < b.min.z)
            b.min.z = p.z;

        if (p.x > b.max.x)
            b.max.x = p.x;
        if (p.y > b.max.y)
            b.max.y = p.y;
        if (p.z > b.max.z)
            b.max.z = p.z;
    }

    if (b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z)
    {
        b.min = (vec3){0, 0, 0};
        b.max = (vec3){0, 0, 0};
    }

    return b;
}

static int stl_read_u32_le(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
        return 0;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int stl_read_f32_le(FILE *f, float *out)
{
    uint32_t u = 0;
    if (!stl_read_u32_le(f, &u))
        return 0;
    union
    {
        uint32_t u;
        float f;
    } v;
    v.u = u;
    *out = v.f;
    return 1;
}

static int stl_try_binary_size(FILE *f, uint32_t *tri_count_out)
{
    if (!f || !tri_count_out)
        return 0;

    long cur = ftell(f);
    if (cur < 0)
        cur = 0;

    if (fseek(f, 0, SEEK_END) != 0)
        return 0;
    long end = ftell(f);
    if (end < 0)
        return 0;

    if (fseek(f, 80, SEEK_SET) != 0)
        return 0;

    uint32_t tri = 0;
    if (!stl_read_u32_le(f, &tri))
        return 0;

    long expect = 84L + (long)tri * 50L;
    if (expect != end)
    {
        if (fseek(f, cur, SEEK_SET) == 0)
        {
        }
        return 0;
    }

    *tri_count_out = tri;

    if (fseek(f, cur, SEEK_SET) == 0)
    {
    }
    return 1;
}

static int stl_quick_verify(const char *path)
{
    if (!path)
        return 0;

    const char *ext = strrchr(path, '.');
    if (ext && (!strcmp(ext, ".stl") || !strcmp(ext, ".STL")))
        return 1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint8_t head[6];
    size_t n = fread(head, 1, 6, f);
    fclose(f);
    if (n < 5)
        return 0;

    if ((head[0] == 's' || head[0] == 'S') &&
        (head[1] == 'o' || head[1] == 'O') &&
        (head[2] == 'l' || head[2] == 'L') &&
        (head[3] == 'i' || head[3] == 'I') &&
        (head[4] == 'd' || head[4] == 'D'))
        return 1;

    return 1;
}

static int stl_load_binary(FILE *f, model_raw_t *raw, ihandle_t material)
{
    if (fseek(f, 0, SEEK_SET) != 0)
        return 0;

    uint8_t header[80];
    if (fread(header, 1, 80, f) != 80)
        return 0;

    uint32_t tri = 0;
    if (!stl_read_u32_le(f, &tri))
        return 0;
    if (!tri)
        return 0;

    uint32_t vcount = tri * 3;
    uint32_t icount = tri * 3;

    model_vertex_t *vtx = (model_vertex_t *)malloc((size_t)vcount * sizeof(model_vertex_t));
    uint32_t *idx = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!vtx || !idx)
    {
        free(vtx);
        free(idx);
        return 0;
    }

    uint32_t wv = 0;
    for (uint32_t t = 0; t < tri; ++t)
    {
        float nx = 0, ny = 0, nz = 1;
        float v0x = 0, v0y = 0, v0z = 0;
        float v1x = 0, v1y = 0, v1z = 0;
        float v2x = 0, v2y = 0, v2z = 0;

        if (!stl_read_f32_le(f, &nx) || !stl_read_f32_le(f, &ny) || !stl_read_f32_le(f, &nz))
            break;
        if (!stl_read_f32_le(f, &v0x) || !stl_read_f32_le(f, &v0y) || !stl_read_f32_le(f, &v0z))
            break;
        if (!stl_read_f32_le(f, &v1x) || !stl_read_f32_le(f, &v1y) || !stl_read_f32_le(f, &v1z))
            break;
        if (!stl_read_f32_le(f, &v2x) || !stl_read_f32_le(f, &v2y) || !stl_read_f32_le(f, &v2z))
            break;

        uint8_t attr[2];
        if (fread(attr, 1, 2, f) != 2)
            break;

        vec3 p0 = (vec3){v0x, v0y, v0z};
        vec3 p1 = (vec3){v1x, v1y, v1z};
        vec3 p2 = (vec3){v2x, v2y, v2z};

        vec3 n = (vec3){nx, ny, nz};
        if (stl_vec3_len2(n) < 1e-20f)
        {
            vec3 e1 = stl_vec3_sub(p1, p0);
            vec3 e2 = stl_vec3_sub(p2, p0);
            n = stl_vec3_cross(e1, e2);
        }
        n = stl_vec3_norm_safe(n);

        model_vertex_t a, b, c;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        memset(&c, 0, sizeof(c));

        a.px = p0.x;
        a.py = p0.y;
        a.pz = p0.z;
        b.px = p1.x;
        b.py = p1.y;
        b.pz = p1.z;
        c.px = p2.x;
        c.py = p2.y;
        c.pz = p2.z;

        a.nx = n.x;
        a.ny = n.y;
        a.nz = n.z;
        b.nx = n.x;
        b.ny = n.y;
        b.nz = n.z;
        c.nx = n.x;
        c.ny = n.y;
        c.nz = n.z;

        a.u = 0.0f;
        a.v = 0.0f;
        b.u = 0.0f;
        b.v = 0.0f;
        c.u = 0.0f;
        c.v = 0.0f;

        a.tx = 1.0f;
        a.ty = 0.0f;
        a.tz = 0.0f;
        a.tw = 1.0f;
        b.tx = 1.0f;
        b.ty = 0.0f;
        b.tz = 0.0f;
        b.tw = 1.0f;
        c.tx = 1.0f;
        c.ty = 0.0f;
        c.tz = 0.0f;
        c.tw = 1.0f;

        vtx[wv + 0] = a;
        vtx[wv + 1] = b;
        vtx[wv + 2] = c;

        idx[wv + 0] = wv + 0;
        idx[wv + 1] = wv + 1;
        idx[wv + 2] = wv + 2;

        wv += 3;
    }

    if (wv == 0)
    {
        free(vtx);
        free(idx);
        return 0;
    }

    model_cpu_lod_t lod0;
    memset(&lod0, 0, sizeof(lod0));
    lod0.vertices = vtx;
    lod0.vertex_count = wv;
    lod0.indices = idx;
    lod0.index_count = wv;

    model_cpu_submesh_t sm;
    memset(&sm, 0, sizeof(sm));
    sm.lods = vector_impl_create_vector(sizeof(model_cpu_lod_t));
    vector_impl_push_back(&sm.lods, &lod0);
    sm.material = material;
    sm.material_name = NULL;

    sm.aabb = stl_aabb_from_vertices(vtx, wv);
    sm.flags = (uint8_t)(sm.flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);

    vector_impl_push_back(&raw->submeshes, &sm);

    return 1;
}

static int stl_scan_float(const char *s, float *out)
{
    if (!s || !out)
        return 0;

    while (*s && isspace((unsigned char)*s))
        ++s;

    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s)
        return 0;
    *out = v;
    return 1;
}

static const char *stl_find_token_ci(const char *p, const char *tok)
{
    if (!p || !tok || !tok[0])
        return NULL;
    size_t n = strlen(tok);
    for (; *p; ++p)
    {
        size_t i = 0;
        for (; i < n; ++i)
        {
            char a = p[i];
            if (!a)
                return NULL;
            char b = tok[i];
            if (tolower((unsigned char)a) != tolower((unsigned char)b))
                break;
        }
        if (i == n)
            return p;
    }
    return NULL;
}

static int stl_load_ascii(FILE *f, model_raw_t *raw, ihandle_t material)
{
    if (fseek(f, 0, SEEK_END) != 0)
        return 0;
    long sz = ftell(f);
    if (sz <= 0)
        return 0;
    if (fseek(f, 0, SEEK_SET) != 0)
        return 0;

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf)
        return 0;

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
    {
        free(buf);
        return 0;
    }
    buf[sz] = 0;

    uint32_t cap_tri = 1024;
    uint32_t tri = 0;

    model_vertex_t *vtx = (model_vertex_t *)malloc((size_t)cap_tri * 3u * sizeof(model_vertex_t));
    uint32_t *idx = (uint32_t *)malloc((size_t)cap_tri * 3u * sizeof(uint32_t));
    if (!vtx || !idx)
    {
        free(buf);
        free(vtx);
        free(idx);
        return 0;
    }

    const char *p = buf;
    while (1)
    {
        const char *fn = stl_find_token_ci(p, "facet normal");
        if (!fn)
            break;

        float nx = 0, ny = 0, nz = 1;
        const char *q = fn + strlen("facet normal");
        if (!stl_scan_float(q, &nx))
        {
            p = fn + 1;
            continue;
        }
        while (*q && !isspace((unsigned char)*q))
            ++q;
        if (!stl_scan_float(q, &ny))
        {
            p = fn + 1;
            continue;
        }
        while (*q && !isspace((unsigned char)*q))
            ++q;
        if (!stl_scan_float(q, &nz))
        {
            p = fn + 1;
            continue;
        }

        const char *v0t = stl_find_token_ci(fn, "vertex");
        if (!v0t)
        {
            p = fn + 1;
            continue;
        }
        const char *v1t = stl_find_token_ci(v0t + 6, "vertex");
        if (!v1t)
        {
            p = fn + 1;
            continue;
        }
        const char *v2t = stl_find_token_ci(v1t + 6, "vertex");
        if (!v2t)
        {
            p = fn + 1;
            continue;
        }

        float v0x = 0, v0y = 0, v0z = 0;
        float v1x = 0, v1y = 0, v1z = 0;
        float v2x = 0, v2y = 0, v2z = 0;

        const char *a0 = v0t + strlen("vertex");
        const char *a1 = v1t + strlen("vertex");
        const char *a2 = v2t + strlen("vertex");

        if (!stl_scan_float(a0, &v0x))
        {
            p = fn + 1;
            continue;
        }
        if (!stl_scan_float(strchr(a0, ' ') ? strchr(a0, ' ') : a0, &v0y))
        {
            p = fn + 1;
            continue;
        }
        const char *t0 = strchr(a0, ' ');
        if (t0)
            t0 = strchr(t0 + 1, ' ');
        if (!stl_scan_float(t0 ? t0 : a0, &v0z))
        {
            p = fn + 1;
            continue;
        }

        if (!stl_scan_float(a1, &v1x))
        {
            p = fn + 1;
            continue;
        }
        if (!stl_scan_float(strchr(a1, ' ') ? strchr(a1, ' ') : a1, &v1y))
        {
            p = fn + 1;
            continue;
        }
        const char *t1 = strchr(a1, ' ');
        if (t1)
            t1 = strchr(t1 + 1, ' ');
        if (!stl_scan_float(t1 ? t1 : a1, &v1z))
        {
            p = fn + 1;
            continue;
        }

        if (!stl_scan_float(a2, &v2x))
        {
            p = fn + 1;
            continue;
        }
        if (!stl_scan_float(strchr(a2, ' ') ? strchr(a2, ' ') : a2, &v2y))
        {
            p = fn + 1;
            continue;
        }
        const char *t2 = strchr(a2, ' ');
        if (t2)
            t2 = strchr(t2 + 1, ' ');
        if (!stl_scan_float(t2 ? t2 : a2, &v2z))
        {
            p = fn + 1;
            continue;
        }

        if (tri + 1 > cap_tri)
        {
            uint32_t new_cap = cap_tri * 2u;
            model_vertex_t *nv = (model_vertex_t *)realloc(vtx, (size_t)new_cap * 3u * sizeof(model_vertex_t));
            uint32_t *ni = (uint32_t *)realloc(idx, (size_t)new_cap * 3u * sizeof(uint32_t));
            if (!nv || !ni)
            {
                free(nv);
                free(ni);
                free(buf);
                free(vtx);
                free(idx);
                return 0;
            }
            vtx = nv;
            idx = ni;
            cap_tri = new_cap;
        }

        vec3 p0 = (vec3){v0x, v0y, v0z};
        vec3 p1 = (vec3){v1x, v1y, v1z};
        vec3 p2 = (vec3){v2x, v2y, v2z};

        vec3 n = (vec3){nx, ny, nz};
        if (stl_vec3_len2(n) < 1e-20f)
        {
            vec3 e1 = stl_vec3_sub(p1, p0);
            vec3 e2 = stl_vec3_sub(p2, p0);
            n = stl_vec3_cross(e1, e2);
        }
        n = stl_vec3_norm_safe(n);

        uint32_t wv = tri * 3u;

        model_vertex_t va, vb, vc;
        memset(&va, 0, sizeof(va));
        memset(&vb, 0, sizeof(vb));
        memset(&vc, 0, sizeof(vc));

        va.px = p0.x;
        va.py = p0.y;
        va.pz = p0.z;
        vb.px = p1.x;
        vb.py = p1.y;
        vb.pz = p1.z;
        vc.px = p2.x;
        vc.py = p2.y;
        vc.pz = p2.z;

        va.nx = n.x;
        va.ny = n.y;
        va.nz = n.z;
        vb.nx = n.x;
        vb.ny = n.y;
        vb.nz = n.z;
        vc.nx = n.x;
        vc.ny = n.y;
        vc.nz = n.z;

        va.u = 0.0f;
        va.v = 0.0f;
        vb.u = 0.0f;
        vb.v = 0.0f;
        vc.u = 0.0f;
        vc.v = 0.0f;

        va.tx = 1.0f;
        va.ty = 0.0f;
        va.tz = 0.0f;
        va.tw = 1.0f;
        vb.tx = 1.0f;
        vb.ty = 0.0f;
        vb.tz = 0.0f;
        vb.tw = 1.0f;
        vc.tx = 1.0f;
        vc.ty = 0.0f;
        vc.tz = 0.0f;
        vc.tw = 1.0f;

        vtx[wv + 0] = va;
        vtx[wv + 1] = vb;
        vtx[wv + 2] = vc;

        idx[wv + 0] = wv + 0;
        idx[wv + 1] = wv + 1;
        idx[wv + 2] = wv + 2;

        tri++;

        p = v2t + 6;
    }

    free(buf);

    if (tri == 0)
    {
        free(vtx);
        free(idx);
        return 0;
    }

    uint32_t vcount = tri * 3u;
    uint32_t icount = tri * 3u;

    model_vertex_t *rv = (model_vertex_t *)realloc(vtx, (size_t)vcount * sizeof(model_vertex_t));
    uint32_t *ri = (uint32_t *)realloc(idx, (size_t)icount * sizeof(uint32_t));
    if (rv)
        vtx = rv;
    if (ri)
        idx = ri;

    model_cpu_lod_t lod0;
    memset(&lod0, 0, sizeof(lod0));
    lod0.vertices = vtx;
    lod0.vertex_count = vcount;
    lod0.indices = idx;
    lod0.index_count = icount;

    model_cpu_submesh_t sm;
    memset(&sm, 0, sizeof(sm));
    sm.lods = vector_impl_create_vector(sizeof(model_cpu_lod_t));
    vector_impl_push_back(&sm.lods, &lod0);
    sm.material = material;
    sm.material_name = NULL;

    sm.aabb = stl_aabb_from_vertices(vtx, vcount);
    sm.flags = (uint8_t)(sm.flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);

    vector_impl_push_back(&raw->submeshes, &sm);

    return 1;
}

static void stl_free_raw(model_raw_t *raw)
{
    model_raw_destroy(raw);
}

bool asset_model_stl_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, ihandle_t *out_handle)
{
    if (out_handle)
        *out_handle = ihandle_invalid();

    if (!am || !out_asset || !path)
        return false;
    if (path_is_ptr)
        return false;

    if (!stl_quick_verify(path))
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    asset_material_t cur = material_make_default(0);
    ihandle_t mat = asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);

    model_raw_t raw = model_raw_make();
    raw.mtllib_path = NULL;
    raw.mtllib = ihandle_invalid();

    int ok = 0;

    uint32_t tri_count = 0;
    if (stl_try_binary_size(f, &tri_count))
        ok = stl_load_binary(f, &raw, mat);
    else
        ok = stl_load_ascii(f, &raw, mat);

    fclose(f);

    if (!ok)
    {
        stl_free_raw(&raw);
        return false;
    }

    model_raw_generate_lods(&raw);

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;
    return true;
}

bool asset_model_stl_init(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MODEL)
        return false;

    asset_model_t model = asset_model_make();

    for (uint32_t i = 0; i < asset->as.model_raw.submeshes.size; ++i)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&asset->as.model_raw.submeshes, i);
        if (!sm || sm->lods.size == 0)
            continue;

        mesh_t gm;
        memset(&gm, 0, sizeof(gm));
        gm.material = sm->material;
        gm.lods = vector_impl_create_vector(sizeof(mesh_lod_t));
        gm.flags = 0;

        mesh_set_local_aabb_from_cpu(&gm, sm);

        uint32_t want_lods = sm->lods.size;
        uint32_t uploaded = 0;

        for (uint32_t li = 0; li < sm->lods.size; ++li)
        {
            model_cpu_lod_t *cl = (model_cpu_lod_t *)vector_impl_at(&sm->lods, li);
            if (!cl || !cl->vertices || !cl->indices || !cl->vertex_count || !cl->index_count)
                continue;

            mesh_lod_t glod;
            memset(&glod, 0, sizeof(glod));
            glod.index_count = cl->index_count;

            glGenVertexArrays(1, &glod.vao);
            glBindVertexArray(glod.vao);

            glGenBuffers(1, &glod.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, glod.vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(cl->vertex_count * sizeof(model_vertex_t)), cl->vertices, GL_STATIC_DRAW);

            glGenBuffers(1, &glod.ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glod.ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cl->index_count * sizeof(uint32_t)), cl->indices, GL_STATIC_DRAW);

            mdl_vao_setup_model_vertex();

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            vector_impl_push_back(&gm.lods, &glod);
            uploaded++;

            if (li == 0)
                gm.flags |= (uint8_t)MESH_FLAG_LOD0_READY;
        }

        if (uploaded > 0)
        {
            if (uploaded == want_lods)
                gm.flags |= (uint8_t)MESH_FLAG_LODS_READY;
            vector_impl_push_back(&model.meshes, &gm);
        }
        else
        {
            vector_impl_free(&gm.lods);
        }
    }

    model_raw_destroy(&asset->as.model_raw);
    asset->as.model = model;

    return true;
}

void asset_model_stl_cleanup(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_MODEL)
        return;

    if (asset->state != ASSET_STATE_READY)
        model_raw_destroy(&asset->as.model_raw);

    for (uint32_t i = 0; i < asset->as.model.meshes.size; ++i)
    {
        mesh_t *m = (mesh_t *)vector_impl_at(&asset->as.model.meshes, i);
        if (!m)
            continue;

        for (uint32_t li = 0; li < m->lods.size; ++li)
        {
            mesh_lod_t *l = (mesh_lod_t *)vector_impl_at(&m->lods, li);
            if (!l)
                continue;
            if (l->ibo)
                glDeleteBuffers(1, &l->ibo);
            if (l->vbo)
                glDeleteBuffers(1, &l->vbo);
            if (l->vao)
                glDeleteVertexArrays(1, &l->vao);
            memset(l, 0, sizeof(*l));
        }

        vector_impl_free(&m->lods);
        m->material = ihandle_invalid();
        m->flags = 0;
        m->local_aabb.min = (vec3){0, 0, 0};
        m->local_aabb.max = (vec3){0, 0, 0};
    }

    asset_model_destroy_cpu_only(&asset->as.model);
}

static bool asset_model_stl_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;
    if (!path || path_is_ptr)
        return false;

    const char *ext = strrchr(path, '.');
    if (!ext)
        return false;

    return (!strcmp(ext, ".stl") || !strcmp(ext, ".STL"));
}

asset_module_desc_t asset_module_model_stl(void)
{
    asset_module_desc_t m = {0};
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL_STL";
    m.load_fn = asset_model_stl_load;
    m.init_fn = asset_model_stl_init;
    m.cleanup_fn = asset_model_stl_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    m.can_load_fn = asset_model_stl_can_load;
    return m;
}
