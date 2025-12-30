#include "asset_model_fbx.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <float.h>
#include <math.h>

#include "asset_manager/asset_types/model.h"
#include "asset_manager/asset_types/material.h"
#include "asset_manager/asset_types/image.h"
#include "asset_manager/asset_manager.h"
#include "vector.h"
#include "systems/model_lod.h"
#include "types/mat4.h"
#include "types/vec3.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include "ufbx.h"

#define FBX_LOGE(...) LOG_ERROR(__VA_ARGS__)
#define FBX_LOGW(...) LOG_WARN(__VA_ARGS__)

typedef struct fbx_mat_entry_t
{
    const ufbx_material *m;
    ihandle_t h;
} fbx_mat_entry_t;

typedef struct asset_image_mem_desc_t
{
    void *bytes;
    size_t bytes_n;
    char *debug_name;
} asset_image_mem_desc_t;

static char *fbx_strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static char *fbx_path_dirname_dup(const char *path)
{
    if (!path)
        return NULL;
    size_t n = strlen(path);
    size_t cut = 0;
    for (size_t i = 0; i < n; ++i)
    {
        char c = path[i];
        if (c == '/' || c == '\\')
            cut = i + 1;
    }
    char *out = (char *)malloc(cut + 1);
    if (!out)
        return NULL;
    memcpy(out, path, cut);
    out[cut] = 0;
    return out;
}

static char *fbx_path_join_dup(const char *a, const char *b)
{
    if (!a || !b)
        return NULL;
    size_t na = strlen(a);
    size_t nb = strlen(b);
    char need = 0;
    if (na > 0)
    {
        char c = a[na - 1];
        if (c != '/' && c != '\\')
            need = 1;
    }
    char *out = (char *)malloc(na + (size_t)need + nb + 1);
    if (!out)
        return NULL;
    memcpy(out, a, na);
    if (need)
        out[na] = '/';
    memcpy(out + na + (size_t)need, b, nb);
    out[na + (size_t)need + nb] = 0;
    return out;
}

static float fbx_clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

static void fbx_vao_setup_model_vertex(void)
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

static vec3 fbx_vec3_norm_safe(vec3 v)
{
    float l2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (l2 < 1e-20f)
        return (vec3){0.0f, 0.0f, 1.0f};
    float inv = 1.0f / sqrtf(l2);
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
}

static vec3 fbx_vec3_mul(vec3 a, float s)
{
    return (vec3){a.x * s, a.y * s, a.z * s};
}

static float fbx_mat3_det_from_mat4(mat4 m)
{
    float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];
    return a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
}

static vec3 fbx_normal_xform(mat4 m, vec3 n)
{
    float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];

    float b01 = a22 * a11 - a12 * a21;
    float b11 = -a22 * a10 + a12 * a20;
    float b21 = a21 * a10 - a11 * a20;

    float det = a00 * b01 + a01 * b11 + a02 * b21;

    if (fabsf(det) < 1e-20f)
    {
        vec3 o;
        o.x = a00 * n.x + a01 * n.y + a02 * n.z;
        o.y = a10 * n.x + a11 * n.y + a12 * n.z;
        o.z = a20 * n.x + a21 * n.y + a22 * n.z;
        return fbx_vec3_norm_safe(o);
    }

    float invdet = 1.0f / det;

    float i00 = b01 * invdet;
    float i01 = (a02 * a21 - a22 * a01) * invdet;
    float i02 = (a12 * a01 - a02 * a11) * invdet;

    float i10 = b11 * invdet;
    float i11 = (a22 * a00 - a02 * a20) * invdet;
    float i12 = (a02 * a10 - a12 * a00) * invdet;

    float i20 = b21 * invdet;
    float i21 = (a01 * a20 - a21 * a00) * invdet;
    float i22 = (a11 * a00 - a01 * a10) * invdet;

    vec3 o;
    o.x = i00 * n.x + i10 * n.y + i20 * n.z;
    o.y = i01 * n.x + i11 * n.y + i21 * n.z;
    o.z = i02 * n.x + i12 * n.y + i22 * n.z;

    return fbx_vec3_norm_safe(o);
}

static mat4 fbx_mat4_from_ufbx_matrix(ufbx_matrix m)
{
    mat4 o = mat4_identity();
    o.m[0] = (float)m.m00;
    o.m[1] = (float)m.m10;
    o.m[2] = (float)m.m20;

    o.m[4] = (float)m.m01;
    o.m[5] = (float)m.m11;
    o.m[6] = (float)m.m21;

    o.m[8] = (float)m.m02;
    o.m[9] = (float)m.m12;
    o.m[10] = (float)m.m22;

    o.m[12] = (float)m.m03;
    o.m[13] = (float)m.m13;
    o.m[14] = (float)m.m23;
    return o;
}

static vec3 fbx_mat4_mul_point(mat4 m, vec3 p)
{
    vec3 o;
    o.x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    o.y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    o.z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return o;
}

static ihandle_t fbx_request_image_from_texture(asset_manager_t *am, const char *fbx_path, const ufbx_texture *tex, const char *debug_name_fallback)
{
    if (!am || !fbx_path || !tex)
        return ihandle_invalid();

    const char *dbg = (tex->name.data && tex->name.data[0]) ? tex->name.data : debug_name_fallback;
    if (!dbg)
        dbg = "fbx_texture";

    if (tex->content.data && tex->content.size > 0)
    {
        asset_image_mem_desc_t *desc = (asset_image_mem_desc_t *)malloc(sizeof(asset_image_mem_desc_t));
        if (!desc)
            return ihandle_invalid();
        memset(desc, 0, sizeof(*desc));

        desc->debug_name = fbx_strdup(dbg);
        if (!desc->debug_name)
        {
            free(desc);
            return ihandle_invalid();
        }

        void *bytes = malloc((size_t)tex->content.size);
        if (!bytes)
        {
            free(desc->debug_name);
            free(desc);
            return ihandle_invalid();
        }

        memcpy(bytes, tex->content.data, (size_t)tex->content.size);
        desc->bytes = bytes;
        desc->bytes_n = (size_t)tex->content.size;

        ihandle_t h = asset_manager_request_ptr(am, ASSET_IMAGE, desc);
        if (!ihandle_is_valid(h))
        {
            free(desc->bytes);
            free(desc->debug_name);
            free(desc);
        }
        return h;
    }

    const char *rel = NULL;
    if (tex->relative_filename.data && tex->relative_filename.data[0])
        rel = tex->relative_filename.data;
    else if (tex->filename.data && tex->filename.data[0])
        rel = tex->filename.data;

    if (!rel || !rel[0])
        return ihandle_invalid();

    char *dir = fbx_path_dirname_dup(fbx_path);
    char *full = dir ? fbx_path_join_dup(dir, rel) : NULL;
    free(dir);

    if (!full)
        return ihandle_invalid();

    ihandle_t h = asset_manager_request(am, ASSET_IMAGE, full);
    free(full);
    return h;
}

static ihandle_t fbx_material_to_handle(asset_manager_t *am, const char *fbx_path, const ufbx_material *m)
{
    asset_material_t cur = material_make_default(0);

    if (m && m->name.data && m->name.data[0])
    {
        free(cur.name);
        cur.name = (char *)malloc(m->name.length + 1);
        if (cur.name)
        {
            memcpy(cur.name, m->name.data, m->name.length);
            cur.name[m->name.length] = 0;
        }
    }

    if (!m)
    {
        cur.opacity = 1.0f;
        return asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);
    }

    material_set_flag(&cur, MAT_FLAG_DOUBLE_SIDED, m->features.double_sided.enabled ? true : false);

    {
        ufbx_material_map bc = m->pbr.base_color;
        if (bc.has_value && bc.value_components >= 3)
        {
            cur.albedo = (vec3){(float)bc.value_vec3.x, (float)bc.value_vec3.y, (float)bc.value_vec3.z};
        }
        else
        {
            ufbx_material_map dc = m->fbx.diffuse_color;
            if (dc.has_value && dc.value_components >= 3)
                cur.albedo = (vec3){(float)dc.value_vec3.x, (float)dc.value_vec3.y, (float)dc.value_vec3.z};
        }

        ufbx_material_map bf = m->pbr.base_factor;
        if (bf.has_value)
            cur.opacity = fbx_clamp01((float)bf.value_real);
        else
            cur.opacity = 1.0f;

        ufbx_material_map op = m->pbr.opacity;
        if (op.has_value)
            cur.opacity = fbx_clamp01((float)op.value_real);

        ufbx_material_map rough = m->pbr.roughness;
        if (rough.has_value)
            cur.roughness = fbx_clamp01((float)rough.value_real);

        ufbx_material_map metal = m->pbr.metalness;
        if (metal.has_value)
            cur.metallic = fbx_clamp01((float)metal.value_real);

        if (bc.texture)
            cur.albedo_tex = fbx_request_image_from_texture(am, fbx_path, bc.texture, "fbx_base_color");

        if (rough.texture)
            cur.roughness_tex = fbx_request_image_from_texture(am, fbx_path, rough.texture, "fbx_roughness");

        if (metal.texture)
            cur.metallic_tex = fbx_request_image_from_texture(am, fbx_path, metal.texture, "fbx_metalness");

        ufbx_material_map emc = m->pbr.emission_color;
        if (emc.has_value && emc.value_components >= 3)
            cur.emissive = (vec3){(float)emc.value_vec3.x, (float)emc.value_vec3.y, (float)emc.value_vec3.z};

        if (emc.texture)
            cur.emissive_tex = fbx_request_image_from_texture(am, fbx_path, emc.texture, "fbx_emission");

        ufbx_material_map nm = m->pbr.normal_map;
        if (nm.texture)
            cur.normal_tex = fbx_request_image_from_texture(am, fbx_path, nm.texture, "fbx_normal");
        else if (m->fbx.normal_map.texture)
            cur.normal_tex = fbx_request_image_from_texture(am, fbx_path, m->fbx.normal_map.texture, "fbx_normal_fbx");

        ufbx_material_map ao = m->pbr.ambient_occlusion;
        if (ao.texture)
            cur.occlusion_tex = fbx_request_image_from_texture(am, fbx_path, ao.texture, "fbx_ao");
    }

    if (m->features.opacity.enabled)
    {
        material_set_flag(&cur, MAT_FLAG_ALPHA_BLEND, true);
    }
    else
    {
        cur.opacity = 1.0f;
    }

    return asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);
}

static ihandle_t fbx_get_or_make_mat(asset_manager_t *am, const char *fbx_path, vector_t *map, const ufbx_material *m)
{
    if (!map)
        return ihandle_invalid();

    for (uint32_t i = 0; i < map->size; ++i)
    {
        fbx_mat_entry_t *e = (fbx_mat_entry_t *)vector_impl_at(map, i);
        if (e && e->m == m)
            return e->h;
    }

    ihandle_t h = fbx_material_to_handle(am, fbx_path, m);

    fbx_mat_entry_t e;
    e.m = m;
    e.h = h;
    vector_impl_push_back(map, &e);
    return h;
}

static aabb_t fbx_aabb_from_vertices(const model_vertex_t *vtx, uint32_t vcount)
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

static void fbx_generate_tangents(model_vertex_t *vtx, uint32_t vcount, const uint32_t *idx, uint32_t icount)
{
    if (!vtx || !idx || vcount == 0 || icount < 3)
        return;

    vec3 *tan1 = (vec3 *)malloc((size_t)vcount * sizeof(vec3));
    vec3 *tan2 = (vec3 *)malloc((size_t)vcount * sizeof(vec3));
    if (!tan1 || !tan2)
    {
        free(tan1);
        free(tan2);
        return;
    }

    for (uint32_t i = 0; i < vcount; ++i)
    {
        tan1[i] = (vec3){0, 0, 0};
        tan2[i] = (vec3){0, 0, 0};
    }

    uint32_t tri_count = icount / 3;
    for (uint32_t t = 0; t < tri_count; ++t)
    {
        uint32_t i0 = idx[t * 3 + 0];
        uint32_t i1 = idx[t * 3 + 1];
        uint32_t i2 = idx[t * 3 + 2];
        if (i0 >= vcount || i1 >= vcount || i2 >= vcount)
            continue;

        vec3 p0 = (vec3){vtx[i0].px, vtx[i0].py, vtx[i0].pz};
        vec3 p1 = (vec3){vtx[i1].px, vtx[i1].py, vtx[i1].pz};
        vec3 p2 = (vec3){vtx[i2].px, vtx[i2].py, vtx[i2].pz};

        float u0 = vtx[i0].u, v0 = vtx[i0].v;
        float u1 = vtx[i1].u, v1 = vtx[i1].v;
        float u2 = vtx[i2].u, v2 = vtx[i2].v;

        vec3 e1 = vec3_sub(p1, p0);
        vec3 e2 = vec3_sub(p2, p0);

        float du1 = u1 - u0;
        float dv1 = v1 - v0;
        float du2 = u2 - u0;
        float dv2 = v2 - v0;

        float denom = du1 * dv2 - dv1 * du2;
        if (fabsf(denom) < 1e-20f)
            continue;

        float r = 1.0f / denom;

        vec3 sdir = fbx_vec3_mul(vec3_sub(fbx_vec3_mul(e1, dv2), fbx_vec3_mul(e2, dv1)), r);
        vec3 tdir = fbx_vec3_mul(vec3_sub(fbx_vec3_mul(e2, du1), fbx_vec3_mul(e1, du2)), r);

        tan1[i0] = vec3_add(tan1[i0], sdir);
        tan1[i1] = vec3_add(tan1[i1], sdir);
        tan1[i2] = vec3_add(tan1[i2], sdir);

        tan2[i0] = vec3_add(tan2[i0], tdir);
        tan2[i1] = vec3_add(tan2[i1], tdir);
        tan2[i2] = vec3_add(tan2[i2], tdir);
    }

    for (uint32_t i = 0; i < vcount; ++i)
    {
        vec3 n = (vec3){vtx[i].nx, vtx[i].ny, vtx[i].nz};
        vec3 t = tan1[i];
        vec3 b = tan2[i];

        float n2 = vec3_dot(n, n);
        float t2 = vec3_dot(t, t);

        if (n2 < 1e-20f || t2 < 1e-20f)
        {
            vtx[i].tx = 1.0f;
            vtx[i].ty = 0.0f;
            vtx[i].tz = 0.0f;
            vtx[i].tw = 1.0f;
            continue;
        }

        n = fbx_vec3_norm_safe(n);

        float ndott = vec3_dot(n, t);
        vec3 ortho = vec3_sub(t, fbx_vec3_mul(n, ndott));
        float o2 = vec3_dot(ortho, ortho);
        if (o2 < 1e-20f)
        {
            vtx[i].tx = 1.0f;
            vtx[i].ty = 0.0f;
            vtx[i].tz = 0.0f;
            vtx[i].tw = 1.0f;
            continue;
        }

        vec3 tnorm = fbx_vec3_norm_safe(ortho);

        vec3 c = vec3_cross(n, tnorm);
        float handed = (vec3_dot(c, b) < 0.0f) ? -1.0f : 1.0f;

        vtx[i].tx = tnorm.x;
        vtx[i].ty = tnorm.y;
        vtx[i].tz = tnorm.z;
        vtx[i].tw = handed;
    }

    free(tan1);
    free(tan2);
}

static int fbx_quick_verify(const char *path)
{
    if (!path)
        return 0;
    const char *ext = strrchr(path, '.');
    if (!ext)
        return 0;
    if (!strcmp(ext, ".fbx") || !strcmp(ext, ".FBX"))
        return 1;
    return 0;
}

static uint32_t fbx_stream_index_u32(const ufbx_uint32_list *indices, uint32_t corner)
{
    if (indices && indices->data)
        return indices->data[corner];
    return corner;
}

static int fbx_read_vertex_corner(model_vertex_t *dst, const ufbx_mesh *mesh, uint32_t corner, mat4 world, int want_tangent)
{
    if (!dst || !mesh)
        return 0;

    uint32_t vcount = (uint32_t)mesh->num_indices;
    if (corner >= vcount)
        return 0;

    if (!mesh->vertex_position.values.data || mesh->vertex_position.values.count == 0)
        return 0;

    uint32_t pi = fbx_stream_index_u32(&mesh->vertex_position.indices, corner);
    if (pi >= mesh->vertex_position.values.count)
        return 0;

    ufbx_vec3 pv = mesh->vertex_position.values.data[pi];
    vec3 p = {(float)pv.x, (float)pv.y, (float)pv.z};
    p = fbx_mat4_mul_point(world, p);
    dst->px = p.x;
    dst->py = p.y;
    dst->pz = p.z;

    dst->nx = 0.0f;
    dst->ny = 0.0f;
    dst->nz = 1.0f;

    if (mesh->vertex_normal.values.data && mesh->vertex_normal.values.count > 0)
    {
        uint32_t ni = fbx_stream_index_u32(&mesh->vertex_normal.indices, corner);
        if (ni < mesh->vertex_normal.values.count)
        {
            ufbx_vec3 nv = mesh->vertex_normal.values.data[ni];
            vec3 n = {(float)nv.x, (float)nv.y, (float)nv.z};
            n = fbx_normal_xform(world, n);
            dst->nx = n.x;
            dst->ny = n.y;
            dst->nz = n.z;
        }
    }

    dst->u = 0.0f;
    dst->v = 0.0f;

    if (mesh->vertex_uv.values.data && mesh->vertex_uv.values.count > 0)
    {
        uint32_t uvi = fbx_stream_index_u32(&mesh->vertex_uv.indices, corner);
        if (uvi < mesh->vertex_uv.values.count)
        {
            ufbx_vec2 uv = mesh->vertex_uv.values.data[uvi];
            dst->u = (float)uv.x;
            dst->v = 1.0f - (float)uv.y;
        }
    }

    dst->tx = 1.0f;
    dst->ty = 0.0f;
    dst->tz = 0.0f;
    dst->tw = 1.0f;

    if (want_tangent && mesh->vertex_tangent.values.data && mesh->vertex_tangent.values.count > 0)
    {
        uint32_t ti = fbx_stream_index_u32(&mesh->vertex_tangent.indices, corner);
        if (ti < mesh->vertex_tangent.values.count)
        {
            ufbx_vec3 tv = mesh->vertex_tangent.values.data[ti];
            vec3 t = {(float)tv.x, (float)tv.y, (float)tv.z};
            t = fbx_normal_xform(world, t);
            dst->tx = t.x;
            dst->ty = t.y;
            dst->tz = t.z;

            float w = 1.0f;
            float det = fbx_mat3_det_from_mat4(world);
            if (det < 0.0f)
                w = -w;
            dst->tw = w;
        }
    }

    return 1;
}

static int fbx_emit_mesh_part(asset_manager_t *am, const char *path, vector_t *mat_map, model_raw_t *raw, const ufbx_node *node, const ufbx_mesh *mesh, const ufbx_mesh_part *part, mat4 world)
{
    if (!am || !path || !raw || !node || !mesh || !part)
        return 0;

    uint32_t vcount = (uint32_t)mesh->num_indices;
    if (vcount == 0)
        return 1;

    model_vertex_t *vtx = (model_vertex_t *)malloc((size_t)vcount * sizeof(model_vertex_t));
    if (!vtx)
        return 0;
    memset(vtx, 0, (size_t)vcount * sizeof(model_vertex_t));

    int has_tangent = 0;
    if (mesh->vertex_tangent.values.data && mesh->vertex_tangent.values.count > 0)
        has_tangent = 1;

    for (uint32_t i = 0; i < vcount; ++i)
    {
        model_vertex_t vv;
        memset(&vv, 0, sizeof(vv));
        if (!fbx_read_vertex_corner(&vv, mesh, i, world, has_tangent))
        {
            free(vtx);
            return 0;
        }
        vtx[i] = vv;
    }

    size_t face_count = part->face_indices.count;
    if (face_count == 0)
    {
        free(vtx);
        return 1;
    }

    uint32_t tri_cap = (uint32_t)(face_count * 2u);
    if (tri_cap < 8u)
        tri_cap = 8u;

    uint32_t *idx = (uint32_t *)malloc((size_t)tri_cap * 3u * sizeof(uint32_t));
    if (!idx)
    {
        free(vtx);
        return 0;
    }

    uint32_t w = 0;
    uint32_t tmp[64];

    for (size_t fi = 0; fi < face_count; ++fi)
    {
        uint32_t face_ix = (uint32_t)part->face_indices.data[fi];
        if (face_ix >= mesh->faces.count)
            continue;

        ufbx_face face = mesh->faces.data[face_ix];
        size_t tris = ufbx_triangulate_face(tmp, 64, mesh, face);
        if (tris == 0)
            continue;

        uint32_t need = (uint32_t)(tris * 3u);
        if (w + need > tri_cap * 3u)
        {
            uint32_t new_cap = tri_cap;
            while (w + need > new_cap * 3u)
                new_cap = new_cap ? new_cap * 2u : 64u;

            uint32_t *nidx = (uint32_t *)realloc(idx, (size_t)new_cap * 3u * sizeof(uint32_t));
            if (!nidx)
            {
                free(idx);
                free(vtx);
                return 0;
            }

            idx = nidx;
            tri_cap = new_cap;
        }

        for (size_t ti = 0; ti < tris * 3u; ++ti)
        {
            uint32_t v = tmp[ti];
            if (v < vcount)
                idx[w++] = v;
        }
    }

    if (w == 0)
    {
        free(idx);
        free(vtx);
        return 1;
    }

    for (uint32_t k = 0; k < w; ++k)
    {
        if (idx[k] >= vcount)
        {
            FBX_LOGE("fbx: bad index %u >= vcount %u (node=%s)", idx[k], vcount, node->name.data ? node->name.data : "(unnamed)");
            free(idx);
            free(vtx);
            return 0;
        }
    }

    if (!has_tangent)
        fbx_generate_tangents(vtx, vcount, idx, w);

    model_cpu_lod_t lod0;
    memset(&lod0, 0, sizeof(lod0));
    lod0.vertices = vtx;
    lod0.vertex_count = vcount;
    lod0.indices = idx;
    lod0.index_count = w;

    model_cpu_submesh_t sm;
    memset(&sm, 0, sizeof(sm));
    sm.lods = vector_impl_create_vector(sizeof(model_cpu_lod_t));
    vector_impl_push_back(&sm.lods, &lod0);

    sm.material_name = NULL;

    const ufbx_material *umat = NULL;
    if (part->index < mesh->materials.count)
        umat = mesh->materials.data[part->index];

    sm.material = fbx_get_or_make_mat(am, path, mat_map, umat);
    sm.aabb = fbx_aabb_from_vertices(vtx, vcount);
    sm.flags = (uint8_t)(sm.flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);

    vector_impl_push_back(&raw->submeshes, &sm);
    return 1;
}

static int fbx_emit_node(asset_manager_t *am, const char *path, vector_t *mat_map, model_raw_t *raw, const ufbx_node *node)
{
    if (!node)
        return 1;

    mat4 world = fbx_mat4_from_ufbx_matrix(node->geometry_to_world);

    if (node->mesh)
    {
        const ufbx_mesh *mesh = node->mesh;
        if (mesh->material_parts.count > 0)
        {
            for (size_t pi = 0; pi < mesh->material_parts.count; ++pi)
            {
                const ufbx_mesh_part *part = &mesh->material_parts.data[pi];
                if (!fbx_emit_mesh_part(am, path, mat_map, raw, node, mesh, part, world))
                    return 0;
            }
        }
        else
        {
            size_t fc = mesh->faces.count;
            uint32_t *faces = NULL;

            if (fc > 0)
            {
                faces = (uint32_t *)malloc(fc * sizeof(uint32_t));
                if (!faces)
                    return 0;
                for (size_t i = 0; i < fc; ++i)
                    faces[i] = (uint32_t)i;
            }

            ufbx_mesh_part part;
            memset(&part, 0, sizeof(part));
            part.index = 0;
            part.face_indices.data = faces;
            part.face_indices.count = fc;

            int ok = fbx_emit_mesh_part(am, path, mat_map, raw, node, mesh, &part, world);

            free(faces);

            if (!ok)
                return 0;
        }
    }

    for (size_t i = 0; i < node->children.count; ++i)
    {
        if (!fbx_emit_node(am, path, mat_map, raw, node->children.data[i]))
            return 0;
    }

    return 1;
}

bool asset_model_fbx_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, ihandle_t *out_handle)
{
    if (out_handle)
        *out_handle = ihandle_invalid();

    if (!am || !out_asset || !path)
        return false;
    if (path_is_ptr)
        return false;

    if (!fbx_quick_verify(path))
    {
        FBX_LOGE("fbx load failed: not .fbx (path='%s')", path);
        return false;
    }

    ufbx_load_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.load_external_files = true;

    ufbx_error error;
    memset(&error, 0, sizeof(error));

    ufbx_scene *scene = ufbx_load_file(path, &opts, &error);
    if (!scene)
    {
        FBX_LOGE("ufbx_load_file failed: %s", error.description.data ? error.description.data : "(no error desc)");
        return false;
    }

    model_raw_t raw = model_raw_make();
    raw.mtllib_path = NULL;
    raw.mtllib = ihandle_invalid();

    vector_t mat_map = vector_impl_create_vector(sizeof(fbx_mat_entry_t));

    if (scene->root_node && scene->root_node->children.count > 0)
    {
        for (size_t i = 0; i < scene->root_node->children.count; ++i)
        {
            const ufbx_node *n = scene->root_node->children.data[i];
            if (!n)
                continue;
            if (!fbx_emit_node(am, path, &mat_map, &raw, n))
            {
                vector_impl_free(&mat_map);
                model_raw_destroy(&raw);
                ufbx_free_scene(scene);
                return false;
            }
        }
    }
    else if (scene->nodes.count > 0)
    {
        for (size_t i = 0; i < scene->nodes.count; ++i)
        {
            const ufbx_node *n = scene->nodes.data[i];
            if (!n || n->parent)
                continue;
            if (!fbx_emit_node(am, path, &mat_map, &raw, n))
            {
                vector_impl_free(&mat_map);
                model_raw_destroy(&raw);
                ufbx_free_scene(scene);
                return false;
            }
        }
    }

    vector_impl_free(&mat_map);
    ufbx_free_scene(scene);

    if (raw.submeshes.size == 0)
    {
        FBX_LOGE("fbx load failed: no submeshes emitted");
        model_raw_destroy(&raw);
        return false;
    }

    model_raw_generate_lods(&raw);

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;

    return true;
}

bool asset_model_fbx_init(asset_manager_t *am, asset_any_t *asset)
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

            for (uint32_t k = 0; k < cl->index_count; ++k)
            {
                if (cl->indices[k] >= cl->vertex_count)
                {
                    FBX_LOGE("fbx: skipping lod with bad indices (%u >= %u)", cl->indices[k], cl->vertex_count);
                    goto skip_lod;
                }
            }

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

            fbx_vao_setup_model_vertex();

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            vector_impl_push_back(&gm.lods, &glod);
            uploaded++;

            if (li == 0)
                gm.flags |= (uint8_t)MESH_FLAG_LOD0_READY;

        skip_lod:
            (void)0;
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

void asset_model_fbx_cleanup(asset_manager_t *am, asset_any_t *asset)
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

static bool asset_model_fbx_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;
    if (!path || path_is_ptr)
        return false;

    const char *ext = strrchr(path, '.');
    if (!ext)
        return false;

    if (!strcmp(ext, ".fbx") || !strcmp(ext, ".FBX"))
        return true;

    return false;
}

asset_module_desc_t asset_module_model_fbx(void)
{
    asset_module_desc_t m = {0};
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL_FBX";
    m.load_fn = asset_model_fbx_load;
    m.init_fn = asset_model_fbx_init;
    m.cleanup_fn = asset_model_fbx_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    m.can_load_fn = asset_model_fbx_can_load;
    return m;
}
