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

#define MOVING_LIGHTS 100

static float demo_clampf(float x, float lo, float hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static mat4 demo_mat4_rotate_x(float a)
{
    mat4 m = mat4_identity();
    float c = cosf(a);
    float s = sinf(a);
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    return m;
}

static mat4 demo_mat4_rotate_y(float a)
{
    mat4 m = mat4_identity();
    float c = cosf(a);
    float s = sinf(a);
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

static mat4 demo_mat4_translate(vec3 t)
{
    mat4 m = mat4_identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

static vec3 demo_mat4_mul_vec3_dir(mat4 m, vec3 v)
{
    vec3 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z;
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z;
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z;
    return r;
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

static float demo_frand01(void)
{
    return (float)rand() / (float)RAND_MAX;
}

static float demo_frand_range(float lo, float hi)
{
    return lo + (hi - lo) * demo_frand01();
}

static mat4 demo_transform_trs(vec3 t, float yaw, vec3 s)
{
    mat4 T = demo_mat4_translate(t);
    mat4 R = mat4_rotate_y(yaw);
    mat4 S = mat4_scale(s);
    return mat4_mul(T, mat4_mul(R, S));
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

    ihandle_t model_h;
    mat4 model_m;

    camera_t cam;
    float fovy_rad;

    int ready;

    int orbit_down;
    int pan_down;
    double last_mx;
    double last_my;
    int have_last_mouse;

    float yaw;
    float pitch;
    float dist;
    vec3 focus;

    int key_w;
    int key_a;
    int key_s;
    int key_d;

    float move_speed;
    float boost_mult;

    float t;
    moving_light_t lights[MOVING_LIGHTS];

    float stats_accum;
    uint64_t stats_frame_id;
} demo_layer_state_t;

static void demo_layer_apply_camera(demo_layer_state_t *s, renderer_t *r)
{
    float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;
    camera_set_perspective(&s->cam, s->fovy_rad, aspect, 0.1f, 200000.0f);

    s->pitch = demo_clampf(s->pitch, -1.55f, 1.55f);
    s->dist = demo_clampf(s->dist, 1.5f, 20000.0f);

    mat4 ry = demo_mat4_rotate_y(s->yaw);
    mat4 rx = demo_mat4_rotate_x(s->pitch);
    mat4 rxy = mat4_mul(ry, rx);

    vec3 offset = demo_mat4_mul_vec3_dir(rxy, (vec3){0.0f, 0.0f, s->dist});
    vec3 cam_pos = demo_vec3_add(s->focus, offset);

    camera_look_at(&s->cam, cam_pos, s->focus, (vec3){0.0f, 1.0f, 0.0f});
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
            s->orbit_down = down;
        if (b == 2)
            s->pan_down = down;

        s->have_last_mouse = 0;

        e->handled = (b == 1 || b == 2);
        return e->handled;
    }

    if (e->type == EV_MOUSE_SCROLL)
    {
        double dy = e->as.mouse_scroll.dy;
        float k = powf(0.90f, (float)dy);
        s->dist = demo_clampf(s->dist * k, 1.5f, 20000.0f);
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

        if (s->orbit_down)
        {
            float sens = 0.0050f;
            s->yaw -= (float)dx * sens;
            s->pitch += (float)-dy * sens;
            e->handled = true;
            return true;
        }

        if (s->pan_down)
        {
            float pan_scale = 0.0018f * s->dist;

            float cy = -cosf(s->yaw);
            float sy = -sinf(s->yaw);

            vec3 right = (vec3){-sy, 0.0f, cy};
            vec3 fwd = (vec3){cy, 0.0f, sy};

            s->focus = demo_vec3_add(s->focus, demo_vec3_scale(right, (float)(-dx) * pan_scale));
            s->focus = demo_vec3_add(s->focus, demo_vec3_scale(fwd, (float)(dy)*pan_scale));

            e->handled = true;
            return true;
        }

        return false;
    }

    if (e->type == EV_KEY_DOWN || e->type == EV_KEY_UP)
    {
        int down = (e->type == EV_KEY_DOWN) ? 1 : 0;
        int key = e->as.key.key;

        if (key == 'W')
            s->key_w = down;
        if (key == 'A')
            s->key_a = down;
        if (key == 'S')
            s->key_s = down;
        if (key == 'D')
            s->key_d = down;

        if (key == 340 || key == 344)
            s->boost_mult = down ? 4.0f : 1.0f;

        e->handled = (key == 'W' || key == 'A' || key == 'S' || key == 'D' || key == 340 || key == 344);
        return e->handled;
    }

    return false;
}

static void demo_init_moving_lights(demo_layer_state_t *s)
{
    for (int i = 0; i < MOVING_LIGHTS; ++i)
    {
        float a = (float)i / (float)MOVING_LIGHTS;

        s->lights[i].radius = demo_frand_range(10.0f, 55.0f);
        s->lights[i].speed = demo_frand_range(0.25f, 1.10f) * (demo_frand01() < 0.5f ? -1.0f : 1.0f);
        s->lights[i].phase = demo_frand_range(0.0f, 6.283185307179586f);
        s->lights[i].height = demo_frand_range(1.0f, 15.0f);
        s->lights[i].intensity = demo_frand_range(1.0f, 10.0f);
        s->lights[i].range = demo_frand_range(5.0f, 10.0f);

        float r = 0.5f + 0.5f * cosf(6.283185307179586f * (a + 0.00f));
        float g = 0.5f + 0.5f * cosf(6.283185307179586f * (a + 0.11f));
        float b = 0.5f + 0.5f * cosf(6.283185307179586f * (a + 0.22f));
        s->lights[i].color = (vec3){r, g, b};
    }
}

static void demo_layer_init(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)calloc(1, sizeof(demo_layer_state_t));
    layer->data = s;

    renderer_t *r = &layer->app->renderer;
    asset_manager_t *am = &layer->app->asset_manager;

    layer->on_event = demo_layer_on_event;

    s->cam = camera_create();
    s->fovy_rad = 60.0f * 0.017453292519943295f;

    s->yaw = 0.35f;
    s->pitch = -0.20f;
    s->dist = 200.0f;
    s->focus = (vec3){0.0f, 3.0f, 0.0f};

    s->move_speed = 10.0f;
    s->boost_mult = 1.0f;

    demo_layer_apply_camera(s, r);
    s->hdri_h = asset_manager_request(am, ASSET_IMAGE, "C:/Users/spenc/Desktop/Bistro_v5_2/Bistro_v5_2/san_giuseppe_bridge_4k.hdr");

    s->model_h = asset_manager_request(am, ASSET_MODEL, "C:/Users/spenc/Desktop/Bistro.glb");
    s->model_m = mat4_identity();

    s->stats_accum = 0.0f;
    s->stats_frame_id = 0;

    srand(1337u);

    demo_init_moving_lights(s);
    s->t = 0.0f;

    cvar_set_float_name("cl_r_ibl_intensity", 0.2f);

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

    s->t += dt;

    float move = s->move_speed * s->boost_mult * dt;

    float cy = cosf(s->yaw);
    float sy = sinf(s->yaw);

    vec3 fwd = demo_vec3_norm((vec3){cy, 0.0f, sy});
    vec3 right = demo_vec3_norm((vec3){-sy, 0.0f, cy});

    vec3 delta = (vec3){0, 0, 0};

    if (s->key_w)
        delta = demo_vec3_add(delta, fwd);
    if (s->key_s)
        delta = demo_vec3_sub(delta, fwd);
    if (s->key_a)
        delta = demo_vec3_add(delta, right);
    if (s->key_d)
        delta = demo_vec3_sub(delta, right);

    float dl = demo_vec3_len(delta);
    if (dl > 1e-6f)
        delta = demo_vec3_scale(delta, move / dl);

    s->focus = demo_vec3_add(s->focus, delta);

    demo_layer_apply_camera(s, r);

    s->stats_accum += dt;
    s->stats_frame_id++;

    if (s->stats_accum >= 0.5f)
    {
        const render_stats_t *st = R_get_stats(r);
        if (st)
        {
            LOG_INFO("[RenderStats] draws=%llu tris=%llu inst_draws=%llu inst=%llu inst_tris=%llu",
                     (unsigned long long)st->draw_calls,
                     (unsigned long long)st->triangles,
                     (unsigned long long)st->instanced_draw_calls,
                     (unsigned long long)st->instances,
                     (unsigned long long)st->instanced_triangles);
        }

        s->stats_accum = fmodf(s->stats_accum, 0.5f);
    }
}

static void demo_layer_draw(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s || !s->ready)
        return;

    renderer_t *r = &layer->app->renderer;

    R_push_camera(r, &s->cam);
    R_push_hdri(r, s->hdri_h);

    for (int i = 0; i < MOVING_LIGHTS; ++i)
    {
        moving_light_t *L = &s->lights[i];

        float a = L->phase + s->t * L->speed;
        float x = cosf(a) * L->radius;
        float z = sinf(a) * L->radius;

        light_t lt = {0};

        lt.type = LIGHT_POINT;
        lt.position = (vec3){x, L->height, z};
        lt.direction = (vec3){0.0f, -1.0f, 0.0f};
        lt.color = L->color;
        lt.intensity = L->intensity;
        lt.range = L->range;
        lt.radius = L->range;

        R_push_light(r, lt);
    }

    {
        mat4 H = demo_transform_trs((vec3){0.0f, 0.0f, 0.0f}, 0.0f, (vec3){1.0f, 1.0f, 1.0f});
        s->model_m = H;
        R_push_model(r, s->model_h, s->model_m);
    }
}

layer_t create_demo_layer(void)
{
    layer_t layer = create_layer("Bistro");
    layer.init = demo_layer_init;
    layer.shutdown = demo_layer_shutdown;
    layer.update = demo_layer_update;
    layer.draw = demo_layer_draw;
    layer.on_event = demo_layer_on_event;
    return layer;
}
