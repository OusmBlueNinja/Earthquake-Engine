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

void debug_draw_asset_model_aabbs_overlay(renderer_t *r, const asset_model_t *m, mat4 model_matrix)
{
    if (!r || !m)
        return;

    vec4 color = (vec4){0.15f, 1.0f, 0.25f, 0.85f};
    line3d_flags_t flags = (line3d_flags_t)(LINE3D_ON_TOP | LINE3D_TRANSLUCENT);

    uint32_t mesh_count = (uint32_t)m->meshes.size;
    for (uint32_t i = 0; i < mesh_count; ++i)
    {
        mesh_t *mesh = (mesh_t *)vector_impl_at((vector_t *)&m->meshes, i);
        if (!mesh)
            continue;
        if (!(mesh->flags & MESH_FLAG_HAS_AABB))
            continue;

        const aabb_t *b = &mesh->local_aabb;

        float x0 = b->min.x, y0 = b->min.y, z0 = b->min.z;
        float x1 = b->max.x, y1 = b->max.y, z1 = b->max.z;

        vec4 v0 = mat4_mul_vec4(model_matrix, (vec4){x0, y0, z0, 1.0f});
        vec4 v1 = mat4_mul_vec4(model_matrix, (vec4){x1, y0, z0, 1.0f});
        vec4 v2 = mat4_mul_vec4(model_matrix, (vec4){x1, y1, z0, 1.0f});
        vec4 v3 = mat4_mul_vec4(model_matrix, (vec4){x0, y1, z0, 1.0f});
        vec4 v4 = mat4_mul_vec4(model_matrix, (vec4){x0, y0, z1, 1.0f});
        vec4 v5 = mat4_mul_vec4(model_matrix, (vec4){x1, y0, z1, 1.0f});
        vec4 v6 = mat4_mul_vec4(model_matrix, (vec4){x1, y1, z1, 1.0f});
        vec4 v7 = mat4_mul_vec4(model_matrix, (vec4){x0, y1, z1, 1.0f});

        vec3 p0 = (vec3){v0.x, v0.y, v0.z};
        vec3 p1 = (vec3){v1.x, v1.y, v1.z};
        vec3 p2 = (vec3){v2.x, v2.y, v2.z};
        vec3 p3 = (vec3){v3.x, v3.y, v3.z};
        vec3 p4 = (vec3){v4.x, v4.y, v4.z};
        vec3 p5 = (vec3){v5.x, v5.y, v5.z};
        vec3 p6 = (vec3){v6.x, v6.y, v6.z};
        vec3 p7 = (vec3){v7.x, v7.y, v7.z};

        R_push_line3d(r, (line3d_t){p0, p1, color, flags});
        R_push_line3d(r, (line3d_t){p1, p2, color, flags});
        R_push_line3d(r, (line3d_t){p2, p3, color, flags});
        R_push_line3d(r, (line3d_t){p3, p0, color, flags});

        R_push_line3d(r, (line3d_t){p4, p5, color, flags});
        R_push_line3d(r, (line3d_t){p5, p6, color, flags});
        R_push_line3d(r, (line3d_t){p6, p7, color, flags});
        R_push_line3d(r, (line3d_t){p7, p4, color, flags});

        R_push_line3d(r, (line3d_t){p0, p4, color, flags});
        R_push_line3d(r, (line3d_t){p1, p5, color, flags});
        R_push_line3d(r, (line3d_t){p2, p6, color, flags});
        R_push_line3d(r, (line3d_t){p3, p7, color, flags});
    }
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
    
    

    return false;
}

static void demo_layer_add_model(demo_layer_state_t *s, asset_manager_t *am, const char *path, mat4 mtx)
{
    if (!s || !am || !path || !path[0])
        return;

    demo_model_entry_t e;
    e.model = asset_manager_request(am, ASSET_MODEL, path);
    e.model_matrix = mtx;
    char out[64];
    handle_hex_triplet_filesafe(out, e.model);
    LOG_INFO("Handle: %s", out);
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

    R_push_hdri(r, s->hdri_h);
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
