#include "demo_layer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core.h"
#include "renderer/renderer.h"
#include "types/vec3.h"
#include "types/mat4.h"
#include "renderer/camera.h"
#include "renderer/light.h"
#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/material.h"
#include "asset_manager/asset_types/model.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static float demo_clampf(float x, float lo, float hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static float demo_maxf(float a, float b)
{
    return a > b ? a : b;
}

static float demo_len3(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

static float demo_mat4_max_scale_axis(mat4 m)
{
    float sx = demo_len3(m.m[0], m.m[1], m.m[2]);
    float sy = demo_len3(m.m[4], m.m[5], m.m[6]);
    float sz = demo_len3(m.m[8], m.m[9], m.m[10]);
    return demo_maxf(sx, demo_maxf(sy, sz));
}

static vec3 demo_mat4_transform_point(mat4 m, vec3 p)
{
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return (vec3){x, y, z};
}

static int demo_compute_model_aabb_from_gpu(asset_model_t *mdl, vec3 *out_min, vec3 *out_max)
{
    if (!mdl || mdl->meshes.size == 0)
        return 0;

    float minx = 1e30f, miny = 1e30f, minz = 1e30f;
    float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;

    for (uint32_t i = 0; i < mdl->meshes.size; ++i)
    {
        mesh_t *mesh = (mesh_t *)vector_impl_at(&mdl->meshes, i);
        if (!mesh || !mesh->vbo)
            continue;

        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);

        GLint sz = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &sz);
        if (sz <= 0)
            continue;

        size_t count = (size_t)sz / sizeof(model_vertex_t);
        if (count == 0)
            continue;

        model_vertex_t *tmp = (model_vertex_t *)malloc(count * sizeof(model_vertex_t));
        if (!tmp)
            continue;

        glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(count * sizeof(model_vertex_t)), tmp);

        for (size_t v = 0; v < count; ++v)
        {
            float x = tmp[v].px;
            float y = tmp[v].py;
            float z = tmp[v].pz;

            if (x < minx)
                minx = x;
            if (y < miny)
                miny = y;
            if (z < minz)
                minz = z;

            if (x > maxx)
                maxx = x;
            if (y > maxy)
                maxy = y;
            if (z > maxz)
                maxz = z;
        }

        free(tmp);
    }

    if (minx > maxx || miny > maxy || minz > maxz)
        return 0;

    *out_min = (vec3){minx, miny, minz};
    *out_max = (vec3){maxx, maxy, maxz};
    return 1;
}

typedef struct demo_layer_state_t
{
    ihandle_t model_h;
    mat4 model_m;

    ihandle_t override_mat_h;
    int did_patch_materials;

    int bounds_ready;
    vec3 aabb_min;
    vec3 aabb_max;
    vec3 local_center;
    float local_radius;

    camera_t cam;
    float fovy_rad;

    light_t lights_point[3];
    light_t sun_dir;

    int ready;
} demo_layer_state_t;

static light_t make_point(vec3 pos, vec3 color, float intensity)
{
    light_t l;
    memset(&l, 0, sizeof(l));
    l.type = LIGHT_POINT;
    l.position = pos;
    l.direction = (vec3){0.0f, -1.0f, 0.0f};
    l.color = color;
    l.intensity = intensity;
    l.radius = 10.0f;
    return l;
}

static light_t make_dir(vec3 dir, vec3 color, float intensity)
{
    light_t l;
    memset(&l, 0, sizeof(l));
    l.type = LIGHT_DIRECTIONAL;
    l.position = (vec3){0.0f, 0.0f, 0.0f};
    l.direction = vec3_norm(dir);
    l.color = color;
    l.intensity = intensity;
    l.radius = 0.0f;
    l.range = 0.0f;
    return l;
}

static void demo_layer_init(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)calloc(1, sizeof(demo_layer_state_t));
    layer->data = s;

    renderer_t *r = &layer->app->renderer;
    asset_manager_t *am = &layer->app->asset_manager;

    s->cam = camera_create();
    s->fovy_rad = 60.0f * 0.017453292519943295f;
    {
        float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;
        camera_set_perspective(&s->cam, s->fovy_rad, aspect, 0.1f, 500.0f);
    }

    s->model_h = asset_manager_request(am, ASSET_MODEL, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_FREE.obj");
    s->model_m = mat4_identity();

    asset_material_t mat = material_make_default(r->default_shader_id);

    mat.albedo_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_Base_Color.png");
    mat.normal_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_Normal.png");
    mat.roughness_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_Roughness.png");
    mat.metallic_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_Metallic.png");
    mat.occlusion_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_AO.png");
    mat.height_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_Height.png");

    mat.opacity = 1.0f;

    ihandle_t opacity_tex = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/44-textures_dirt_separated/Cottage_Dirt_Opacity.png");
    if (ihandle_is_valid(opacity_tex))
        mat.opacity = 0.999f;

    mat.roughness = 1.0f;
    mat.metallic = 0.0f;
    mat.normal_strength = 1.0f;
    mat.height_scale = 0.03f;
    mat.height_steps = 24;

    s->override_mat_h = asset_manager_submit_raw(am, ASSET_MATERIAL, &mat);

    s->did_patch_materials = 0;
    s->bounds_ready = 0;
    s->local_center = (vec3){0, 0, 0};
    s->local_radius = 1.0f;

    s->lights_point[0] = make_point((vec3){0.0f, 3.0f, 0.0f}, (vec3){1.0f, 0.1f, 0.1f}, 2.0f);
    s->lights_point[1] = make_point((vec3){3.0f, 3.0f, 0.0f}, (vec3){0.1f, 1.0f, 0.1f}, 2.0f);
    s->lights_point[2] = make_point((vec3){-3.0f, 3.0f, 0.0f}, (vec3){0.1f, 0.1f, 1.0f}, 2.0f);

    s->sun_dir = make_dir((vec3){0.15f, -1.0f, 1.0f}, (vec3){1.0f, 0.93f, 0.78f}, 1.25f);

    s->ready = 1;
}

static void demo_layer_shutdown(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s)
        return;

    free(s);
    layer->data = NULL;
}

static void demo_layer_update(layer_t *layer, float dt)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;
    const asset_manager_t *am = &layer->app->asset_manager;

    float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;

    static float t = 0.0f;
    t += dt;

    mat4 rot = mat4_rotate_y(t * 0.2f);
    mat4 scl = mat4_scale((vec3){1.0f, 1.0f, 1.0f});
    s->model_m = mat4_mul(rot, scl);

    if (!s->bounds_ready)
    {
        const asset_any_t *a = asset_manager_get_any(am, s->model_h);
        if (a && a->type == ASSET_MODEL && a->state == ASSET_STATE_READY)
        {
            asset_model_t *mdl = (asset_model_t *)&a->as.model;
            vec3 mn, mx;
            if (demo_compute_model_aabb_from_gpu(mdl, &mn, &mx))
            {
                s->aabb_min = mn;
                s->aabb_max = mx;

                s->local_center = (vec3){
                    (mn.x + mx.x) * 0.5f,
                    (mn.y + mx.y) * 0.5f,
                    (mn.z + mx.z) * 0.5f};

                vec3 ext = (vec3){
                    (mx.x - mn.x) * 0.5f,
                    (mx.y - mn.y) * 0.5f,
                    (mx.z - mn.z) * 0.5f};

                s->local_radius = sqrtf(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
                if (s->local_radius < 0.001f)
                    s->local_radius = 0.001f;

                s->bounds_ready = 1;
            }
        }
    }

    vec3 target_local = s->bounds_ready ? s->local_center : (vec3){0.0f, 0.75f, 0.0f};
    vec3 target_world = demo_mat4_transform_point(s->model_m, target_local);

    float fovy = s->fovy_rad;
    float fovx = 2.0f * atanf(tanf(fovy * 0.5f) * aspect);

    float scale = demo_mat4_max_scale_axis(s->model_m);
    float radius_world = (s->bounds_ready ? s->local_radius : 1.0f) * scale;

    float margin = 1.15f;
    float rv = radius_world * margin;

    float dv = rv / tanf(fovy * 0.5f);
    float dh = rv / tanf(fovx * 0.5f);
    float dist = demo_maxf(dv, dh);

    dist = demo_clampf(dist, 0.25f, 2000.0f);

    float orbit_speed = 0.45f;
    float ang = t * orbit_speed;

    float elev_deg = 15.0f;
    float elev = elev_deg * 0.017453292519943295f;

    float y_off = tanf(elev) * dist;

    vec3 pos = (vec3){
        target_world.x + dist * cosf(ang),
        target_world.y + y_off,
        target_world.z + dist * sinf(ang)};

    float nearp = dist - rv * 1.25f;
    nearp = demo_clampf(nearp, 0.02f, 500.0f);

    float farp = dist + rv * 2.0f;
    if (farp < nearp + 1.0f)
        farp = nearp + 1.0f;

    camera_set_perspective(&s->cam, fovy, aspect, nearp, farp);
    camera_look_at(&s->cam, pos, target_world, (vec3){0.0f, 1.0f, 0.0f});
}

static void demo_layer_draw(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;
    const asset_manager_t *am = &layer->app->asset_manager;

    if (!s->did_patch_materials)
    {
        const asset_any_t *a = asset_manager_get_any(am, s->model_h);
        if (a && a->type == ASSET_MODEL && a->state == ASSET_STATE_READY)
        {
            asset_model_t *mdl = (asset_model_t *)&a->as.model;
            for (uint32_t i = 0; i < mdl->meshes.size; ++i)
            {
                mesh_t *m = (mesh_t *)vector_impl_at(&mdl->meshes, i);
                m->material = s->override_mat_h;
            }
            s->did_patch_materials = 1;
        }
    }

    R_push_camera(r, &s->cam);

    R_push_light(r, s->sun_dir);

    R_push_light(r, s->lights_point[0]);
    R_push_light(r, s->lights_point[1]);
    R_push_light(r, s->lights_point[2]);

    R_push_model(r, s->model_h, s->model_m);
}

layer_t create_demo_layer(void)
{
    layer_t layer;
    layer.name = "Cottage OBJ";
    layer.init = demo_layer_init;
    layer.shutdown = demo_layer_shutdown;
    layer.update = demo_layer_update;
    layer.draw = demo_layer_draw;
    return layer;
}
