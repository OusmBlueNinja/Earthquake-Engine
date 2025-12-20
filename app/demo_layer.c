#include "demo_layer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core.h"
#include "renderer/renderer.h"
#include "types/vec3.h"
#include "types/mat4.h"
#include "renderer/camera.h"
#include "renderer/model.h"
#include "renderer/material.h"
#include "renderer/light.h"
#include "asset_manager/asset_manager.h"

typedef struct demo_layer_state_t
{
    model_factory_t mf;

    material_t *mats[3];
    model_t models[3];
    mat4 model_m[3];

    camera_t cam;

    light_t lights_point[2];
    light_t light_spot;

    int ready;
} demo_layer_state_t;

static material_t *material_create_solid(uint8_t shader_id, vec3 albedo)
{
    material_t *mat = (material_t *)calloc(1, sizeof(material_t));
    if (!mat)
        return NULL;

    mat->shader_id = shader_id;
    mat->albedo = albedo;
    mat->emissive = (vec3){0.0f, 0.0f, 0.0f};
    mat->roughness = 0.6f;
    mat->metallic = 0.1f;
    mat->opacity = 1.0f;

    mat->albedo_tex = ihandle_invalid();
    mat->normal_tex = ihandle_invalid();
    mat->metallic_tex = ihandle_invalid();
    mat->roughness_tex = ihandle_invalid();
    mat->emissive_tex = ihandle_invalid();
    mat->occlusion_tex = ihandle_invalid();
    mat->height_tex = ihandle_invalid();
    mat->arm_tex = ihandle_invalid();

    return mat;
}

static void material_destroy(material_t *m)
{
    if (!m)
        return;
    free(m);
}

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

static light_t make_spot(vec3 pos, vec3 dir, vec3 color, float intensity)
{
    light_t l;
    memset(&l, 0, sizeof(l));
    l.type = LIGHT_SPOT;
    l.position = pos;
    l.direction = dir;
    l.color = color;
    l.intensity = intensity;
    l.radius = 25.0f;
    l.range = 100.0f;
    return l;
}

static void demo_layer_init(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)calloc(1, sizeof(demo_layer_state_t));
    layer->data = s;

    renderer_t *r = &layer->app->renderer;

    if (!model_factory_init(&s->mf))
    {
        s->ready = 0;
        return;
    }

    s->cam = camera_create();
    {
        float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;
        camera_set_perspective(&s->cam, 60.0f * 0.017453292519943295f, aspect, 0.1f, 100.0f);
    }

    s->mats[0] = material_create_solid(r->default_shader_id, (vec3){1.0f, 1.0f, 1.0f});
    s->mats[1] = material_create_solid(r->default_shader_id, (vec3){1.0f, 1.0f, 1.0f});
    s->mats[2] = material_create_solid(r->default_shader_id, (vec3){1.0f, 1.0f, 1.0f});

    if (!s->mats[0] || !s->mats[1] || !s->mats[2])
    {
        s->ready = 0;
        return;
    }

    s->models[0] = model_make_primitive(&s->mf, PRIM_CUBE, s->mats[0]);
    s->models[1] = model_make_primitive(&s->mf, PRIM_CUBE, s->mats[1]);
    s->models[2] = model_make_primitive(&s->mf, PRIM_CUBE, s->mats[2]);

    float spacing = 1.10f;
    float topY = 1.0f;

    mat4 t0 = mat4_translate((vec3){-spacing * 0.5f, 0.0f, 0.0f});
    mat4 t1 = mat4_translate((vec3){spacing * 0.5f, 0.0f, 0.0f});
    mat4 t2 = mat4_translate((vec3){0.0f, topY, 0.0f});

    float yaw0 = 0.25f;
    float yaw1 = -0.35f;
    float yaw2 = 0.55f;

    mat4 r0 = mat4_rotate_y(yaw0);
    mat4 r1 = mat4_rotate_y(yaw1);
    mat4 r2 = mat4_rotate_y(yaw2);

    s->model_m[0] = mat4_mul(t0, r0);
    s->model_m[1] = mat4_mul(t1, r1);
    s->model_m[2] = mat4_mul(t2, r2);

    s->lights_point[0] = make_point((vec3){3.0f, 2.0f, 1.0f}, (vec3){1.0f, 1.0f, 1.0f}, 1.0f);
    s->lights_point[1] = make_point((vec3){3.0f, 0.5f, -1.5f}, (vec3){1.0f, 1.0f, 1.0f}, 1.0f);

    {
        vec3 spotPos = (vec3){0.0f, 0.0f, -5.0f};
        vec3 spotDir = (vec3){0.0f, 0.0f, 1.0f};
        s->light_spot = make_spot(spotPos, spotDir, (vec3){1.0f, 1.0f, 1.0f}, 1.0f);
    }

    s->mats[0]->albedo_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_diff_4k.png");
    s->mats[0]->normal_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_nor_gl_4k.png");
    s->mats[0]->roughness_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_rough_4k.png");
    s->mats[0]->metallic_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_arm_4k.png");
    s->mats[0]->occlusion_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_ao_4k.png");
    s->mats[0]->height_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_disp_4k.png");
    s->mats[0]->arm_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/blue_metal_plate_4k/textures/blue_metal_plate_arm_4k.png");

    s->mats[1]->albedo_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_diff_4k.png");
    s->mats[1]->normal_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_nor_gl_4k.png");
    s->mats[1]->roughness_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_rough_4k.png");
    s->mats[1]->metallic_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_arm_4k.png");
    s->mats[1]->occlusion_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_ao_4k.png");
    s->mats[1]->height_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_disp_4k.png");
    s->mats[1]->arm_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/wood_chip_path_4k/textures/wood_chip_path_arm_4k.png");

    s->mats[2]->albedo_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_diff_4k.png");
    s->mats[2]->normal_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_nor_gl_4k.png");
    s->mats[2]->roughness_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_rough_4k.png");
    s->mats[2]->metallic_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_arm_4k.png");
    s->mats[2]->occlusion_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_ao_4k.png");
    s->mats[2]->height_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_disp_4k.png");
    s->mats[2]->arm_tex = asset_manager_request(&layer->app->asset_manager, ASSET_IMAGE, "C:/Users/spenc/Pictures/textures/rusty_metal_grid_4k/textures/rusty_metal_grid_arm_4k.png");

    s->ready = 1;
}

static void demo_layer_shutdown(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s)
        return;

    for (int i = 0; i < 3; ++i)
        material_destroy(s->mats[i]);

    model_factory_shutdown(&s->mf);

    free(s);
    layer->data = NULL;
}

static void demo_layer_update(layer_t *layer, float dt)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;

    {
        float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;
        camera_set_perspective(&s->cam, 60.0f * 0.017453292519943295f, aspect, 0.1f, 100.0f);
    }

    static float t = 0.0f;
    t += dt;

    float radius = 5.0f;
    float speed = 0.6f;
    float a = t * speed;

    vec3 target = (vec3){0.0f, 0.5f, 0.0f};
    vec3 pos = (vec3){radius * cosf(a), 2.0f, radius * sinf(a)};

    camera_look_at(&s->cam, pos, target, (vec3){0.0f, 1.0f, 0.0f});
}

static void demo_layer_draw(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;

    R_push_camera(r, &s->cam);

    R_push_light(r, s->lights_point[0]);
    R_push_light(r, s->lights_point[1]);
    R_push_light(r, s->light_spot);

    R_push_model(r, &s->models[0], s->model_m[0]);
    R_push_model(r, &s->models[1], s->model_m[1]);
    R_push_model(r, &s->models[2], s->model_m[2]);
}

layer_t create_demo_layer(void)
{
    layer_t layer;
    memset(&layer, 0, sizeof(layer));
    layer.name = "CubeStack :)";
    layer.init = demo_layer_init;
    layer.shutdown = demo_layer_shutdown;
    layer.update = demo_layer_update;
    layer.draw = demo_layer_draw;
    return layer;
}
