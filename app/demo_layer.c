#include "demo_layer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <GL/glew.h>

#include "core.h"
#include "renderer/renderer.h"
#include "types/vec3.h"
#include "types/mat4.h"
#include "renderer/camera.h"
#include "renderer/model.h"
#include "renderer/material.h"
#include "renderer/light.h"

typedef struct demo_layer_state_t
{
    mesh_t *mesh;

    material_t *mats[3];
    model_t models[3];
    mat4 model_m[3];

    camera_t cam;

    light_t lights_point[2];
    light_t light_spot;

    int ready;
} demo_layer_state_t;

static mesh_t *mesh_create_cube(void)
{
    static const float v[] = {
        -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,

        0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
        -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
        0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,

        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

        0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,

        -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f};

    static const uint32_t i[] = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20};

    mesh_t *m = (mesh_t *)calloc(1, sizeof(mesh_t));
    if (!m)
        return NULL;

    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);

    glGenBuffers(1, &m->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

    glGenBuffers(1, &m->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(i), i, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));

    glBindVertexArray(0);

    m->index_count = (uint32_t)(sizeof(i) / sizeof(i[0]));
    return m;
}

static void mesh_destroy(mesh_t *m)
{
    if (!m)
        return;
    if (m->ibo)
        glDeleteBuffers(1, &m->ibo);
    if (m->vbo)
        glDeleteBuffers(1, &m->vbo);
    if (m->vao)
        glDeleteVertexArrays(1, &m->vao);
    free(m);
}

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
    for (int k = 0; k < MATERIAL_TEXTURE_MAX; ++k)
        mat->textures[k] = 0;
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

    s->cam = camera_create();
    float aspect = (r->fb_size.y != 0) ? ((float)r->fb_size.x / (float)r->fb_size.y) : 1.0f;
    camera_set_perspective(&s->cam, 60.0f * 0.017453292519943295f, aspect, 0.1f, 100.0f);

    s->mesh = mesh_create_cube();
    if (!s->mesh)
    {
        s->ready = 0;
        return;
    }

    s->mats[0] = material_create_solid(r->default_shader_id, (vec3){1.0f, 0.2f, 0.2f});
    s->mats[1] = material_create_solid(r->default_shader_id, (vec3){0.2f, 1.0f, 0.2f});
    s->mats[2] = material_create_solid(r->default_shader_id, (vec3){0.2f, 0.4f, 1.0f});

    s->models[0].mesh = s->mesh;
    s->models[0].material = s->mats[0];
    s->models[1].mesh = s->mesh;
    s->models[1].material = s->mats[1];
    s->models[2].mesh = s->mesh;
    s->models[2].material = s->mats[2];

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

    s->lights_point[0] = make_point((vec3){3.0f, 2.0f, 1.0f}, (vec3){1.0f, 0.85f, 0.75f}, 1.0f);
    s->lights_point[1] = make_point((vec3){3.0f, 0.5f, -1.5f}, (vec3){0.75f, 0.85f, 1.0f}, 1.0f);

    vec3 spotPos = (vec3){0.0f, 0.0f, -5.0f};
    vec3 spotDir = (vec3){0.0f, 0.0f, 1.0f};
    s->light_spot = make_spot(spotPos, spotDir, (vec3){1.0f, 0.0f, 1.0f}, 1.0f);

    s->ready = 1;
}

static void demo_layer_shutdown(layer_t *layer)
{
    demo_layer_state_t *s = (demo_layer_state_t *)layer->data;
    if (!s)
        return;

    for (int i = 0; i < 3; ++i)
        material_destroy(s->mats[i]);
    mesh_destroy(s->mesh);

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
    camera_set_perspective(&s->cam, 60.0f * 0.017453292519943295f, aspect, 0.1f, 100.0f);

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
