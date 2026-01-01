#include "core/systems/scene_renderer/scene_renderer.h"

#include <math.h>

#include "renderer/renderer.h"

#include "core/systems/ecs/ecs.h"
#include "core/systems/ecs/view.h"
#include "core/systems/ecs/components/c_tag.h"
#include "core/systems/ecs/components/c_transform.h"
#include "core/systems/ecs/components/c_mesh_renderer.h"

#include "types/mat4.h"

static float sr_deg_to_rad(float d)
{
    return d * 0.01745329251994329577f;
}

static mat4 sr_trs(const c_transform_t *tr)
{
    mat4 T = mat4_translate(tr->position);

    float rx = sr_deg_to_rad(tr->rotation.x);
    float ry = sr_deg_to_rad(tr->rotation.y);
    float rz = sr_deg_to_rad(tr->rotation.z);

    mat4 Rx = mat4_rotate_x(rx);
    mat4 Ry = mat4_rotate_y(ry);
    mat4 Rz = mat4_rotate_z(rz);

    mat4 R = mat4_mul(mat4_mul(Rz, Ry), Rx);

    mat4 S = mat4_scale(tr->scale);

    return mat4_mul(mat4_mul(T, R), S);
}

void scene_renderer_render(renderer_t *r, ecs_world_t *scene)
{
    if (!r || !scene)
        return;

    static ecs_component_id_t s_tr_id = 0;
    static ecs_component_id_t s_mr_id = 0;

    if (!s_tr_id)
        s_tr_id = ecs_component_id_by_name(scene, "c_transform_t");
    if (!s_mr_id)
        s_mr_id = ecs_component_id_by_name(scene, "c_mesh_renderer_t");

    if (!s_tr_id || !s_mr_id)
        return;

    ecs_view_t v;
    ecs_component_id_t ids[2] = {s_tr_id, s_mr_id};
    if (!ecs_view_init(&v, scene, 2u, ids))
        return;

    ecs_entity_t e = 0;
    void *c[2];

    while (ecs_view_next(&v, &e, c))
    {
        c_transform_t *tr = (c_transform_t *)c[0];
        c_mesh_renderer_t *mr = (c_mesh_renderer_t *)c[1];

        if (!ihandle_is_valid(mr->model))
            continue;

        c_tag_t *tag = ecs_get(scene, e, c_tag_t);
        if (tag && tag->visible == 0)
            continue;

        mat4 model_mtx = sr_trs(tr);
        R_push_model(r, mr->model, model_mtx);
    }
}
