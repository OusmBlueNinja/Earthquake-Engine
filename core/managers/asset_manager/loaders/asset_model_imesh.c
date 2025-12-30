#include "asset_model_imesh.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/model.h"
#include "asset_manager/asset_types/material.h"
#include "vector.h"
#include "handle.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#define IMESH_LOGE(...) LOG_ERROR(__VA_ARGS__)
#define IMESH_LOGW(...) LOG_ERROR(__VA_ARGS__)

typedef struct imesh_header_t
{
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t submesh_count;
    uint32_t reserved0;
    ihandle_t model_handle;
    uint64_t submesh_table_offset;
} imesh_header_t;

typedef struct imesh_submesh_record_t
{
    uint32_t flags;
    uint32_t material_name_len;
    uint64_t material_name_offset;
    ihandle_t material_handle;
    float aabb_min[3];
    float aabb_max[3];
    uint32_t lod_count;
    uint32_t reserved0;
    uint64_t lods_offset;
} imesh_submesh_record_t;

typedef struct imesh_lod_record_t
{
    uint32_t vertex_count;
    uint32_t index_count;
    uint64_t vertices_offset;
    uint64_t indices_offset;
} imesh_lod_record_t;

enum
{
    IMESH_SUBMESH_HAS_AABB = 1u << 0
};

static bool imesh_has_ext(const char *p)
{
    if (!p)
        return false;
    const char *dot = strrchr(p, '.');
    if (!dot)
        return false;
    return strcmp(dot, ".imesh") == 0;
}

static void imesh_dir_of(const char *path, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    out[0] = 0;
    if (!path)
        return;

    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *s = a;
    if (!s || (b && b > s))
        s = b;
    if (!s)
        return;

    size_t n = (size_t)(s - path + 1);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

static bool imesh_is_abs_or_has_sep(const char *s)
{
    if (!s)
        return false;
    if (strchr(s, '/') || strchr(s, '\\'))
        return true;
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
        return true;
    return false;
}

static bool imesh_ends_with(const char *s, const char *ext)
{
    if (!s || !ext)
        return false;
    size_t ls = strlen(s);
    size_t le = strlen(ext);
    if (ls < le)
        return false;
    return memcmp(s + (ls - le), ext, le) == 0;
}

static char *imesh_make_imat_path(const char *mesh_path, const char *mat_name, uint32_t mat_len)
{
    if (!mat_name || mat_len == 0)
        return 0;

    char *tmp = (char *)malloc((size_t)mat_len + 1u);
    if (!tmp)
        return 0;

    memcpy(tmp, mat_name, mat_len);
    tmp[mat_len] = 0;

    if (imesh_is_abs_or_has_sep(tmp) || imesh_ends_with(tmp, ".imat"))
        return tmp;

    char dir[1024];
    imesh_dir_of(mesh_path, dir, sizeof(dir));
    size_t ld = strlen(dir);
    size_t ln = strlen(tmp);
    size_t le = 5;

    char *full = (char *)malloc(ld + ln + le + 1);
    if (!full)
    {
        free(tmp);
        return 0;
    }

    memcpy(full, dir, ld);
    memcpy(full + ld, tmp, ln);
    memcpy(full + ld + ln, ".imat", le);
    full[ld + ln + le] = 0;

    free(tmp);
    return full;
}

static bool imesh_read_all(const char *path, uint8_t **out_data, uint32_t *out_size)
{
    if (!path || !out_data || !out_size)
        return false;

    *out_data = 0;
    *out_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    bool ok = true;

    if (fseek(f, 0, SEEK_END) != 0)
        ok = false;

    long end = ok ? ftell(f) : 0;
    if (ok && end <= 0)
        ok = false;

    if (ok && fseek(f, 0, SEEK_SET) != 0)
        ok = false;

    uint8_t *data = 0;
    uint32_t size = 0;

    if (ok)
    {
        size = (uint32_t)end;
        data = (uint8_t *)malloc((size_t)size);
        if (!data)
            ok = false;
    }

    if (ok)
    {
        size_t got = fread(data, 1, (size_t)size, f);
        if (got != (size_t)size)
            ok = false;
    }

    fclose(f);

    if (!ok)
    {
        if (data)
            free(data);
        return false;
    }

    *out_data = data;
    *out_size = size;
    return true;
}

static bool imesh_validate_blob(const uint8_t *data, uint32_t size, const imesh_header_t **out_h)
{
    if (!data || size < (uint32_t)sizeof(imesh_header_t) || !out_h)
        return false;

    const imesh_header_t *h = (const imesh_header_t *)data;

    if (memcmp(h->magic, "IMSH", 4) != 0)
        return false;
    if (h->version != 2)
        return false;
    if (h->submesh_table_offset >= size)
        return false;

    uint64_t need = (uint64_t)h->submesh_table_offset + (uint64_t)h->submesh_count * (uint64_t)sizeof(imesh_submesh_record_t);
    if (need > (uint64_t)size)
        return false;

    *out_h = h;
    return true;
}

static void imesh_setup_model_vertex_vao(void)
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

static void imesh_free_raw(model_raw_t *raw)
{
    if (!raw)
        return;
    model_raw_destroy(raw);
}

static bool imesh_parse_to_raw(asset_manager_t *am, const char *mesh_path, const uint8_t *data, uint32_t size, model_raw_t *out_raw, ihandle_t *out_handle)
{
    if (!am || !data || !out_raw)
        return false;

    const imesh_header_t *h = 0;
    if (!imesh_validate_blob(data, size, &h))
        return false;

    if (out_handle)
        *out_handle = h->model_handle;

    model_raw_t raw = model_raw_make();
    raw.mtllib_path = 0;
    raw.mtllib = ihandle_invalid();
    raw.lod_count = 0;

    const imesh_submesh_record_t *smt = (const imesh_submesh_record_t *)(data + h->submesh_table_offset);

    bool ok = true;

    for (uint32_t si = 0; si < h->submesh_count && ok; ++si)
    {
        const imesh_submesh_record_t *sr = &smt[si];

        uint64_t lod_table_end = sr->lods_offset + (uint64_t)sr->lod_count * (uint64_t)sizeof(imesh_lod_record_t);
        if (sr->lods_offset >= size || lod_table_end > (uint64_t)size)
            ok = false;

        if (ok && sr->material_name_len)
        {
            uint64_t me = sr->material_name_offset + (uint64_t)sr->material_name_len;
            if (sr->material_name_offset >= size || me > (uint64_t)size)
                ok = false;
        }

        if (ok && sr->lod_count == 0)
            ok = false;

        if (!ok)
            break;

        model_cpu_submesh_t sm;
        memset(&sm, 0, sizeof(sm));
        sm.lods = vector_impl_create_vector(sizeof(model_cpu_lod_t));
        sm.material_name = 0;
        sm.material = ihandle_invalid();
        sm.flags = 0;

        if (sr->flags & IMESH_SUBMESH_HAS_AABB)
        {
            sm.flags = (uint8_t)(sm.flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);
            sm.aabb.min.x = sr->aabb_min[0];
            sm.aabb.min.y = sr->aabb_min[1];
            sm.aabb.min.z = sr->aabb_min[2];
            sm.aabb.max.x = sr->aabb_max[0];
            sm.aabb.max.y = sr->aabb_max[1];
            sm.aabb.max.z = sr->aabb_max[2];
        }

        if (sr->lod_count > raw.lod_count)
            raw.lod_count = (uint8_t)sr->lod_count;

        if (ihandle_is_valid(sr->material_handle))
        {
            sm.material = sr->material_handle;
        }
        else if (sr->material_name_len)
        {
            const char *mn = (const char *)(data + sr->material_name_offset);
            char *imat_path = imesh_make_imat_path(mesh_path, mn, sr->material_name_len);
            if (imat_path)
            {
                sm.material = asset_manager_request(am, ASSET_MATERIAL, imat_path);
                free(imat_path);
            }
        }

        const imesh_lod_record_t *lrs = (const imesh_lod_record_t *)(data + sr->lods_offset);

        for (uint32_t li = 0; li < sr->lod_count && ok; ++li)
        {
            const imesh_lod_record_t *lr = &lrs[li];

            uint64_t vb = lr->vertices_offset;
            uint64_t ib = lr->indices_offset;

            uint64_t vb_end = vb + (uint64_t)lr->vertex_count * (uint64_t)sizeof(model_vertex_t);
            uint64_t ib_end = ib + (uint64_t)lr->index_count * (uint64_t)sizeof(uint32_t);

            if (lr->vertex_count == 0 || lr->index_count == 0)
                ok = false;

            if (ok && (vb >= size || ib >= size || vb_end > (uint64_t)size || ib_end > (uint64_t)size))
                ok = false;

            if (!ok)
                break;

            model_cpu_lod_t lod;
            memset(&lod, 0, sizeof(lod));
            lod.vertex_count = lr->vertex_count;
            lod.index_count = lr->index_count;

            size_t vbytes = (size_t)lr->vertex_count * sizeof(model_vertex_t);
            size_t ibytes = (size_t)lr->index_count * sizeof(uint32_t);

            lod.vertices = (model_vertex_t *)malloc(vbytes);
            lod.indices = (uint32_t *)malloc(ibytes);

            if (!lod.vertices || !lod.indices)
            {
                free(lod.vertices);
                free(lod.indices);
                ok = false;
                break;
            }

            memcpy(lod.vertices, data + vb, vbytes);
            memcpy(lod.indices, data + ib, ibytes);

            vector_impl_push_back(&sm.lods, &lod);
        }

        if (!ok)
        {
            for (uint32_t li = 0; li < sm.lods.size; ++li)
            {
                model_cpu_lod_t *cl = (model_cpu_lod_t *)vector_impl_at(&sm.lods, li);
                if (!cl)
                    continue;
                free(cl->vertices);
                free(cl->indices);
                cl->vertices = 0;
                cl->indices = 0;
                cl->vertex_count = 0;
                cl->index_count = 0;
            }
            vector_impl_free(&sm.lods);
            break;
        }

        vector_impl_push_back(&raw.submeshes, &sm);
    }

    if (!ok || raw.submeshes.size == 0)
    {
        imesh_free_raw(&raw);
        return false;
    }

    *out_raw = raw;
    return true;
}

bool asset_model_imesh_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, ihandle_t *out_handle)
{
    if (out_handle)
        *out_handle = ihandle_invalid();

    if (!am || !out_asset || !path)
        return false;

    const uint8_t *blob = 0;
    uint32_t blob_size = 0;

    uint8_t *file_data = 0;
    uint32_t file_size = 0;

    if (path_is_ptr)
    {
        const asset_blob_t *b = (const asset_blob_t *)(const void *)path;
        if (!b || !b->data || b->size < (uint32_t)sizeof(imesh_header_t))
            return false;
        blob = b->data;
        blob_size = b->size;
    }
    else
    {
        if (!imesh_has_ext(path))
            return false;
        if (!imesh_read_all(path, &file_data, &file_size))
            return false;
        blob = file_data;
        blob_size = file_size;
    }

    model_raw_t raw = model_raw_make();
    ihandle_t ph = ihandle_invalid();

    bool ok = imesh_parse_to_raw(am, path_is_ptr ? "" : path, blob, blob_size, &raw, &ph);

    if (file_data)
        free(file_data);

    if (!ok)
        return false;

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;

    if (out_handle)
        *out_handle = ph;

    return true;
}

bool asset_model_imesh_init(asset_manager_t *am, asset_any_t *asset)
{
    if (!am || !asset || asset->type != ASSET_MODEL)
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

            imesh_setup_model_vertex_vao();

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            vector_impl_push_back(&gm.lods, &glod);
            uploaded++;

            if (li == 0)
                gm.flags = (uint8_t)(gm.flags | (uint8_t)MESH_FLAG_LOD0_READY);
        }

        if (uploaded > 0)
        {
            if (uploaded == want_lods)
                gm.flags = (uint8_t)(gm.flags | (uint8_t)MESH_FLAG_LODS_READY);
            vector_impl_push_back(&model.meshes, &gm);
        }
        else
        {
            vector_impl_free(&gm.lods);
        }
    }

    model_raw_destroy(&asset->as.model_raw);
    asset->as.model = model;

    asset->state = ASSET_STATE_READY;

    return true;
}

void asset_model_imesh_cleanup(asset_manager_t *am, asset_any_t *asset)
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

static uint64_t imesh_align_u64(uint64_t x, uint64_t a)
{
    if (a == 0)
        return x;
    uint64_t m = a - 1ull;
    return (x + m) & ~m;
}

static bool asset_model_imesh_save_blob(asset_manager_t *am, ihandle_t h, const asset_any_t *a, asset_blob_t *out)
{
    (void)am;

    if (!a || !out)
        return false;

    if (a->type != ASSET_MODEL)
        return false;

    if (a->state != ASSET_STATE_READY)
        return false;

    const asset_model_t *model = &a->as.model;

    uint32_t submesh_count = (uint32_t)model->meshes.size;
    if (submesh_count == 0)
        return false;

    uint32_t total_lods = 0;
    for (uint32_t si = 0; si < submesh_count; ++si)
    {
        const mesh_t *sm = (const mesh_t *)vector_impl_at((vector_t *)&model->meshes, si);
        if (!sm)
            return false;
        if (sm->lods.size == 0)
            return false;
        if (total_lods + (uint32_t)sm->lods.size < total_lods)
            return false;
        total_lods += (uint32_t)sm->lods.size;
    }

    uint32_t *sub_lod_base = (uint32_t *)malloc((size_t)submesh_count * sizeof(uint32_t));
    uint32_t *sub_lod_count = (uint32_t *)malloc((size_t)submesh_count * sizeof(uint32_t));
    uint32_t *lod_vbytes = (uint32_t *)malloc((size_t)total_lods * sizeof(uint32_t));
    uint32_t *lod_ibytes = (uint32_t *)malloc((size_t)total_lods * sizeof(uint32_t));
    uint32_t *lod_vcount = (uint32_t *)malloc((size_t)total_lods * sizeof(uint32_t));
    uint32_t *lod_icount = (uint32_t *)malloc((size_t)total_lods * sizeof(uint32_t));
    uint64_t *lod_voff = (uint64_t *)malloc((size_t)total_lods * sizeof(uint64_t));
    uint64_t *lod_ioff = (uint64_t *)malloc((size_t)total_lods * sizeof(uint64_t));

    if (!sub_lod_base || !sub_lod_count || !lod_vbytes || !lod_ibytes || !lod_vcount || !lod_icount || !lod_voff || !lod_ioff)
    {
        free(sub_lod_base);
        free(sub_lod_count);
        free(lod_vbytes);
        free(lod_ibytes);
        free(lod_vcount);
        free(lod_icount);
        free(lod_voff);
        free(lod_ioff);
        return false;
    }

    uint32_t lod_cursor = 0;
    for (uint32_t si = 0; si < submesh_count; ++si)
    {
        const mesh_t *sm = (const mesh_t *)vector_impl_at((vector_t *)&model->meshes, si);
        uint32_t lc = (uint32_t)sm->lods.size;

        sub_lod_base[si] = lod_cursor;
        sub_lod_count[si] = lc;

        for (uint32_t li = 0; li < lc; ++li)
        {
            const mesh_lod_t *lod = (const mesh_lod_t *)vector_impl_at((vector_t *)&sm->lods, li);
            if (!lod || !lod->vbo || !lod->ibo)
            {
                free(sub_lod_base);
                free(sub_lod_count);
                free(lod_vbytes);
                free(lod_ibytes);
                free(lod_vcount);
                free(lod_icount);
                free(lod_voff);
                free(lod_ioff);
                return false;
            }

            GLint vb_i = 0;
            GLint ib_i = 0;

            glBindBuffer(GL_ARRAY_BUFFER, lod->vbo);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vb_i);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod->ibo);
            glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &ib_i);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            if (vb_i <= 0 || ib_i <= 0)
            {
                free(sub_lod_base);
                free(sub_lod_count);
                free(lod_vbytes);
                free(lod_ibytes);
                free(lod_vcount);
                free(lod_icount);
                free(lod_voff);
                free(lod_ioff);
                return false;
            }

            uint32_t vb = (uint32_t)vb_i;
            uint32_t ib = (uint32_t)ib_i;

            if (vb % (uint32_t)sizeof(model_vertex_t) != 0)
            {
                free(sub_lod_base);
                free(sub_lod_count);
                free(lod_vbytes);
                free(lod_ibytes);
                free(lod_vcount);
                free(lod_icount);
                free(lod_voff);
                free(lod_ioff);
                return false;
            }

            if (ib % (uint32_t)sizeof(uint32_t) != 0)
            {
                free(sub_lod_base);
                free(sub_lod_count);
                free(lod_vbytes);
                free(lod_ibytes);
                free(lod_vcount);
                free(lod_icount);
                free(lod_voff);
                free(lod_ioff);
                return false;
            }

            uint32_t idx = lod_cursor + li;
            lod_vbytes[idx] = vb;
            lod_ibytes[idx] = ib;
            lod_vcount[idx] = vb / (uint32_t)sizeof(model_vertex_t);
            lod_icount[idx] = ib / (uint32_t)sizeof(uint32_t);
            lod_voff[idx] = 0;
            lod_ioff[idx] = 0;
        }

        lod_cursor += lc;
    }

    uint64_t smt_off = (uint64_t)sizeof(imesh_header_t);
    uint64_t smt_size = (uint64_t)submesh_count * (uint64_t)sizeof(imesh_submesh_record_t);

    uint64_t lod_tables_off = smt_off + smt_size;

    uint64_t cursor = lod_tables_off;
    for (uint32_t si = 0; si < submesh_count; ++si)
    {
        cursor += (uint64_t)sub_lod_count[si] * (uint64_t)sizeof(imesh_lod_record_t);
    }

    cursor = imesh_align_u64(cursor, 16);

    for (uint32_t li = 0; li < total_lods; ++li)
    {
        cursor = imesh_align_u64(cursor, 16);
        lod_voff[li] = cursor;
        cursor += (uint64_t)lod_vbytes[li];

        cursor = imesh_align_u64(cursor, 16);
        lod_ioff[li] = cursor;
        cursor += (uint64_t)lod_ibytes[li];
    }

    if (cursor > 0xFFFFFFFFull)
    {
        free(sub_lod_base);
        free(sub_lod_count);
        free(lod_vbytes);
        free(lod_ibytes);
        free(lod_vcount);
        free(lod_icount);
        free(lod_voff);
        free(lod_ioff);
        return false;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)cursor);
    if (!buf)
    {
        free(sub_lod_base);
        free(sub_lod_count);
        free(lod_vbytes);
        free(lod_ibytes);
        free(lod_vcount);
        free(lod_icount);
        free(lod_voff);
        free(lod_ioff);
        return false;
    }
    memset(buf, 0, (size_t)cursor);

    imesh_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = 'I';
    hdr.magic[1] = 'M';
    hdr.magic[2] = 'S';
    hdr.magic[3] = 'H';
    hdr.version = 2;
    hdr.flags = 0;
    hdr.submesh_count = submesh_count;
    hdr.reserved0 = 0;
    hdr.model_handle = h;
    hdr.submesh_table_offset = smt_off;

    memcpy(buf, &hdr, sizeof(hdr));

    imesh_submesh_record_t *smt = (imesh_submesh_record_t *)(buf + smt_off);

    uint64_t lod_table_cursor = lod_tables_off;

    for (uint32_t si = 0; si < submesh_count; ++si)
    {
        const mesh_t *sm = (const mesh_t *)vector_impl_at((vector_t *)&model->meshes, si);
        uint32_t base = sub_lod_base[si];
        uint32_t lc = sub_lod_count[si];

        imesh_submesh_record_t sr;
        memset(&sr, 0, sizeof(sr));
        sr.flags = IMESH_SUBMESH_HAS_AABB;
        sr.material_name_len = 0;
        sr.material_name_offset = 0;
        sr.material_handle = sm->material;
        sr.aabb_min[0] = sm->local_aabb.min.x;
        sr.aabb_min[1] = sm->local_aabb.min.y;
        sr.aabb_min[2] = sm->local_aabb.min.z;
        sr.aabb_max[0] = sm->local_aabb.max.x;
        sr.aabb_max[1] = sm->local_aabb.max.y;
        sr.aabb_max[2] = sm->local_aabb.max.z;
        sr.lod_count = lc;
        sr.reserved0 = 0;
        sr.lods_offset = lod_table_cursor;

        smt[si] = sr;

        imesh_lod_record_t *lrs = (imesh_lod_record_t *)(buf + sr.lods_offset);

        for (uint32_t li = 0; li < lc; ++li)
        {
            uint32_t idx = base + li;

            imesh_lod_record_t lr;
            memset(&lr, 0, sizeof(lr));
            lr.vertex_count = lod_vcount[idx];
            lr.index_count = lod_icount[idx];
            lr.vertices_offset = lod_voff[idx];
            lr.indices_offset = lod_ioff[idx];

            lrs[li] = lr;

            const mesh_lod_t *lod = (const mesh_lod_t *)vector_impl_at((vector_t *)&sm->lods, li);

            glBindBuffer(GL_ARRAY_BUFFER, lod->vbo);
            glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)lod_vbytes[idx], (void *)(buf + lod_voff[idx]));
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod->ibo);
            glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)lod_ibytes[idx], (void *)(buf + lod_ioff[idx]));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }

        lod_table_cursor += (uint64_t)lc * (uint64_t)sizeof(imesh_lod_record_t);
    }

    free(sub_lod_base);
    free(sub_lod_count);
    free(lod_vbytes);
    free(lod_ibytes);
    free(lod_vcount);
    free(lod_icount);
    free(lod_voff);
    free(lod_ioff);

    memset(out, 0, sizeof(*out));
    out->data = buf;
    out->size = (uint32_t)cursor;
    out->align = 16;
    out->uncompressed_size = (uint32_t)cursor;
    out->codec = 0;
    out->flags = 0;
    out->reserved = 0;

    return true;
}

static void asset_model_imesh_blob_free(asset_manager_t *am, asset_blob_t *blob)
{
    (void)am;
    if (!blob)
        return;
    if (blob->data)
        free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

static bool asset_model_imesh_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;

    if (!path)
        return false;

    if (path_is_ptr)
    {
        const asset_blob_t *b = (const asset_blob_t *)(const void *)path;
        if (!b || !b->data || b->size < (uint32_t)sizeof(imesh_header_t))
            return false;
        const imesh_header_t *h = (const imesh_header_t *)b->data;
        if (memcmp(h->magic, "IMSH", 4) != 0)
            return false;
        return h->version == 2;
    }

    if (!imesh_has_ext(path))
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    imesh_header_t h;
    size_t got = fread(&h, 1, sizeof(h), f);
    fclose(f);

    if (got != sizeof(h))
        return false;
    if (memcmp(h.magic, "IMSH", 4) != 0)
        return false;
    return h.version == 2;
}

asset_module_desc_t asset_module_model_imesh(void)
{
    asset_module_desc_t m = (asset_module_desc_t){0};
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL_IMESH";
    m.load_fn = asset_model_imesh_load;
    m.init_fn = asset_model_imesh_init;
    m.cleanup_fn = asset_model_imesh_cleanup;
    m.save_blob_fn = asset_model_imesh_save_blob;
    m.blob_free_fn = asset_model_imesh_blob_free;
    m.can_load_fn = asset_model_imesh_can_load;
    return m;
}