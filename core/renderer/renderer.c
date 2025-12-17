#include "renderer.h"
#include "utils/logger.h"
#include "shader.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static camera_t g_camera;
static light_t g_light;

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 r->fb_size.x, r->fb_size.y,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           r->color_tex,
                           0);

    glGenTextures(1, &r->depth_tex);
    glBindTexture(GL_TEXTURE_2D, r->depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                 r->fb_size.x, r->fb_size.y,
                 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D,
                           r->depth_tex,
                           0);

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

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    R_create_targets(r);

    LOG_DEBUG("Created framebuffer '%d'", r->fbo);
    return 0;
}

void R_shutdown(renderer_t *r)
{
    if (!r)
        return;

    glDeleteFramebuffers(1, &r->fbo);
    glDeleteTextures(1, &r->color_tex);
    glDeleteTextures(1, &r->depth_tex);

    LOG_INFO("Shutting down Renderer");
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

    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);
    glClearColor(r->clear_color.x,
                 r->clear_color.y,
                 r->clear_color.z,
                 r->clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void R_end_frame(renderer_t *r)
{
    (void)r;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void R_push_camera(const camera_t *cam)
{
    if (!cam)
        return;
    g_camera = *cam;
}

void R_push_light(const light_t *light)
{
    if (!light)
        return;
    g_light = *light;
}

void R_draw_model(const model_t *model, mat4 model_matrix)
{
    if (!model || !model->material || !model->material->shader)
        return;

    shader_t *s = model->material->shader;
    shader_bind(s);

    shader_set_mat4(s, "u_Model", model_matrix);
    shader_set_mat4(s, "u_View", g_camera.view);
    shader_set_mat4(s, "u_Proj", g_camera.proj);

    shader_set_vec3(s, "u_CameraPos", g_camera.position);

    shader_set_int(s, "u_LightType", g_light.type);
    shader_set_vec3(s, "u_LightPos", g_light.position);
    shader_set_vec3(s, "u_LightDir", g_light.direction);
    shader_set_vec3(s, "u_LightColor", g_light.color);
    shader_set_float(s, "u_LightIntensity", g_light.intensity);

    model_draw(model);
}

uint32_t R_get_color_texture(const renderer_t *r)
{
    return r ? r->color_tex : 0;
}

uint32_t R_get_depth_texture(const renderer_t *r)
{
    return r ? r->depth_tex : 0;
}
