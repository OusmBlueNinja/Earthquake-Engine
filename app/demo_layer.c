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

static mat4 demo_mat4_translate(vec3 t)
{
    mat4 m = mat4_identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

typedef struct demo_layer_state_t
{
    ihandle_t model_h;
    mat4 model_m;

    ihandle_t override_mat_h;
    int did_patch_materials;

    ihandle_t cube_model_h;
    mat4 cube_model_m;
    ihandle_t cube_mat_h;
    int did_patch_cube;

    camera_t cam;
    float fovy_rad;

    light_t lights_point[3];
    light_t sun_dir;

    float t;
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

    s->cube_model_h = asset_manager_request(am, ASSET_MODEL, "res/models/cube.obj");
    s->cube_model_m = mat4_identity();

    asset_material_t cube_mat = material_make_default(r->default_shader_id);
    cube_mat.albedo = (vec3){0.2f, 0.85f, 1.0f};
    cube_mat.emissive = (vec3){0.0f, 0.0f, 0.0f};
    cube_mat.opacity = 0.35f;
    cube_mat.roughness = 0.08f;
    cube_mat.metallic = 0.0f;
    cube_mat.normal_strength = 1.0f;

    s->cube_mat_h = asset_manager_submit_raw(am, ASSET_MATERIAL, &cube_mat);

    s->did_patch_materials = 0;
    s->did_patch_cube = 0;

    s->lights_point[0] = make_point((vec3){0.0f, 3.0f, 0.0f}, (vec3){1.0f, 0.1f, 0.1f}, 2.0f);
    s->lights_point[1] = make_point((vec3){3.0f, 3.0f, 0.0f}, (vec3){0.1f, 1.0f, 0.1f}, 2.0f);
    s->lights_point[2] = make_point((vec3){-3.0f, 3.0f, 0.0f}, (vec3){0.1f, 0.1f, 1.0f}, 2.0f);

    s->sun_dir = make_dir((vec3){0.15f, -1.0f, 1.0f}, (vec3){1.0f, 0.93f, 0.78f}, 1.25f);

    s->t = 0.0f;
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

    float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;

    s->t += dt;

    vec3 target = (vec3){0.0f, 1.0f, 0.0f};

    float dist = 12.0f;
    float ang = s->t * 0.35f;
    float y = 5.5f;

    vec3 cam_pos = (vec3){
        target.x + cosf(ang) * dist,
        y,
        target.z + sinf(ang) * dist};

    camera_set_perspective(&s->cam, s->fovy_rad, aspect, 0.1f, 200.0f);
    camera_look_at(&s->cam, cam_pos, target, (vec3){0.0f, 1.0f, 0.0f});

    mat4 rot = mat4_rotate_y(s->t * 0.2f);
    s->model_m = rot;

    float bob = sinf(s->t * 1.5f) * 0.25f;

    vec3 cube_pos = (vec3){
        0.0f,
        6.0f + bob,
        0.0f};

    mat4 cube_t = demo_mat4_translate(cube_pos);
    mat4 cube_s = mat4_scale((vec3){1.25f, 1.25f, 1.25f});
    s->cube_model_m = mat4_mul(cube_t, cube_s);
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

    if (!s->did_patch_cube)
    {
        const asset_any_t *c = asset_manager_get_any(am, s->cube_model_h);
        if (c && c->type == ASSET_MODEL && c->state == ASSET_STATE_READY)
        {
            asset_model_t *mdl = (asset_model_t *)&c->as.model;
            for (uint32_t i = 0; i < mdl->meshes.size; ++i)
            {
                mesh_t *m = (mesh_t *)vector_impl_at(&mdl->meshes, i);
                m->material = s->cube_mat_h;
            }
            s->did_patch_cube = 1;
        }
    }

    R_push_camera(r, &s->cam);

    R_push_light(r, s->sun_dir);
    R_push_light(r, s->lights_point[0]);
    R_push_light(r, s->lights_point[1]);
    R_push_light(r, s->lights_point[2]);

    R_push_model(r, s->model_h, s->model_m);
    R_push_model(r, s->cube_model_h, s->cube_model_m);
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
