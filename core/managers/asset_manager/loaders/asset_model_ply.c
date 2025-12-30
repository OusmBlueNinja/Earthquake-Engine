/* asset_model_ply.c */
#include "asset_model_ply.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
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

aabb_t model_cpu_submesh_compute_aabb(const model_cpu_submesh_t *sm);
void mesh_set_local_aabb_from_cpu(mesh_t *dst, const model_cpu_submesh_t *src);

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

static int ply_quick_verify(const char *path)
{
    if (!path)
        return 0;

    const char *ext = strrchr(path, '.');
    if (ext && (!strcmp(ext, ".ply") || !strcmp(ext, ".PLY")))
        return 1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    char hdr[4] = {0, 0, 0, 0};
    size_t n = fread(hdr, 1, 3, f);
    fclose(f);
    if (n != 3)
        return 0;
    return (hdr[0] == 'p' && hdr[1] == 'l' && hdr[2] == 'y');
}

static int ply_read_line(FILE *f, char *buf, size_t cap)
{
    if (!f || !buf || cap == 0)
        return 0;

    size_t w = 0;
    int c = 0;

    while ((c = fgetc(f)) != EOF)
    {
        if (c == '\r')
            continue;
        if (c == '\n')
            break;
        if (w + 1 < cap)
            buf[w++] = (char)c;
    }

    if (c == EOF && w == 0)
        return 0;

    buf[w] = 0;
    return 1;
}

static char *ply_ltrim(char *s)
{
    while (s && *s && isspace((unsigned char)*s))
        ++s;
    return s;
}

static void ply_rtrim_inplace(char *s)
{
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

static int ply_startswith_ci(const char *s, const char *pfx)
{
    if (!s || !pfx)
        return 0;
    for (; *pfx; ++pfx, ++s)
    {
        unsigned char a = (unsigned char)*s;
        unsigned char b = (unsigned char)*pfx;
        if (!a)
            return 0;
        if (tolower(a) != tolower(b))
            return 0;
    }
    return 1;
}

static int ply_is_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int ply_next_token(char **p, char *out, size_t out_cap)
{
    if (!p || !*p || !out || out_cap == 0)
        return 0;

    char *s = *p;
    while (*s && isspace((unsigned char)*s))
        ++s;
    if (!*s)
    {
        *p = s;
        return 0;
    }

    size_t w = 0;
    while (*s && !isspace((unsigned char)*s))
    {
        if (w + 1 < out_cap)
            out[w++] = *s;
        ++s;
    }
    out[w] = 0;
    *p = s;
    return 1;
}

typedef struct ply_header_t
{
    uint32_t vertex_count;
    uint32_t face_count;
    int ascii;

    int vx, vy, vz;
    int vnx, vny, vnz;
    int vu, vv;

    int in_vertex;
    int in_face;
} ply_header_t;

static void ply_header_init(ply_header_t *h)
{
    memset(h, 0, sizeof(*h));
    h->ascii = 0;
    h->vx = h->vy = h->vz = -1;
    h->vnx = h->vny = h->vnz = -1;
    h->vu = h->vv = -1;
}

static int ply_parse_header(FILE *f, ply_header_t *h)
{
    char line[1024];

    if (!ply_read_line(f, line, sizeof(line)))
        return 0;

    char *s0 = ply_ltrim(line);
    ply_rtrim_inplace(s0);
    if (!ply_is_ci(s0, "ply"))
        return 0;

    int prop_index = 0;

    while (ply_read_line(f, line, sizeof(line)))
    {
        char *s = ply_ltrim(line);
        ply_rtrim_inplace(s);
        if (!s[0])
            continue;

        if (ply_startswith_ci(s, "comment"))
            continue;

        if (ply_startswith_ci(s, "format"))
        {
            char *p = s;
            char tok[128];
            ply_next_token(&p, tok, sizeof(tok));
            if (!ply_next_token(&p, tok, sizeof(tok)))
                return 0;

            if (ply_is_ci(tok, "ascii"))
                h->ascii = 1;
            else
                return 0;

            continue;
        }

        if (ply_startswith_ci(s, "element"))
        {
            char *p = s;
            char tok[128];
            ply_next_token(&p, tok, sizeof(tok));

            if (!ply_next_token(&p, tok, sizeof(tok)))
                return 0;

            h->in_vertex = 0;
            h->in_face = 0;
            prop_index = 0;

            if (ply_is_ci(tok, "vertex"))
            {
                char cnt[128];
                if (!ply_next_token(&p, cnt, sizeof(cnt)))
                    return 0;
                h->vertex_count = (uint32_t)strtoul(cnt, NULL, 10);
                h->in_vertex = 1;
            }
            else if (ply_is_ci(tok, "face"))
            {
                char cnt[128];
                if (!ply_next_token(&p, cnt, sizeof(cnt)))
                    return 0;
                h->face_count = (uint32_t)strtoul(cnt, NULL, 10);
                h->in_face = 1;
            }

            continue;
        }

        if (ply_startswith_ci(s, "property"))
        {
            char *p = s;
            char tok[128];
            ply_next_token(&p, tok, sizeof(tok));

            if (!ply_next_token(&p, tok, sizeof(tok)))
                return 0;

            if (ply_is_ci(tok, "list"))
            {
                char count_t[128], index_t[128], name[128];
                if (!ply_next_token(&p, count_t, sizeof(count_t)))
                    return 0;
                if (!ply_next_token(&p, index_t, sizeof(index_t)))
                    return 0;
                if (!ply_next_token(&p, name, sizeof(name)))
                    return 0;

                if (h->in_vertex)
                    prop_index++;

                continue;
            }

            char name[128];
            if (!ply_next_token(&p, name, sizeof(name)))
                return 0;

            if (h->in_vertex)
            {
                if (ply_is_ci(name, "x"))
                    h->vx = prop_index;
                else if (ply_is_ci(name, "y"))
                    h->vy = prop_index;
                else if (ply_is_ci(name, "z"))
                    h->vz = prop_index;
                else if (ply_is_ci(name, "nx"))
                    h->vnx = prop_index;
                else if (ply_is_ci(name, "ny"))
                    h->vny = prop_index;
                else if (ply_is_ci(name, "nz"))
                    h->vnz = prop_index;
                else if (ply_is_ci(name, "u") || ply_is_ci(name, "s") || ply_is_ci(name, "texture_u"))
                    h->vu = prop_index;
                else if (ply_is_ci(name, "v") || ply_is_ci(name, "t") || ply_is_ci(name, "texture_v"))
                    h->vv = prop_index;

                prop_index++;
            }

            continue;
        }

        if (ply_is_ci(s, "end_header"))
            break;
    }

    if (!h->ascii)
        return 0;
    if (h->vertex_count == 0)
        return 0;
    if (h->vx < 0 || h->vy < 0 || h->vz < 0)
        return 0;

    return 1;
}

static int ply_read_vertex_ascii(FILE *f, const ply_header_t *h, model_vertex_t *out_v)
{
    char line[4096];
    if (!ply_read_line(f, line, sizeof(line)))
        return 0;

    char *p = line;
    float vals[64];
    uint32_t nvals = 0;

    while (nvals < 64)
    {
        char tok[128];
        if (!ply_next_token(&p, tok, sizeof(tok)))
            break;
        vals[nvals++] = (float)strtod(tok, NULL);
    }

    int maxidx = h->vx;
    if (h->vy > maxidx)
        maxidx = h->vy;
    if (h->vz > maxidx)
        maxidx = h->vz;
    if (h->vnx > maxidx)
        maxidx = h->vnx;
    if (h->vny > maxidx)
        maxidx = h->vny;
    if (h->vnz > maxidx)
        maxidx = h->vnz;
    if (h->vu > maxidx)
        maxidx = h->vu;
    if (h->vv > maxidx)
        maxidx = h->vv;

    if ((int)nvals < (maxidx + 1))
        return 0;

    memset(out_v, 0, sizeof(*out_v));

    out_v->px = vals[h->vx];
    out_v->py = vals[h->vy];
    out_v->pz = vals[h->vz];

    if (h->vnx >= 0 && h->vny >= 0 && h->vnz >= 0)
    {
        out_v->nx = vals[h->vnx];
        out_v->ny = vals[h->vny];
        out_v->nz = vals[h->vnz];
    }
    else
    {
        out_v->nx = 0.0f;
        out_v->ny = 0.0f;
        out_v->nz = 1.0f;
    }

    if (h->vu >= 0 && h->vv >= 0)
    {
        out_v->u = vals[h->vu];
        out_v->v = vals[h->vv];
    }
    else
    {
        out_v->u = 0.0f;
        out_v->v = 0.0f;
    }

    out_v->tx = 1.0f;
    out_v->ty = 0.0f;
    out_v->tz = 0.0f;
    out_v->tw = 1.0f;

    return 1;
}

static int ply_read_face_ascii(FILE *f, uint32_t *out_idx, uint32_t *out_count)
{
    char line[4096];
    if (!ply_read_line(f, line, sizeof(line)))
        return 0;

    char *p = line;
    char tok[128];

    if (!ply_next_token(&p, tok, sizeof(tok)))
        return 0;

    uint32_t n = (uint32_t)strtoul(tok, NULL, 10);
    if (n < 3)
        return 0;

    uint32_t tmp[64];
    uint32_t got = 0;

    while (got < n && got < 64)
    {
        if (!ply_next_token(&p, tok, sizeof(tok)))
            break;
        tmp[got++] = (uint32_t)strtoul(tok, NULL, 10);
    }

    if (got < 3)
        return 0;

    uint32_t w = 0;
    uint32_t i0 = tmp[0];
    for (uint32_t i = 1; i + 1 < got; ++i)
    {
        out_idx[w++] = i0;
        out_idx[w++] = tmp[i];
        out_idx[w++] = tmp[i + 1];
    }

    *out_count = w;
    return 1;
}

static void ply_compute_flat_normals(model_vertex_t *vtx, uint32_t vcount, const uint32_t *idx, uint32_t icount)
{
    if (!vtx || !idx || icount < 3)
        return;

    for (uint32_t i = 0; i < icount; i += 3)
    {
        uint32_t a = idx[i + 0];
        uint32_t b = idx[i + 1];
        uint32_t c = idx[i + 2];
        if (a >= vcount || b >= vcount || c >= vcount)
            continue;

        vec3 P0 = (vec3){vtx[a].px, vtx[a].py, vtx[a].pz};
        vec3 P1 = (vec3){vtx[b].px, vtx[b].py, vtx[b].pz};
        vec3 P2 = (vec3){vtx[c].px, vtx[c].py, vtx[c].pz};

        vec3 e1 = (vec3){P1.x - P0.x, P1.y - P0.y, P1.z - P0.z};
        vec3 e2 = (vec3){P2.x - P0.x, P2.y - P0.y, P2.z - P0.z};

        vec3 n = (vec3){e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        float l2 = n.x * n.x + n.y * n.y + n.z * n.z;
        if (l2 < 1e-20f)
            n = (vec3){0.0f, 0.0f, 1.0f};
        else
        {
            float inv = 1.0f / sqrtf(l2);
            n = (vec3){n.x * inv, n.y * inv, n.z * inv};
        }

        vtx[a].nx = n.x;
        vtx[a].ny = n.y;
        vtx[a].nz = n.z;
        vtx[b].nx = n.x;
        vtx[b].ny = n.y;
        vtx[b].nz = n.z;
        vtx[c].nx = n.x;
        vtx[c].ny = n.y;
        vtx[c].nz = n.z;
    }
}

static void ply_free_raw(model_raw_t *raw)
{
    model_raw_destroy(raw);
}

bool asset_model_ply_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset)
{
    if (!am || !out_asset || !path)
        return false;
    if (path_is_ptr)
        return false;

    if (!ply_quick_verify(path))
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    ply_header_t h;
    ply_header_init(&h);

    if (!ply_parse_header(f, &h))
    {
        fclose(f);
        return false;
    }

    asset_material_t cur = material_make_default(0);
    ihandle_t mat = asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);

    model_vertex_t *verts = (model_vertex_t *)malloc((size_t)h.vertex_count * sizeof(model_vertex_t));
    if (!verts)
    {
        fclose(f);
        return false;
    }

    for (uint32_t i = 0; i < h.vertex_count; ++i)
    {
        if (!ply_read_vertex_ascii(f, &h, &verts[i]))
        {
            free(verts);
            fclose(f);
            return false;
        }
    }

    vector_t idxv = vector_impl_create_vector(sizeof(uint32_t));
    uint32_t tmpi[192];
    uint32_t tmpn = 0;
    (void)tmpn;

    for (uint32_t i = 0; i < h.face_count; ++i)
    {
        uint32_t outc = 0;
        if (!ply_read_face_ascii(f, tmpi, &outc))
            continue;
        for (uint32_t k = 0; k < outc; ++k)
            vector_impl_push_back(&idxv, &tmpi[k]);
    }

    fclose(f);

    if (idxv.size < 3)
    {
        vector_impl_free(&idxv);
        free(verts);
        return false;
    }

    uint32_t *idx = (uint32_t *)malloc((size_t)idxv.size * sizeof(uint32_t));
    if (!idx)
    {
        vector_impl_free(&idxv);
        free(verts);
        return false;
    }

    for (uint32_t i = 0; i < idxv.size; ++i)
    {
        uint32_t *pi = (uint32_t *)vector_impl_at(&idxv, i);
        idx[i] = pi ? *pi : 0u;
    }

    uint32_t icount = idxv.size;
    vector_impl_free(&idxv);

    if (h.vnx < 0 || h.vny < 0 || h.vnz < 0)
        ply_compute_flat_normals(verts, h.vertex_count, idx, icount);

    model_raw_t raw = model_raw_make();
    raw.mtllib_path = NULL;
    raw.mtllib = ihandle_invalid();

    model_cpu_lod_t lod0;
    memset(&lod0, 0, sizeof(lod0));
    lod0.vertices = verts;
    lod0.vertex_count = h.vertex_count;
    lod0.indices = idx;
    lod0.index_count = icount;

    model_cpu_submesh_t sm;
    memset(&sm, 0, sizeof(sm));
    sm.lods = vector_impl_create_vector(sizeof(model_cpu_lod_t));
    vector_impl_push_back(&sm.lods, &lod0);
    sm.material = mat;
    sm.material_name = NULL;

    vector_impl_push_back(&raw.submeshes, &sm);

    model_cpu_submesh_t *psm = (model_cpu_submesh_t *)vector_impl_at(&raw.submeshes, 0);
    if (psm)
    {
        psm->aabb = model_cpu_submesh_compute_aabb(psm);
        psm->flags = (uint8_t)(psm->flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);
    }

    model_raw_generate_lods(&raw);

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;

    return true;
}

bool asset_model_ply_init(asset_manager_t *am, asset_any_t *asset)
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

void asset_model_ply_cleanup(asset_manager_t *am, asset_any_t *asset)
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

static bool asset_model_ply_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;
    if (!path || path_is_ptr)
        return false;

    const char *ext = strrchr(path, '.');
    if (!ext)
        return false;

    return (!strcmp(ext, ".ply") || !strcmp(ext, ".PLY"));
}

asset_module_desc_t asset_module_model_ply(void)
{
    asset_module_desc_t m = {0};
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL_PLY";
    m.load_fn = asset_model_ply_load;
    m.init_fn = asset_model_ply_init;
    m.cleanup_fn = asset_model_ply_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    m.can_load_fn = asset_model_ply_can_load;
    return m;
}