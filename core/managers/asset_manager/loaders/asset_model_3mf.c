/* asset_model_3mf.c */
#include "asset_model_3mf.h"

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

#include "miniz.h"

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

static vec3 mf_vec3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }

static vec3 mf_vec3_cross(vec3 a, vec3 b)
{
    return (vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static float mf_vec3_len2(vec3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

static vec3 mf_vec3_norm_safe(vec3 v)
{
    float l2 = mf_vec3_len2(v);
    if (l2 < 1e-20f)
        return (vec3){0.0f, 0.0f, 1.0f};
    float inv = 1.0f / sqrtf(l2);
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
}

static aabb_t mf_aabb_from_vertices(const model_vertex_t *vtx, uint32_t vcount)
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

typedef struct mf_v3_t
{
    float x, y, z;
} mf_v3_t;

static const char *mf_skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p))
        ++p;
    return p;
}

static int mf_match_ci_n(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (!ca || !cb)
            return 0;
        if (tolower(ca) != tolower(cb))
            return 0;
    }
    return 1;
}

static const char *mf_find_ci(const char *p, const char *tok)
{
    if (!p || !tok || !tok[0])
        return NULL;
    size_t n = strlen(tok);
    for (; *p; ++p)
        if (mf_match_ci_n(p, tok, n))
            return p;
    return NULL;
}

static int mf_read_attr_f32(const char *tag, const char *name, float *out)
{
    if (!tag || !name || !out)
        return 0;

    char pat[64];
    size_t nn = strlen(name);
    if (nn + 2 >= sizeof(pat))
        return 0;

    pat[0] = ' ';
    memcpy(pat + 1, name, nn);
    pat[1 + nn] = 0;

    const char *p = mf_find_ci(tag, pat);
    if (!p)
        p = mf_find_ci(tag, name);
    if (!p)
        return 0;

    p += (p[0] == ' ') ? (1 + (int)nn) : (int)nn;
    p = mf_skip_ws(p);
    if (*p != '=')
        return 0;
    ++p;
    p = mf_skip_ws(p);
    if (*p != '"' && *p != '\'')
        return 0;
    char q = *p++;

    char *end = NULL;
    float v = strtof(p, &end);
    if (!end || end == p)
        return 0;

    while (*end && *end != q)
        ++end;
    if (*end != q)
        return 0;

    *out = v;
    return 1;
}

static int mf_read_attr_u32(const char *tag, const char *name, uint32_t *out)
{
    if (!tag || !name || !out)
        return 0;

    char pat[64];
    size_t nn = strlen(name);
    if (nn + 2 >= sizeof(pat))
        return 0;

    pat[0] = ' ';
    memcpy(pat + 1, name, nn);
    pat[1 + nn] = 0;

    const char *p = mf_find_ci(tag, pat);
    if (!p)
        p = mf_find_ci(tag, name);
    if (!p)
        return 0;

    p += (p[0] == ' ') ? (1 + (int)nn) : (int)nn;
    p = mf_skip_ws(p);
    if (*p != '=')
        return 0;
    ++p;
    p = mf_skip_ws(p);
    if (*p != '"' && *p != '\'')
        return 0;
    char q = *p++;

    char *end = NULL;
    unsigned long v = strtoul(p, &end, 10);
    if (!end || end == p)
        return 0;

    while (*end && *end != q)
        ++end;
    if (*end != q)
        return 0;

    *out = (uint32_t)v;
    return 1;
}

static int mf_quick_verify(const char *path)
{
    if (!path)
        return 0;

    const char *ext = strrchr(path, '.');
    if (ext && (!strcmp(ext, ".3mf") || !strcmp(ext, ".3MF")))
        return 1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint8_t sig[4];
    size_t n = fread(sig, 1, 4, f);
    fclose(f);
    if (n != 4)
        return 0;

    if (sig[0] == 'P' && sig[1] == 'K' && sig[2] == 3 && sig[3] == 4)
        return 1;

    return 0;
}

static int mf_zip_find_first_3d_model(mz_zip_archive *zip, char *out_path, size_t out_cap)
{
    if (!zip || !out_path || out_cap == 0)
        return 0;

    mz_uint n = mz_zip_reader_get_num_files(zip);
    for (mz_uint i = 0; i < n; ++i)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(zip, i, &st))
            continue;
        if (st.m_is_directory)
            continue;

        const char *p = st.m_filename;
        if (!p || !p[0])
            continue;

        const char *ext = strrchr(p, '.');
        if (!ext)
            continue;

        if ((ext[1] == 'm' || ext[1] == 'M') &&
            (ext[2] == 'o' || ext[2] == 'O') &&
            (ext[3] == 'd' || ext[3] == 'D') &&
            (ext[4] == 'e' || ext[4] == 'E') &&
            (ext[5] == 'l' || ext[5] == 'L') &&
            ext[6] == 0)
        {
            strncpy(out_path, p, out_cap - 1);
            out_path[out_cap - 1] = 0;
            return 1;
        }
    }

    return 0;
}

static int mf_zip_read_main_model_to_mem(const char *zip_path, void **out_bytes, size_t *out_n)
{
    if (!zip_path || !out_bytes || !out_n)
        return 0;

    *out_bytes = NULL;
    *out_n = 0;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, zip_path, 0))
        return 0;

    int idx = mz_zip_reader_locate_file(&zip, "3D/3dmodel.model", NULL, 0);
    if (idx < 0)
        idx = mz_zip_reader_locate_file(&zip, "/3D/3dmodel.model", NULL, 0);

    if (idx < 0)
    {
        char found[512];
        if (!mf_zip_find_first_3d_model(&zip, found, sizeof(found)))
        {
            mz_zip_reader_end(&zip);
            return 0;
        }
        idx = mz_zip_reader_locate_file(&zip, found, NULL, 0);
        if (idx < 0)
        {
            mz_zip_reader_end(&zip);
            return 0;
        }
    }

    size_t n = 0;
    void *p = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &n, 0);
    mz_zip_reader_end(&zip);

    if (!p || n == 0)
    {
        mz_free(p);
        return 0;
    }

    *out_bytes = p;
    *out_n = n;
    return 1;
}

static int mf_parse_model_xml(const char *xml, size_t xml_n, vector_t *out_pos, vector_t *out_tri)
{
    if (!xml || !xml_n || !out_pos || !out_tri)
        return 0;

    const char *p = xml;
    const char *end = xml + xml_n;

    while (1)
    {
        const char *vtag = mf_find_ci(p, "<vertex");
        if (!vtag || vtag >= end)
            break;

        const char *gt = strchr(vtag, '>');
        if (!gt || gt >= end)
            break;

        float x = 0, y = 0, z = 0;
        mf_read_attr_f32(vtag, "x", &x);
        mf_read_attr_f32(vtag, "y", &y);
        mf_read_attr_f32(vtag, "z", &z);

        mf_v3_t vv;
        vv.x = x;
        vv.y = y;
        vv.z = z;
        vector_impl_push_back(out_pos, &vv);

        p = gt + 1;
    }

    p = xml;

    while (1)
    {
        const char *ttag = mf_find_ci(p, "<triangle");
        if (!ttag || ttag >= end)
            break;

        const char *gt = strchr(ttag, '>');
        if (!gt || gt >= end)
            break;

        uint32_t v1 = 0, v2 = 0, v3 = 0;
        if (!mf_read_attr_u32(ttag, "v1", &v1) || !mf_read_attr_u32(ttag, "v2", &v2) || !mf_read_attr_u32(ttag, "v3", &v3))
        {
            p = gt + 1;
            continue;
        }

        uint32_t tri[3] = {v1, v2, v3};
        vector_impl_push_back(out_tri, tri);

        p = gt + 1;
    }

    return out_pos->size > 0 && out_tri->size > 0;
}

static int mf_build_mesh_from_lists(const vector_t *pos, const vector_t *tri, model_raw_t *raw, ihandle_t material)
{
    if (!pos || !tri || !raw)
        return 0;

    uint32_t vcount = (uint32_t)tri->size * 3u;
    uint32_t icount = (uint32_t)tri->size * 3u;
    if (!vcount || !icount)
        return 0;

    model_vertex_t *vtx = (model_vertex_t *)malloc((size_t)vcount * sizeof(model_vertex_t));
    uint32_t *idx = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!vtx || !idx)
    {
        free(vtx);
        free(idx);
        return 0;
    }

    uint32_t w = 0;
    for (uint32_t t = 0; t < (uint32_t)tri->size; ++t)
    {
        const uint32_t *ti = (const uint32_t *)vector_impl_at((vector_t *)tri, t);
        if (!ti)
            continue;

        uint32_t i0 = ti[0], i1 = ti[1], i2 = ti[2];
        if (i0 >= pos->size || i1 >= pos->size || i2 >= pos->size)
            continue;

        const mf_v3_t *p0 = (const mf_v3_t *)vector_impl_at((vector_t *)pos, i0);
        const mf_v3_t *p1 = (const mf_v3_t *)vector_impl_at((vector_t *)pos, i1);
        const mf_v3_t *p2 = (const mf_v3_t *)vector_impl_at((vector_t *)pos, i2);
        if (!p0 || !p1 || !p2)
            continue;

        vec3 P0 = (vec3){p0->x, p0->y, p0->z};
        vec3 P1 = (vec3){p1->x, p1->y, p1->z};
        vec3 P2 = (vec3){p2->x, p2->y, p2->z};

        vec3 e1 = mf_vec3_sub(P1, P0);
        vec3 e2 = mf_vec3_sub(P2, P0);
        vec3 n = mf_vec3_norm_safe(mf_vec3_cross(e1, e2));

        model_vertex_t a, b, c;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        memset(&c, 0, sizeof(c));

        a.px = P0.x;
        a.py = P0.y;
        a.pz = P0.z;
        b.px = P1.x;
        b.py = P1.y;
        b.pz = P1.z;
        c.px = P2.x;
        c.py = P2.y;
        c.pz = P2.z;

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

        vtx[w + 0] = a;
        vtx[w + 1] = b;
        vtx[w + 2] = c;

        idx[w + 0] = w + 0;
        idx[w + 1] = w + 1;
        idx[w + 2] = w + 2;

        w += 3;
    }

    if (w == 0)
    {
        free(vtx);
        free(idx);
        return 0;
    }

    model_vertex_t *rv = (model_vertex_t *)realloc(vtx, (size_t)w * sizeof(model_vertex_t));
    uint32_t *ri = (uint32_t *)realloc(idx, (size_t)w * sizeof(uint32_t));
    if (rv)
        vtx = rv;
    if (ri)
        idx = ri;

    model_cpu_lod_t lod0;
    memset(&lod0, 0, sizeof(lod0));
    lod0.vertices = vtx;
    lod0.vertex_count = w;
    lod0.indices = idx;
    lod0.index_count = w;

    model_cpu_submesh_t sm;
    memset(&sm, 0, sizeof(sm));
    sm.lods = vector_impl_create_vector(sizeof(model_cpu_lod_t));
    vector_impl_push_back(&sm.lods, &lod0);
    sm.material = material;
    sm.material_name = NULL;

    sm.aabb = mf_aabb_from_vertices(vtx, w);
    sm.flags = (uint8_t)(sm.flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);

    vector_impl_push_back(&raw->submeshes, &sm);
    return 1;
}

static void mf_free_raw(model_raw_t *raw)
{
    model_raw_destroy(raw);
}

bool asset_model_3mf_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset)
{
    if (!am || !out_asset || !path)
        return false;
    if (path_is_ptr)
        return false;

    if (!mf_quick_verify(path))
        return false;

    void *xml_bytes = NULL;
    size_t xml_n = 0;

    if (!mf_zip_read_main_model_to_mem(path, &xml_bytes, &xml_n))
        return false;

    asset_material_t cur = material_make_default(0);
    ihandle_t mat = asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);

    vector_t pos = vector_impl_create_vector(sizeof(mf_v3_t));
    vector_t tri = vector_impl_create_vector(sizeof(uint32_t) * 3u);

    int ok_parse = mf_parse_model_xml((const char *)xml_bytes, xml_n, &pos, &tri);
    mz_free(xml_bytes);

    if (!ok_parse)
    {
        vector_impl_free(&pos);
        vector_impl_free(&tri);
        return false;
    }

    model_raw_t raw = model_raw_make();
    raw.mtllib_path = NULL;
    raw.mtllib = ihandle_invalid();

    int ok_mesh = mf_build_mesh_from_lists(&pos, &tri, &raw, mat);

    vector_impl_free(&pos);
    vector_impl_free(&tri);

    if (!ok_mesh)
    {
        mf_free_raw(&raw);
        return false;
    }

    model_raw_generate_lods(&raw);

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;

    return true;
}

bool asset_model_3mf_init(asset_manager_t *am, asset_any_t *asset)
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

void asset_model_3mf_cleanup(asset_manager_t *am, asset_any_t *asset)
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

static bool asset_model_3mf_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;
    if (!path || path_is_ptr)
        return false;

    const char *ext = strrchr(path, '.');
    if (!ext)
        return false;

    return (!strcmp(ext, ".3mf") || !strcmp(ext, ".3MF"));
}

asset_module_desc_t asset_module_model_3mf(void)
{
    asset_module_desc_t m = {0};
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL_3MF";
    m.load_fn = asset_model_3mf_load;
    m.init_fn = asset_model_3mf_init;
    m.cleanup_fn = asset_model_3mf_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    m.can_load_fn = asset_model_3mf_can_load;
    return m;
}