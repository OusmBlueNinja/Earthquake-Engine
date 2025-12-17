#include "renderer.h"
#include "shader.h"
#include "utils/logger.h"
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#define MAX_LIGHTS 16

static void R_create_targets(renderer_t *r)
{
    if (r->fbo)
    {
        glDeleteFramebuffers(1, &r->fbo);
        glDeleteTextures(1, &r->color_tex);
        glDeleteTextures(1, &r->depth_tex);
    }

    glGenFramebuffers(1, &r->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);

    glGenTextures(1, &r->color_tex);
    glBindTexture(GL_TEXTURE_2D, r->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, r->fb_size.x, r->fb_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->color_tex, 0);

    glGenTextures(1, &r->depth_tex);
    glBindTexture(GL_TEXTURE_2D, r->depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, r->fb_size.x, r->fb_size.y, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, r->depth_tex, 0);

    GLenum bufs[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, bufs);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int R_init(renderer_t *r)
{
    if (!r)
        return 1;

    r->clear_color = (vec4){0.05f, 0.05f, 0.06f, 1.0f};
    r->fb_size = (vec2i){1, 1};
    r->fbo = 0;
    r->color_tex = 0;
    r->depth_tex = 0;
    r->default_shader_id = 0;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    R_create_targets(r);

    r->lights = create_vector(light_t);
    r->models = create_vector(pushed_model_t);
    r->shaders = create_vector(shader_t *);

    shader_t *default_shader = shader_create_from_files(
        "res/shaders/shader.vert",
        "res/shaders/shader.frag");
    if (!default_shader)
    {
        LOG_ERROR("Failed to load default shader");
        return 1;
    }

    r->default_shader_id = R_add_shader(r, default_shader);

    shader_bind(default_shader);
    shader_set_int(default_shader, "u_MaxLights", MAX_LIGHTS);
    shader_unbind();

    LOG_DEBUG("Renderer initialized with framebuffer %u", r->fbo);
    return 0;
}

uint8_t R_add_shader(renderer_t *r, shader_t *shader)
{
    if (!r || !shader)
        return 0xFF;
    vector_push_back(&r->shaders, &shader);
    return (uint8_t)(r->shaders.size - 1);
}

shader_t *R_get_shader(const renderer_t *r, uint8_t shader_id)
{
    return *(shader_t **)(void *)vector_at((vector_t *)&r->shaders, shader_id);
}

void R_shutdown(renderer_t *r)
{
    if (!r)
        return;

    for (uint32_t i = 0; i < r->shaders.size; i++)
    {
        shader_t *s = *(shader_t **)vector_at(&r->shaders, i);
        if (s)
            shader_destroy(s);
    }

    vector_free(&r->shaders);
    vector_free(&r->lights);
    vector_free(&r->models);

    glDeleteFramebuffers(1, &r->fbo);
    glDeleteTextures(1, &r->color_tex);
    glDeleteTextures(1, &r->depth_tex);

    LOG_INFO("Renderer shutdown complete");
}

void R_resize(renderer_t *r, vec2i size)
{
    if (!r)
        return;
    if (size.x < 1)
        size.x = 1;
    if (size.y < 1)
        size.y = 1;
    if (r->fb_size.x == size.x && r->fb_size.y == size.y)
        return;

    r->fb_size = size;
    R_create_targets(r);
}

void R_set_clear_color(renderer_t *r, vec4 color)
{
    if (!r)
        return;
    r->clear_color = color;
}

void R_begin_frame(renderer_t *r)
{
    if (!r)
        return;

    vector_clear(&r->lights);
    vector_clear(&r->models);

    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);
    glClearColor(r->clear_color.x, r->clear_color.y, r->clear_color.z, r->clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void R_end_frame(renderer_t *r)
{
    if (!r)
        return;

    uint32_t model_count = r->models.size;
    uint32_t light_count = r->lights.size;
    if (light_count > MAX_LIGHTS)
        light_count = MAX_LIGHTS;

    for (uint32_t i = 0; i < model_count; i++)
    {
        pushed_model_t *pm = vector_at(&r->models, i);
        if (!pm || !pm->model)
            continue;

        material_t *mat = pm->model->material;
        uint8_t shader_id = mat ? mat->shader_id : r->default_shader_id;
        shader_t *s = R_get_shader(r, shader_id);
        if (!s)
            continue;

        shader_bind(s);

        shader_set_mat4(s, "u_Model", pm->model_matrix);
        shader_set_mat4(s, "u_View", r->camera.view);
        shader_set_mat4(s, "u_Proj", r->camera.proj);
        shader_set_vec3(s, "u_CameraPos", r->camera.position);

        shader_set_int(s, "u_LightCount", light_count);
        for (uint32_t l = 0; l < light_count; l++)
        {
            light_t *light = vector_at(&r->lights, l);

            char buf[64];
            snprintf(buf, 64, "u_Lights[%u].type", l);
            shader_set_int(s, buf, light->type);
            snprintf(buf, 64, "u_Lights[%u].position", l);
            shader_set_vec3(s, buf, light->position);
            snprintf(buf, 64, "u_Lights[%u].direction", l);
            shader_set_vec3(s, buf, light->direction);
            snprintf(buf, 64, "u_Lights[%u].color", l);
            shader_set_vec3(s, buf, light->color);
            snprintf(buf, 64, "u_Lights[%u].intensity", l);
            shader_set_float(s, buf, light->intensity);
        }

        if (mat)
        {
            shader_set_vec3(s, "u_Albedo", mat->albedo);
            shader_set_vec3(s, "u_Emissive", mat->emissive);
            shader_set_float(s, "u_Roughness", mat->roughness);
            shader_set_float(s, "u_Metallic", mat->metallic);
            shader_set_float(s, "u_Opacity", mat->opacity);
        }

        model_draw(pm->model);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void R_push_camera(renderer_t *r, const camera_t *cam)
{
    if (!r || !cam)
        return;
    r->camera = *cam;
}

void R_push_light(renderer_t *r, light_t light)
{
    if (!r)
        return;
    vector_push_back(&r->lights, &light);
}

void R_push_model(renderer_t *r, const model_t *model, mat4 model_matrix)
{
    if (!r || !model)
        return;
    pushed_model_t pm = {model, model_matrix};
    vector_push_back(&r->models, &pm);
}

uint32_t R_get_color_texture(const renderer_t *r)
{
    return r ? r->color_tex : 0;
}

uint32_t R_get_depth_texture(const renderer_t *r)
{
    return r ? r->depth_tex : 0;
}
