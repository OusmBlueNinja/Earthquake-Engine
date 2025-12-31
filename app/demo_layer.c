#include "demo_layer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core.h"
#include "cvar.h"
#include "renderer/renderer.h"
#include "types/vec3.h"
#include "types/mat4.h"
#include "renderer/camera.h"
#include "renderer/light.h"
#include "asset_manager/asset_manager.h"
#include "asset_manager/asset_types/model.h"
#include "asset_manager/asset_types/material.h"
#include "asset_manager/asset_types/image.h"
#include "systems/event.h"
#include "vector.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#define MOVING_LIGHTS 5

#define KEY_SPACE 32
#define KEY_LEFT_SHIFT 340
#define KEY_RIGHT_SHIFT 344
#define KEY_LEFT_CONTROL 341
#define KEY_RIGHT_CONTROL 345
#define KEY_LEFT 263
#define KEY_RIGHT 262
#define KEY_UP 265
#define KEY_DOWN 264

typedef struct demo_model_entry_t
{
    ihandle_t model;
    mat4 model_matrix;
} demo_model_entry_t;

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

static vec3 demo_vec3_add(vec3 a, vec3 b)
{
    return (vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static vec3 demo_vec3_sub(vec3 a, vec3 b)
{
    return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static vec3 demo_vec3_scale(vec3 a, float s)
{
    return (vec3){a.x * s, a.y * s, a.z * s};
}

static float demo_vec3_len(vec3 a)
{
    return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

static vec3 demo_vec3_norm(vec3 a)
{
    float l = demo_vec3_len(a);
    if (l <= 1e-8f)
        return (vec3){0, 0, 0};
    return demo_vec3_scale(a, 1.0f / l);
}

static vec3 demo_vec3_cross(vec3 a, vec3 b)
{
    return (vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

static mat4 demo_transform_trs(vec3 t, float yaw, vec3 s)
{
    mat4 T = demo_mat4_translate(t);
    mat4 R = mat4_rotate_y(yaw);
    mat4 S = mat4_scale(s);
    return mat4_mul(T, mat4_mul(R, S));
}

static vec3 demo_mat4_get_translation(mat4 m)
{
    return (vec3){m.m[12], m.m[13], m.m[14]};
}

typedef struct moving_light_t
{
    float radius;
    float speed;
    float phase;
    float height;
    float intensity;
    float range;
    vec3 color;
} moving_light_t;

typedef struct demo_layer_state_t
{
    ihandle_t hdri_h;

    vector_t models;

    camera_t cam;
    float fovy_rad;

    int ready;

    int look_down;
    double last_mx;
    double last_my;
    int have_last_mouse;

    float yaw;
    float pitch;
    vec3 pos;

    int key_w;
    int key_a;
    int key_s;
    int key_d;
    int key_space;
    int key_ctrl;

    int key_left;
    int key_right;
    int key_up;
    int key_down;

    float move_speed;
    float boost_mult;

    float t;
    moving_light_t lights[MOVING_LIGHTS];

    float stats_accum;
    uint64_t stats_frame_id;

    int focus_index;

    float last_dt;
} demo_layer_state_t;

static vec3 demo_cam_forward(float yaw, float pitch)
{
    float cy = cosf(yaw);
    float sy = sinf(yaw);
    float cp = cosf(pitch);
    float sp = sinf(pitch);
    return demo_vec3_norm((vec3){cp * cy, sp, cp * sy});
}

static void demo_layer_apply_camera(demo_layer_state_t *s, renderer_t *r)
{
    float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;

    float near_plane = 0.1f;
    float far_plane = 5000.0f;
    camera_set_perspective(&s->cam, s->fovy_rad, aspect, near_plane, far_plane);

    s->pitch = demo_clampf(s->pitch, -1.55f, 1.55f);

    vec3 fwd = demo_cam_forward(s->yaw, s->pitch);
    vec3 target = demo_vec3_add(s->pos, fwd);

    camera_look_at(&s->cam, s->pos, target, (vec3){0.0f, 1.0f, 0.0f});
}

static void demo_layer_focus_model(demo_layer_state_t *s, uint32_t index)
{
    if (!s)
        return;
    if (index >= s->models.size)
        return;

    demo_model_entry_t *e = (demo_model_entry_t *)vector_impl_at(&s->models, index);
    if (!e)
        return;

    vec3 target = demo_mat4_get_translation(e->model_matrix);

    float dist = 3.0f;

    vec3 fwd = demo_cam_forward(s->yaw, s->pitch);
    s->pos = demo_vec3_sub(target, demo_vec3_scale(fwd, dist));
}

static int demo_key_to_focus_index(int key)
{
    if (key >= '1' && key <= '9')
        return (key - '1');
    if (key == '0')
        return 9;
    return -1;
}

static bool demo_layer_on_event(layer_t *layer, event_t *e)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready || !e)
        return false;

    if (e->type == EV_MOUSE_BUTTON_DOWN || e->type == EV_MOUSE_BUTTON_UP)
    {
        int down = (e->type == EV_MOUSE_BUTTON_DOWN) ? 1 : 0;
        int b = e->as.mouse_button.button;

        if (b == 1)
            s->look_down = down;

        s->have_last_mouse = 0;

        e->handled = (b == 1);
        return e->handled;
    }

    if (e->type == EV_MOUSE_SCROLL)
    {
        double dy = e->as.mouse_scroll.dy;
        float k = (dy > 0.0) ? 1.10f : 0.90f;
        float steps = (float)fabs(dy);
        float mult = powf(k, steps);
        s->move_speed = demo_clampf(s->move_speed * mult, 0.25f, 5000.0f);
        e->handled = true;
        return true;
    }

    if (e->type == EV_MOUSE_MOVE)
    {
        double mx = e->as.mouse_move.x;
        double my = e->as.mouse_move.y;

        if (!s->have_last_mouse)
        {
            s->last_mx = mx;
            s->last_my = my;
            s->have_last_mouse = 1;
            return false;
        }

        double dx = mx - s->last_mx;
        double dy = my - s->last_my;
        s->last_mx = mx;
        s->last_my = my;

        if (s->look_down)
        {
            float sens = 0.0025f;
            s->yaw -= (float)dx * sens;
            s->pitch += (float)-dy * sens;
            e->handled = true;
            return true;
        }

        return false;
    }

    if (e->type == EV_KEY_DOWN || e->type == EV_KEY_UP)
    {
        int down = (e->type == EV_KEY_DOWN) ? 1 : 0;
        int key = e->as.key.key;

        if (down)
        {
            int fi = demo_key_to_focus_index(key);
            if (fi >= 0 && (uint32_t)fi < s->models.size)
            {
                s->focus_index = fi;
                demo_layer_focus_model(s, (uint32_t)fi);
                e->handled = 1;
                return true;
            }
        }

        if (key == 'W')
            s->key_w = down;
        if (key == 'A')
            s->key_a = down;
        if (key == 'S')
            s->key_s = down;
        if (key == 'D')
            s->key_d = down;

        if (key == KEY_SPACE)
            s->key_space = down;
        if (key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL)
            s->key_ctrl = down;

        if (key == KEY_RIGHT)
            s->key_left = down;
        if (key == KEY_LEFT)
            s->key_right = down;
        if (key == KEY_UP)
            s->key_up = down;
        if (key == KEY_DOWN)
            s->key_down = down;

        if (key == KEY_LEFT_SHIFT || key == KEY_RIGHT_SHIFT)
            s->boost_mult = down ? 4.0f : 1.0f;

        e->handled = 1;
        return true;
    }

    return false;
}

static void demo_layer_add_model(demo_layer_state_t *s, asset_manager_t *am, const char *path, mat4 mtx)
{
    if (!s || !am || !path || !path[0])
        return;

    demo_model_entry_t e;
    e.model = asset_manager_request(am, ASSET_MODEL, path);
    e.model_matrix = mtx;

    vector_impl_push_back(&s->models, &e);
}

static void demo_layer_init(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)calloc(1, sizeof(demo_layer_state_t));
    layer->data = s;

    renderer_t *r = &layer->app->renderer;
    asset_manager_t *am = &layer->app->asset_manager;

    layer->on_event = demo_layer_on_event;

    s->models = vector_impl_create_vector(sizeof(demo_model_entry_t));

    s->cam = camera_create();
    s->fovy_rad = 60.0f * 0.017453292519943295f;

    s->yaw = 0.0f;
    s->pitch = 0.0f;
    s->pos = (vec3){-0.0f, 3.0f, -10.0f};

    s->move_speed = 1.0f;
    s->boost_mult = 2.0f;

    demo_layer_apply_camera(s, r);
    s->hdri_h = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/Bistro_v5_2/Bistro_v5_2/san_giuseppe_bridge_4k.hdr");

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/38-floor/Floor.FBX",
                         demo_transform_trs((vec3){0.0f, -1.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/CoffeeCart_01_4k.gltf/CoffeeCart_01_4k.gltf",
                         demo_transform_trs((vec3){-1.5f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/Camera_01_4k.gltf/Camera_01_4k.gltf",
                         demo_transform_trs((vec3){0.0f, 0.0f, 0.0f}, 0.0f, (vec3){4.0f, 4.0f, 4.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/vintage_video_camera_4k.gltf/vintage_video_camera_4k.gltf",
                         demo_transform_trs((vec3){0.5f, 0.0f, 0.0f}, 0.0f, (vec3){4.0f, 4.0f, 4.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/CashRegister_01_4k.gltf/CashRegister_01_4k.gltf",
                         demo_transform_trs((vec3){1.5f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/electric_stove_4k.gltf/electric_stove_4k.gltf",
                         demo_transform_trs((vec3){2.5f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/Television_01_4k.gltf/Television_01_4k.gltf",
                         demo_transform_trs((vec3){3.5f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/ship_pinnace_4k.gltf/ship_pinnace_4k.gltf",
                         demo_transform_trs((vec3){10.0f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/utility_box_02_4k.gltf/utility_box_02_4k.gltf",
                         demo_transform_trs((vec3){-3.0f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    demo_layer_add_model(s, am, "C:/Users/spenc/Desktop/ornate_mirror_01_4k.gltf/ornate_mirror_01_4k.gltf",
                         demo_transform_trs((vec3){-0.0f, 0.0f, -5.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f}));

    s->focus_index = -1;

    s->stats_accum = 0.0f;
    s->stats_frame_id = 0;

    srand(1337u);

    s->t = 0.0f;

    s->ready = 1;
}

static void demo_layer_shutdown(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s)
        return;

    vector_impl_free(&s->models);

    free(s);
    layer->data = NULL;
}

static void demo_layer_update(layer_t *layer, float dt)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;

    s->last_dt = dt;

    s->t += dt;

    float rot_speed = 1.6f;
    if (s->key_left)
        s->yaw += rot_speed * dt;
    if (s->key_right)
        s->yaw -= rot_speed * dt;
    if (s->key_up)
        s->pitch += rot_speed * dt;
    if (s->key_down)
        s->pitch -= rot_speed * dt;

    s->pitch = demo_clampf(s->pitch, -1.55f, 1.55f);

    float move = s->move_speed * s->boost_mult * dt;

    vec3 world_up = (vec3){0.0f, 1.0f, 0.0f};
    vec3 fwd = demo_cam_forward(s->yaw, s->pitch);
    vec3 right = demo_vec3_norm(demo_vec3_cross(fwd, world_up));
    vec3 up = demo_vec3_norm(demo_vec3_cross(right, fwd));

    vec3 delta = (vec3){0, 0, 0};

    if (s->key_w)
        delta = demo_vec3_add(delta, fwd);
    if (s->key_s)
        delta = demo_vec3_sub(delta, fwd);
    if (s->key_d)
        delta = demo_vec3_add(delta, right);
    if (s->key_a)
        delta = demo_vec3_sub(delta, right);

    if (s->key_space)
        delta = demo_vec3_add(delta, world_up);
    if (s->key_ctrl)
        delta = demo_vec3_sub(delta, world_up);

    float dl = demo_vec3_len(delta);
    if (dl > 1e-6f)
        delta = demo_vec3_scale(delta, move / dl);

    s->pos = demo_vec3_add(s->pos, delta);

    demo_layer_apply_camera(s, r);

    s->stats_accum += dt;
    s->stats_frame_id++;

    // if (all_loaded(&layer->app->asset_manager))
    //{
    //     uint8_t *pack = 0;
    //     uint32_t pack_size = 0;
    //     asset_manager_build_pack_ex(&layer->app->asset_manager, &pack, &pack_size, SAVE_FLAG_SEPARATE_ASSETS, "./test/save");
    //     asset_manager_free_pack(pack);
    //     layer->app->running = false;
    // }
}

static void demo_layer_draw(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;

    renderer_scene_settings_t settings = R_scene_settings_default();
    settings.delta_time = s->last_dt;
    R_push_scene_settings(r, &settings);

    // R_push_camera(r, &s->cam);
    R_push_hdri(r, s->hdri_h);

    light_t lights[2 + MOVING_LIGHTS];
    uint32_t light_count = 0;

    {
        light_t sun = (light_t){0};
        sun.type = LIGHT_DIRECTIONAL;
        sun.direction = demo_vec3_norm((vec3){-0.3f, -1.0f, -0.2f});
        sun.color = (vec3){1.0f, 1.0f, 1.0f};
        sun.intensity = 1.0f;
        lights[light_count++] = sun;
    }

    {
        light_t p = (light_t){0};
        p.type = LIGHT_POINT;
        p.position = (vec3){0.0f, 4.0f, 0.0f};
        p.direction = (vec3){0.0f, -1.0f, 0.0f};
        p.color = (vec3){1.0f, 0.82f, 0.70f};
        p.intensity = 1.0f;
        p.range = 20.0f;
        p.radius = p.range;
        lights[light_count++] = p;
    }

    for (int i = 0; i < MOVING_LIGHTS; ++i)
    {
        moving_light_t *L = &s->lights[i];

        float a = L->phase + s->t * L->speed;
        float x = cosf(a) * L->radius;
        float z = sinf(a) * L->radius;

        light_t lt = (light_t){0};

        lt.type = LIGHT_POINT;
        lt.position = (vec3){x, L->height, z};
        lt.direction = (vec3){0.0f, -1.0f, 0.0f};
        lt.color = L->color;
        lt.intensity = L->intensity;
        lt.range = L->range;
        lt.radius = L->range;

        lights[light_count++] = lt;
    }

    for (uint32_t i = 0; i < light_count; ++i)
        R_push_light(r, lights[i]);

    for (uint32_t i = 0; i < s->models.size; ++i)
    {
        demo_model_entry_t *e = (demo_model_entry_t *)vector_impl_at(&s->models, i);
        if (!e)
            continue;
        R_push_model(r, e->model, e->model_matrix);
    }
}

layer_t create_demo_layer(void)
{
    layer_t layer = create_layer("Bistro Scene");
    layer.init = demo_layer_init;
    layer.shutdown = demo_layer_shutdown;
    layer.update = demo_layer_update;
    layer.draw = demo_layer_draw;
    layer.on_event = demo_layer_on_event;
    return layer;
}
