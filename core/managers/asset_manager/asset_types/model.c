#include "asset_manager/asset_types/model.h"

#include <stdlib.h>
#include <string.h>

model_raw_t model_raw_make(void)
{
    model_raw_t r;
    memset(&r, 0, sizeof(r));
    r.submeshes = vector_impl_create_vector(sizeof(model_cpu_submesh_t));
    r.mtllib_path = NULL;
    r.mtllib = ihandle_invalid();
    return r;
}

static void model_cpu_submesh_destroy(model_cpu_submesh_t *s)
{
    if (!s)
        return;

    free(s->vertices);
    free(s->indices);
    free(s->material_name);

    s->vertices = NULL;
    s->indices = NULL;
    s->material_name = NULL;
    s->vertex_count = 0;
    s->index_count = 0;
    s->material = ihandle_invalid();
}

void model_raw_destroy(model_raw_t *r)
{
    if (!r)
        return;

    for (uint32_t i = 0; i < r->submeshes.size; ++i)
    {
        model_cpu_submesh_t *s = (model_cpu_submesh_t *)vector_impl_at(&r->submeshes, i);
        model_cpu_submesh_destroy(s);
    }

    vector_impl_free(&r->submeshes);

    free(r->mtllib_path);
    r->mtllib_path = NULL;
    r->mtllib = ihandle_invalid();

    memset(r, 0, sizeof(*r));
}

asset_model_t asset_model_make(void)
{
    asset_model_t m;
    memset(&m, 0, sizeof(m));
    m.meshes = vector_impl_create_vector(sizeof(mesh_t));
    return m;
}

void asset_model_destroy_cpu_only(asset_model_t *m)
{
    if (!m)
        return;

    vector_impl_free(&m->meshes);
    memset(m, 0, sizeof(*m));
}
