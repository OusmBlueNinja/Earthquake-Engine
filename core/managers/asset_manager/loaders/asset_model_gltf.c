#include "asset_model_gltf.h"

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
#include "asset_manager/asset_types/image.h"
#include "vector.h"
#include "asset_image.h"
#include "systems/model_lod.h"
#include "types/mat4.h"
#include "types/vec3.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

typedef struct mdl_gltf_mat_entry_t
{
    const cgltf_material *m;
    ihandle_t h;
} mdl_gltf_mat_entry_t;

static char *mdl_strdup(const char *s)
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

static char *mdl_path_dirname_dup(const char *path)
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

static char *mdl_path_join_dup(const char *a, const char *b)
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

static float mdl_clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

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

static cgltf_attribute *mdl_gltf_find_attr(cgltf_primitive *prim, cgltf_attribute_type type, int index)
{
    if (!prim)
        return NULL;
    for (cgltf_size i = 0; i < prim->attributes_count; ++i)
    {
        cgltf_attribute *a = &prim->attributes[i];
        if (a->type == type && a->index == index)
            return a;
    }
    return NULL;
}

static const cgltf_image *mdl_gltf_tex_image(const cgltf_texture *t)
{
    if (!t)
        return NULL;
    if (t->image)
        return t->image;
    if (t->basisu_image)
        return t->basisu_image;
    return NULL;
}

static vec3 mdl_mat4_mul_point(mat4 m, vec3 p)
{
    vec3 o;
    o.x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    o.y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    o.z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return o;
}

static vec3 mdl_mat4_mul_dir(mat4 m, vec3 v)
{
    vec3 o;
    o.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z;
    o.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z;
    o.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z;
    return o;
}

static vec3 mdl_vec3_norm_safe(vec3 v)
{
    float l2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (l2 < 1e-20f)
        return (vec3){0.0f, 0.0f, 1.0f};
    float inv = 1.0f / sqrtf(l2);
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
}

static vec3 mdl_vec3_mul(vec3 a, float s)
{
    return (vec3){a.x * s, a.y * s, a.z * s};
}

static float mdl_mat3_det_from_mat4(mat4 m)
{
    float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];
    return a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
}

static vec3 mdl_normal_xform(mat4 m, vec3 n)
{
    float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];

    float b01 = a22 * a11 - a12 * a21;
    float b11 = -a22 * a10 + a12 * a20;
    float b21 = a21 * a10 - a11 * a20;

    float det = a00 * b01 + a01 * b11 + a02 * b21;

    if (fabsf(det) < 1e-20f)
        return mdl_vec3_norm_safe(mdl_mat4_mul_dir(m, n));

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

    return mdl_vec3_norm_safe(o);
}

static mat4 mdl_mat4_from_quat(float x, float y, float z, float w)
{
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;
    float wx = w * x;
    float wy = w * y;
    float wz = w * z;

    mat4 r = mat4_identity();

    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);

    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);

    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);

    return r;
}

static mat4 mdl_mat4_from_trs(vec3 t, float qx, float qy, float qz, float qw, vec3 s)
{
    mat4 T = mat4_translate(t);
    mat4 R = mdl_mat4_from_quat(qx, qy, qz, qw);
    mat4 S = mat4_scale(s);
    return mat4_mul(mat4_mul(T, R), S);
}

static mat4 mdl_node_local_mtx(const cgltf_node *n)
{
    if (!n)
        return mat4_identity();

    if (n->has_matrix)
    {
        mat4 m;
        for (int i = 0; i < 16; ++i)
            m.m[i] = n->matrix[i];
        return m;
    }

    vec3 t = {0.0f, 0.0f, 0.0f};
    vec3 s = {1.0f, 1.0f, 1.0f};
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;

    if (n->has_translation)
        t = (vec3){n->translation[0], n->translation[1], n->translation[2]};
    if (n->has_scale)
        s = (vec3){n->scale[0], n->scale[1], n->scale[2]};
    if (n->has_rotation)
    {
        qx = n->rotation[0];
        qy = n->rotation[1];
        qz = n->rotation[2];
        qw = n->rotation[3];
    }

    return mdl_mat4_from_trs(t, qx, qy, qz, qw, s);
}

static int mdl_b64_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return (int)(c - 'A');
    if (c >= 'a' && c <= 'z')
        return (int)(c - 'a') + 26;
    if (c >= '0' && c <= '9')
        return (int)(c - '0') + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    if (c == '=')
        return -2;
    return -1;
}

static uint8_t *mdl_b64_decode(const char *s, size_t n, size_t *out_n)
{
    if (!s || !out_n)
        return NULL;

    size_t cap = (n / 4 + 1) * 3;
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out)
        return NULL;

    size_t w = 0;
    int q[4] = {0, 0, 0, 0};
    int qi = 0;

    for (size_t i = 0; i < n; ++i)
    {
        unsigned char c = (unsigned char)s[i];
        if (isspace((int)c))
            continue;
        int v = mdl_b64_val(c);
        if (v < 0 && v != -2)
            continue;

        q[qi++] = v;
        if (qi == 4)
        {
            int a = q[0], b = q[1], c2 = q[2], d = q[3];
            if (a < 0 || b < 0)
                break;

            uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)(c2 < 0 ? 0 : c2) << 6) | (uint32_t)(d < 0 ? 0 : d);

            out[w++] = (uint8_t)((triple >> 16) & 0xFF);
            if (c2 != -2)
                out[w++] = (uint8_t)((triple >> 8) & 0xFF);
            if (d != -2)
                out[w++] = (uint8_t)(triple & 0xFF);

            qi = 0;
        }
    }

    *out_n = w;
    return out;
}

typedef struct asset_image_mem_desc_t
{
    void *bytes;
    size_t bytes_n;
    char *debug_name;
} asset_image_mem_desc_t;

static ihandle_t mdl_gltf_request_image(asset_manager_t *am, const char *gltf_path, cgltf_data *data, const cgltf_image *img)
{
    if (!am || !img || !gltf_path)
        return ihandle_invalid();

    if (img->uri && img->uri[0] && strncmp(img->uri, "data:", 5))
    {
        char *dir = mdl_path_dirname_dup(gltf_path);
        char *full = dir ? mdl_path_join_dup(dir, img->uri) : NULL;
        free(dir);
        if (!full)
            return ihandle_invalid();
        ihandle_t h = asset_manager_request(am, ASSET_IMAGE, full);
        free(full);
        return h;
    }

    asset_image_mem_desc_t *desc = (asset_image_mem_desc_t *)malloc(sizeof(asset_image_mem_desc_t));
    if (!desc)
        return ihandle_invalid();
    memset(desc, 0, sizeof(*desc));

    if (img->name && img->name[0])
        desc->debug_name = mdl_strdup(img->name);
    else
        desc->debug_name = mdl_strdup("gltf_embedded_image");

    if (img->uri && img->uri[0] && !strncmp(img->uri, "data:", 5))
    {
        const char *comma = strchr(img->uri, ',');
        if (!comma)
        {
            free(desc->debug_name);
            free(desc);
            return ihandle_invalid();
        }

        const char *meta = img->uri + 5;
        size_t meta_n = (size_t)(comma - meta);

        int is_b64 = 0;
        const char *b64 = comma + 1;
        for (size_t i = 0; i + 6 <= meta_n; ++i)
        {
            if (!memcmp(meta + i, "base64", 6))
            {
                is_b64 = 1;
                break;
            }
        }

        if (!is_b64)
        {
            free(desc->debug_name);
            free(desc);
            return ihandle_invalid();
        }

        size_t out_n = 0;
        uint8_t *bytes = mdl_b64_decode(b64, strlen(b64), &out_n);
        if (!bytes || !out_n)
        {
            free(bytes);
            free(desc->debug_name);
            free(desc);
            return ihandle_invalid();
        }

        desc->bytes = bytes;
        desc->bytes_n = out_n;

        ihandle_t h = asset_manager_request_ptr(am, ASSET_IMAGE, desc);
        if (!ihandle_is_valid(h))
        {
            free(bytes);
            free(desc->debug_name);
            free(desc);
        }
        return h;
    }

    if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data)
    {
        const uint8_t *base = (const uint8_t *)img->buffer_view->buffer->data;
        size_t off = (size_t)img->buffer_view->offset;
        size_t sz = (size_t)img->buffer_view->size;

        uint8_t *bytes = (uint8_t *)malloc(sz);
        if (!bytes)
        {
            free(desc->debug_name);
            free(desc);
            return ihandle_invalid();
        }

        memcpy(bytes, base + off, sz);

        desc->bytes = bytes;
        desc->bytes_n = sz;

        ihandle_t h = asset_manager_request_ptr(am, ASSET_IMAGE, desc);
        if (!ihandle_is_valid(h))
        {
            free(bytes);
            free(desc->debug_name);
            free(desc);
        }
        return h;
    }

    free(desc->debug_name);
    free(desc);
    (void)data;
    return ihandle_invalid();
}

static ihandle_t mdl_gltf_material_to_handle(asset_manager_t *am, const char *gltf_path, cgltf_data *data, const cgltf_material *m)
{
    asset_material_t cur = material_make_default(0);

    if (m && m->name && m->name[0])
    {
        free(cur.name);
        cur.name = (char *)malloc(strlen(m->name) + 1);
        if (cur.name)
            memcpy(cur.name, m->name, strlen(m->name) + 1);
    }

    if (!m)
    {
        cur.opacity = 1.0f;
        return asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);
    }

    material_set_flag(&cur, MAT_FLAG_DOUBLE_SIDED, m->double_sided ? true : false);

    material_set_flag(&cur, MAT_FLAG_ALPHA_CUTOUT, (m->alpha_mode == cgltf_alpha_mode_mask));
    material_set_flag(&cur, MAT_FLAG_ALPHA_BLEND, (m->alpha_mode == cgltf_alpha_mode_blend));

    if (m->has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness *p = &m->pbr_metallic_roughness;

        cur.albedo = (vec3){p->base_color_factor[0], p->base_color_factor[1], p->base_color_factor[2]};
        cur.opacity = mdl_clamp01(p->base_color_factor[3]);
        cur.metallic = mdl_clamp01(p->metallic_factor);
        cur.roughness = mdl_clamp01(p->roughness_factor);

        if (p->base_color_texture.texture)
        {
            const cgltf_image *img = mdl_gltf_tex_image(p->base_color_texture.texture);
            cur.albedo_tex = mdl_gltf_request_image(am, gltf_path, data, img);
        }

        if (p->metallic_roughness_texture.texture)
        {
            const cgltf_image *img = mdl_gltf_tex_image(p->metallic_roughness_texture.texture);
            ihandle_t t = mdl_gltf_request_image(am, gltf_path, data, img);
            cur.roughness_tex = t;
            cur.metallic_tex = t;
        }
    }

    cur.emissive = (vec3){m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]};

    if (m->emissive_texture.texture)
    {
        const cgltf_image *img = mdl_gltf_tex_image(m->emissive_texture.texture);
        cur.emissive_tex = mdl_gltf_request_image(am, gltf_path, data, img);
    }

    if (m->normal_texture.texture)
    {
        const cgltf_image *img = mdl_gltf_tex_image(m->normal_texture.texture);
        cur.normal_tex = mdl_gltf_request_image(am, gltf_path, data, img);
        if (m->normal_texture.scale > 0.0f)
            cur.normal_strength = m->normal_texture.scale;
    }

    if (m->occlusion_texture.texture)
    {
        const cgltf_image *img = mdl_gltf_tex_image(m->occlusion_texture.texture);
        cur.occlusion_tex = mdl_gltf_request_image(am, gltf_path, data, img);
    }

    if (m->alpha_mode == cgltf_alpha_mode_blend)
    {
        cur.opacity = mdl_clamp01(cur.opacity);
    }
    else
    {
        cur.opacity = 1.0f;
    }

    return asset_manager_submit_raw(am, ASSET_MATERIAL, &cur);
}

static ihandle_t mdl_gltf_get_or_make_mat(asset_manager_t *am, const char *gltf_path, cgltf_data *data, vector_t *map, const cgltf_material *m)
{
    if (!map)
        return ihandle_invalid();

    for (uint32_t i = 0; i < map->size; ++i)
    {
        mdl_gltf_mat_entry_t *e = (mdl_gltf_mat_entry_t *)vector_impl_at(map, i);
        if (e && e->m == m)
            return e->h;
    }

    ihandle_t h = mdl_gltf_material_to_handle(am, gltf_path, data, m);
    mdl_gltf_mat_entry_t e;
    e.m = m;
    e.h = h;
    vector_impl_push_back(map, &e);
    return h;
}

static bool mdl_gltf_read_vtx(model_vertex_t *dst, cgltf_primitive *prim, cgltf_size i, mat4 world, int want_tangent)
{
    cgltf_attribute *a_pos = mdl_gltf_find_attr(prim, cgltf_attribute_type_position, 0);
    if (!a_pos || !a_pos->data)
        return false;

    float v3[3] = {0, 0, 0};
    cgltf_accessor_read_float(a_pos->data, i, v3, 3);
    vec3 p = {v3[0], v3[1], v3[2]};
    p = mdl_mat4_mul_point(world, p);
    dst->px = p.x;
    dst->py = p.y;
    dst->pz = p.z;

    cgltf_attribute *a_nrm = mdl_gltf_find_attr(prim, cgltf_attribute_type_normal, 0);
    if (a_nrm && a_nrm->data)
    {
        float n3[3] = {0, 0, 1};
        cgltf_accessor_read_float(a_nrm->data, i, n3, 3);
        vec3 n = {n3[0], n3[1], n3[2]};
        n = mdl_normal_xform(world, n);
        dst->nx = n.x;
        dst->ny = n.y;
        dst->nz = n.z;
    }
    else
    {
        dst->nx = 0.0f;
        dst->ny = 0.0f;
        dst->nz = 1.0f;
    }

    cgltf_attribute *a_uv0 = mdl_gltf_find_attr(prim, cgltf_attribute_type_texcoord, 0);
    if (a_uv0 && a_uv0->data)
    {
        float uv2[2] = {0, 0};
        cgltf_accessor_read_float(a_uv0->data, i, uv2, 2);
        dst->u = uv2[0];
        dst->v = uv2[1];
    }
    else
    {
        dst->u = 0.0f;
        dst->v = 0.0f;
    }

    dst->tx = 1.0f;
    dst->ty = 0.0f;
    dst->tz = 0.0f;
    dst->tw = 1.0f;

    if (want_tangent)
    {
        cgltf_attribute *a_tan = mdl_gltf_find_attr(prim, cgltf_attribute_type_tangent, 0);
        if (a_tan && a_tan->data)
        {
            float t4[4] = {1, 0, 0, 1};
            cgltf_accessor_read_float(a_tan->data, i, t4, 4);
            vec3 t = {t4[0], t4[1], t4[2]};
            t = mdl_normal_xform(world, t);
            dst->tx = t.x;
            dst->ty = t.y;
            dst->tz = t.z;
            float w = t4[3];
            float det = mdl_mat3_det_from_mat4(world);
            if (det < 0.0f)
                w = -w;
            dst->tw = w;
        }
    }

    return true;
}

static uint32_t *mdl_gltf_build_indices(cgltf_primitive *prim, uint32_t vcount, uint32_t *out_icount)
{
    *out_icount = 0;

    if (!prim)
        return NULL;

    if (prim->indices)
    {
        uint32_t base_count = (uint32_t)prim->indices->count;

        if (prim->type == cgltf_primitive_type_triangles)
        {
            uint32_t *idx = (uint32_t *)malloc((size_t)base_count * sizeof(uint32_t));
            if (!idx)
                return NULL;
            for (uint32_t i = 0; i < base_count; ++i)
                idx[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
            *out_icount = base_count;
            return idx;
        }

        if (prim->type == cgltf_primitive_type_triangle_strip)
        {
            if (base_count < 3)
                return NULL;
            uint32_t tri_count = (base_count - 2) * 3;
            uint32_t *idx = (uint32_t *)malloc((size_t)tri_count * sizeof(uint32_t));
            if (!idx)
                return NULL;
            uint32_t w = 0;
            for (uint32_t i = 0; i + 2 < base_count; ++i)
            {
                uint32_t a = (uint32_t)cgltf_accessor_read_index(prim->indices, i + 0);
                uint32_t b = (uint32_t)cgltf_accessor_read_index(prim->indices, i + 1);
                uint32_t c = (uint32_t)cgltf_accessor_read_index(prim->indices, i + 2);
                if ((i & 1) == 0)
                {
                    idx[w++] = a;
                    idx[w++] = b;
                    idx[w++] = c;
                }
                else
                {
                    idx[w++] = b;
                    idx[w++] = a;
                    idx[w++] = c;
                }
            }
            *out_icount = w;
            return idx;
        }

        if (prim->type == cgltf_primitive_type_triangle_fan)
        {
            if (base_count < 3)
                return NULL;
            uint32_t tri_count = (base_count - 2) * 3;
            uint32_t *idx = (uint32_t *)malloc((size_t)tri_count * sizeof(uint32_t));
            if (!idx)
                return NULL;
            uint32_t w = 0;
            uint32_t a0 = (uint32_t)cgltf_accessor_read_index(prim->indices, 0);
            for (uint32_t i = 1; i + 1 < base_count; ++i)
            {
                uint32_t b = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
                uint32_t c = (uint32_t)cgltf_accessor_read_index(prim->indices, i + 1);
                idx[w++] = a0;
                idx[w++] = b;
                idx[w++] = c;
            }
            *out_icount = w;
            return idx;
        }

        return NULL;
    }

    if (prim->type != cgltf_primitive_type_triangles)
        return NULL;

    uint32_t *idx = (uint32_t *)malloc((size_t)vcount * sizeof(uint32_t));
    if (!idx)
        return NULL;
    for (uint32_t i = 0; i < vcount; ++i)
        idx[i] = i;
    *out_icount = vcount;
    return idx;
}

static bool mdl_gltf_quick_verify(const char *path)
{
    if (!path)
        return false;

    const char *ext = strrchr(path, '.');
    if (ext && (!strcmp(ext, ".gltf") || !strcmp(ext, ".GLTF") || !strcmp(ext, ".glb") || !strcmp(ext, ".GLB")))
        return true;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint32_t magic = 0;
    size_t n = fread(&magic, 1, 4, f);
    fclose(f);
    if (n != 4)
        return false;
    if (magic == 0x46546C67u)
        return true;

    return false;
}

static void mdl_gltf_free_raw(model_raw_t *raw)
{
    model_raw_destroy(raw);
}

static aabb_t mdl_aabb_from_vertices(const model_vertex_t *vtx, uint32_t vcount)
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

static void mdl_generate_tangents(model_vertex_t *vtx, uint32_t vcount, const uint32_t *idx, uint32_t icount)
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

        vec3 sdir = mdl_vec3_mul(vec3_sub(mdl_vec3_mul(e1, dv2), mdl_vec3_mul(e2, dv1)), r);
        vec3 tdir = mdl_vec3_mul(vec3_sub(mdl_vec3_mul(e2, du1), mdl_vec3_mul(e1, du2)), r);

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

        n = mdl_vec3_norm_safe(n);

        float ndott = vec3_dot(n, t);
        vec3 ortho = vec3_sub(t, mdl_vec3_mul(n, ndott));
        float o2 = vec3_dot(ortho, ortho);
        if (o2 < 1e-20f)
        {
            vtx[i].tx = 1.0f;
            vtx[i].ty = 0.0f;
            vtx[i].tz = 0.0f;
            vtx[i].tw = 1.0f;
            continue;
        }

        vec3 tnorm = mdl_vec3_norm_safe(ortho);

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

static bool mdl_emit_node_mesh(asset_manager_t *am, const char *path, cgltf_data *data, vector_t *mat_map, model_raw_t *raw, const cgltf_node *node, mat4 parent_world)
{
    mat4 local = mdl_node_local_mtx(node);
    mat4 world = mat4_mul(parent_world, local);

    if (node && node->mesh)
    {
        cgltf_mesh *mesh = node->mesh;

        for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi)
        {
            cgltf_primitive *prim = &mesh->primitives[pi];
            if (!prim)
                continue;

            cgltf_attribute *a_pos = mdl_gltf_find_attr(prim, cgltf_attribute_type_position, 0);
            if (!a_pos || !a_pos->data)
                continue;

            if (!(prim->type == cgltf_primitive_type_triangles ||
                  prim->type == cgltf_primitive_type_triangle_strip ||
                  prim->type == cgltf_primitive_type_triangle_fan))
                continue;

            uint32_t vcount = (uint32_t)a_pos->data->count;
            if (!vcount)
                continue;

            int has_tangent = 0;
            cgltf_attribute *a_tan = mdl_gltf_find_attr(prim, cgltf_attribute_type_tangent, 0);
            if (a_tan && a_tan->data)
                has_tangent = 1;

            model_vertex_t *vtx = (model_vertex_t *)malloc((size_t)vcount * sizeof(model_vertex_t));
            if (!vtx)
                return false;

            for (uint32_t i = 0; i < vcount; ++i)
            {
                model_vertex_t v;
                memset(&v, 0, sizeof(v));
                if (!mdl_gltf_read_vtx(&v, prim, (cgltf_size)i, world, has_tangent))
                {
                    free(vtx);
                    return false;
                }
                vtx[i] = v;
            }

            uint32_t icount = 0;
            uint32_t *idx = mdl_gltf_build_indices(prim, vcount, &icount);
            if (!idx || !icount)
            {
                free(idx);
                free(vtx);
                return false;
            }

            if (!has_tangent)
                mdl_generate_tangents(vtx, vcount, idx, icount);

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
            sm.material_name = NULL;
            sm.material = mdl_gltf_get_or_make_mat(am, path, data, mat_map, prim->material);

            sm.aabb = mdl_aabb_from_vertices(vtx, vcount);
            sm.flags = (uint8_t)(sm.flags | (uint8_t)CPU_SUBMESH_FLAG_HAS_AABB);

            vector_impl_push_back(&raw->submeshes, &sm);
        }
    }

    if (node && node->children_count)
    {
        for (cgltf_size ci = 0; ci < node->children_count; ++ci)
        {
            cgltf_node *ch = node->children[ci];
            if (!ch)
                continue;
            if (!mdl_emit_node_mesh(am, path, data, mat_map, raw, ch, world))
                return false;
        }
    }

    return true;
}

bool asset_model_gltf_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset)
{
    if (!am || !out_asset || !path)
        return false;
    if (path_is_ptr)
        return false;

    if (!mdl_gltf_quick_verify(path))
        return false;

    cgltf_options opt;
    memset(&opt, 0, sizeof(opt));

    cgltf_data *data = NULL;
    cgltf_result r = cgltf_parse_file(&opt, path, &data);
    if (r != cgltf_result_success || !data)
        return false;

    r = cgltf_load_buffers(&opt, data, path);
    if (r != cgltf_result_success)
    {
        cgltf_free(data);
        return false;
    }

    r = cgltf_validate(data);
    if (r != cgltf_result_success)
    {
        cgltf_free(data);
        return false;
    }

    model_raw_t raw = model_raw_make();
    raw.mtllib_path = NULL;
    raw.mtllib = ihandle_invalid();

    vector_t mat_map = vector_impl_create_vector(sizeof(mdl_gltf_mat_entry_t));

    cgltf_scene *scene = NULL;
    if (data->scene)
        scene = data->scene;
    else if (data->scenes_count)
        scene = &data->scenes[0];

    if (scene && scene->nodes_count)
    {
        mat4 I = mat4_identity();
        for (cgltf_size ni = 0; ni < scene->nodes_count; ++ni)
        {
            cgltf_node *n = scene->nodes[ni];
            if (!n)
                continue;
            if (!mdl_emit_node_mesh(am, path, data, &mat_map, &raw, n, I))
            {
                vector_impl_free(&mat_map);
                mdl_gltf_free_raw(&raw);
                cgltf_free(data);
                return false;
            }
        }
    }

    vector_impl_free(&mat_map);
    cgltf_free(data);

    model_raw_generate_lods(&raw);

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_MODEL;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.model_raw = raw;

    return true;
}

bool asset_model_gltf_init(asset_manager_t *am, asset_any_t *asset)
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

        // Used by renderer for transparent sorting, culling, etc.
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

void asset_model_gltf_cleanup(asset_manager_t *am, asset_any_t *asset)
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

asset_module_desc_t asset_module_model_gltf(void)
{
    asset_module_desc_t m;
    m.type = ASSET_MODEL;
    m.name = "ASSET_MODEL_GLTF";
    m.load_fn = asset_model_gltf_load;
    m.init_fn = asset_model_gltf_init;
    m.cleanup_fn = asset_model_gltf_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    return m;
}
