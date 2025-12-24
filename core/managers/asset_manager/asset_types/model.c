#include "asset_manager/asset_types/model.h"

#include <stdlib.h>
#include <string.h>
#include <float.h>

model_raw_t model_raw_make(void)
{
    model_raw_t r;
    memset(&r, 0, sizeof(r));
    r.submeshes = vector_impl_create_vector(sizeof(model_cpu_submesh_t));
    r.mtllib_path = NULL;
    r.mtllib = ihandle_invalid();
    r.lod_count = 1;
    return r;
}

static void model_cpu_lod_free(model_cpu_lod_t *l)
{
    if (!l)
        return;
    free(l->vertices);
    free(l->indices);
    memset(l, 0, sizeof(*l));
}

static void model_cpu_submesh_free(model_cpu_submesh_t *sm)
{
    if (!sm)
        return;

    for (uint32_t i = 0; i < sm->lods.size; ++i)
    {
        model_cpu_lod_t *l = (model_cpu_lod_t *)vector_impl_at(&sm->lods, i);
        model_cpu_lod_free(l);
    }
    vector_impl_free(&sm->lods);

    free(sm->material_name);
    sm->material_name = NULL;
    sm->material = ihandle_invalid();
}

void model_raw_destroy(model_raw_t *r)
{
    if (!r)
        return;

    for (uint32_t i = 0; i < r->submeshes.size; ++i)
    {
        model_cpu_submesh_t *sm = (model_cpu_submesh_t *)vector_impl_at(&r->submeshes, i);
        model_cpu_submesh_free(sm);
    }
    vector_impl_free(&r->submeshes);

    free(r->mtllib_path);
    r->mtllib_path = NULL;
    r->mtllib = ihandle_invalid();
    r->lod_count = 1;
}

asset_model_t asset_model_make(void)
{
    asset_model_t m;
    memset(&m, 0, sizeof(m));
    m.meshes = vector_impl_create_vector(sizeof(mesh_t));
    return m;
}

static void mesh_free_cpu_only(mesh_t *m)
{
    if (!m)
        return;
    vector_impl_free(&m->lods);
    m->material = ihandle_invalid();
    m->has_aabb = 0;
    m->local_aabb.min = (vec3){0, 0, 0};
    m->local_aabb.max = (vec3){0, 0, 0};
}

void asset_model_destroy_cpu_only(asset_model_t *m)
{
    if (!m)
        return;

    for (uint32_t i = 0; i < m->meshes.size; ++i)
    {
        mesh_t *mesh = (mesh_t *)vector_impl_at(&m->meshes, i);
        mesh_free_cpu_only(mesh);
    }
    vector_impl_free(&m->meshes);
}

static aabb_t aabb_empty(void)
{
    aabb_t b;
    b.min = (vec3){FLT_MAX, FLT_MAX, FLT_MAX};
    b.max = (vec3){-FLT_MAX, -FLT_MAX, -FLT_MAX};
    return b;
}

static void aabb_grow_point(aabb_t *b, vec3 p)
{
    if (p.x < b->min.x)
        b->min.x = p.x;
    if (p.y < b->min.y)
        b->min.y = p.y;
    if (p.z < b->min.z)
        b->min.z = p.z;

    if (p.x > b->max.x)
        b->max.x = p.x;
    if (p.y > b->max.y)
        b->max.y = p.y;
    if (p.z > b->max.z)
        b->max.z = p.z;
}

static aabb_t aabb_fix_if_empty(aabb_t b)
{
    if (b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z)
    {
        b.min = (vec3){0, 0, 0};
        b.max = (vec3){0, 0, 0};
    }
    return b;
}

aabb_t model_cpu_submesh_compute_aabb(const model_cpu_submesh_t *sm)
{
    if (!sm || sm->lods.size == 0)
        return (aabb_t){(vec3){0, 0, 0}, (vec3){0, 0, 0}};

    aabb_t b = aabb_empty();
    uint32_t any = 0;

    for (uint32_t li = 0; li < sm->lods.size; ++li)
    {
        const model_cpu_lod_t *lod = (const model_cpu_lod_t *)vector_impl_at((vector_t *)&sm->lods, li);
        if (!lod || !lod->vertices || lod->vertex_count == 0)
            continue;

        for (uint32_t vi = 0; vi < lod->vertex_count; ++vi)
        {
            const model_vertex_t *v = &lod->vertices[vi];
            aabb_grow_point(&b, (vec3){v->px, v->py, v->pz});
        }
        any = 1;
    }

    if (!any)
        return (aabb_t){(vec3){0, 0, 0}, (vec3){0, 0, 0}};

    return aabb_fix_if_empty(b);
}

void mesh_set_local_aabb_from_cpu(mesh_t *dst, const model_cpu_submesh_t *src)
{
    if (!dst)
        return;

    if (!src)
    {
        dst->has_aabb = 0;
        dst->local_aabb.min = (vec3){0, 0, 0};
        dst->local_aabb.max = (vec3){0, 0, 0};
        return;
    }

    dst->local_aabb = model_cpu_submesh_compute_aabb(src);
    dst->has_aabb = 1;
}
