#include "core/systems/scene_renderer/scene_renderer.h"

#include <math.h>

#include "renderer/renderer.h"

#include "core/systems/ecs/ecs.h"
#include "core/systems/ecs/view.h"
#include "core/systems/ecs/entity.h"
#include "core/systems/ecs/components/c_tag.h"
#include "core/systems/ecs/components/c_transform.h"
#include "core/systems/ecs/components/c_mesh_renderer.h"
#include "core/systems/ecs/components/c_light.h"

#include "types/mat4.h"
#include "core/renderer/light.h"

static float sr_deg_to_rad(float d)
{
    return d * 0.01745329251994329577f;
}

static vec3 sr_vec3_norm(vec3 v)
{
    float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 0.0000001f)
        return (vec3){0.0f, -1.0f, 0.0f};
    float inv = 1.0f / sqrtf(len2);
    return (vec3){v.x * inv, v.y * inv, v.z * inv};
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

static mat4 sr_tr_no_scale(const c_transform_t *tr)
{
    float rx = sr_deg_to_rad(tr->rotation.x);
    float ry = sr_deg_to_rad(tr->rotation.y);
    float rz = sr_deg_to_rad(tr->rotation.z);

    mat4 Rx = mat4_rotate_x(rx);
    mat4 Ry = mat4_rotate_y(ry);
    mat4 Rz = mat4_rotate_z(rz);

    return mat4_mul(mat4_mul(Rz, Ry), Rx);
}

static int sr_is_visible_in_hierarchy(ecs_world_t *w, ecs_entity_t e)
{
    ecs_entity_t cur = e;

    while (cur != 0 && ecs_entity_is_alive(w, cur))
    {
        c_tag_t *tag = ecs_get(w, cur, c_tag_t);
        if (tag && tag->visible == 0)
            return 0;

        cur = ecs_entity_get_parent(w, cur);
    }

    return 1;
}

static mat4 sr_world_matrix(ecs_world_t *w, ecs_entity_t e)
{
    mat4 mats[128];
    uint32_t mcount = 0;

    ecs_entity_t cur = e;

    while (cur != 0 && ecs_entity_is_alive(w, cur))
    {
        c_transform_t *tr = ecs_get(w, cur, c_transform_t);
        if (tr)
        {
            if (mcount < (uint32_t)(sizeof(mats) / sizeof(mats[0])))
                mats[mcount++] = sr_trs(tr);
        }

        cur = ecs_entity_get_parent(w, cur);
    }

    mat4 world = mat4_identity();

    while (mcount)
    {
        mcount--;
        world = mat4_mul(world, mats[mcount]);
    }

    return world;
}

static mat4 sr_world_rotation_matrix(ecs_world_t *w, ecs_entity_t e)
{
    mat4 mats[128];
    uint32_t mcount = 0;

    ecs_entity_t cur = e;

    while (cur != 0 && ecs_entity_is_alive(w, cur))
    {
        c_transform_t *tr = ecs_get(w, cur, c_transform_t);
        if (tr)
        {
            if (mcount < (uint32_t)(sizeof(mats) / sizeof(mats[0])))
                mats[mcount++] = sr_tr_no_scale(tr);
        }

        cur = ecs_entity_get_parent(w, cur);
    }

    mat4 worldR = mat4_identity();

    while (mcount)
    {
        mcount--;
        worldR = mat4_mul(worldR, mats[mcount]);
    }

    return worldR;
}

static vec3 sr_forward_from_euler_deg(vec3 rot_deg)
{
    float rx = sr_deg_to_rad(rot_deg.x);
    float ry = sr_deg_to_rad(rot_deg.y);
    float rz = sr_deg_to_rad(rot_deg.z);

    mat4 Rx = mat4_rotate_x(rx);
    mat4 Ry = mat4_rotate_y(ry);
    mat4 Rz = mat4_rotate_z(rz);

    mat4 R = mat4_mul(mat4_mul(Rz, Ry), Rx);

    vec3 f = (vec3){0.0f, 0.0f, -1.0f};

    vec3 o;
    o.x = R.m[0] * f.x + R.m[4] * f.y + R.m[8] * f.z;
    o.y = R.m[1] * f.x + R.m[5] * f.y + R.m[9] * f.z;
    o.z = R.m[2] * f.x + R.m[6] * f.y + R.m[10] * f.z;

    return o;
}

static light_t sr_make_light(ecs_world_t *scene, ecs_entity_t e, const c_transform_t *tr, const c_light_t *cl)
{
    light_t L;
    L.type = cl->type;
    L.color = cl->color;
    L.intensity = cl->intensity;
    L.radius = cl->radius;
    L.range = cl->range;

    L.position = tr ? tr->position : (vec3){0.0f, 0.0f, 0.0f};

    if (cl->type == LIGHT_DIRECTIONAL)
    {
        mat4 R = sr_world_rotation_matrix(scene, e);

        vec3 d;
        d.x = -R.m[8];
        d.y = -R.m[9];
        d.z = -R.m[10];

        L.direction = sr_vec3_norm(d);
    }
    else
    {
        if (tr)
            L.direction = sr_vec3_norm(sr_forward_from_euler_deg(tr->rotation));
        else
            L.direction = (vec3){0.0f, 0.0f, -1.0f};
    }

    return L;
}

void scene_renderer_render(renderer_t *r, ecs_world_t *scene)
{
    if (!r || !scene)
        return;

    static ecs_component_id_t s_tr_id = 0;
    static ecs_component_id_t s_mr_id = 0;
    static ecs_component_id_t s_li_id = 0;

    if (!s_tr_id)
        s_tr_id = ecs_component_id_by_name(scene, "c_transform_t");
    if (!s_mr_id)
        s_mr_id = ecs_component_id_by_name(scene, "c_mesh_renderer_t");
    if (!s_li_id)
        s_li_id = ecs_component_id_by_name(scene, "c_light_t");

    if (!s_tr_id)
        return;

    if (s_mr_id)
    {
        ecs_view_t v;
        ecs_component_id_t ids[2] = {s_tr_id, s_mr_id};
        if (ecs_view_init(&v, scene, 2u, ids))
        {
            ecs_entity_t e = 0;
            void *c[2];

            while (ecs_view_next(&v, &e, c))
            {
                c_mesh_renderer_t *mr = (c_mesh_renderer_t *)c[1];

                if (!sr_is_visible_in_hierarchy(scene, e))
                    continue;

                mat4 world_mtx = sr_world_matrix(scene, e);
                R_push_model(r, mr->model, world_mtx);
            }
        }
    }

    if (s_li_id)
    {
        ecs_view_t v;
        ecs_component_id_t ids[2] = {s_tr_id, s_li_id};
        if (ecs_view_init(&v, scene, 2u, ids))
        {
            ecs_entity_t e = 0;
            void *c[2];

            while (ecs_view_next(&v, &e, c))
            {
                c_transform_t *tr = (c_transform_t *)c[0];
                c_light_t *cl = (c_light_t *)c[1];

                if (!sr_is_visible_in_hierarchy(scene, e))
                    continue;

                light_t L = sr_make_light(scene, e, tr, cl);
                R_push_light(r, L);
            }
        }
    }
}
