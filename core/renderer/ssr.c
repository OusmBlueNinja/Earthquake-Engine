#include "renderer/ssr.h"
#include "renderer/renderer.h"
#include "shader.h"

#include <string.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static void ssr_destroy(renderer_t *r)
{
    if (r->ssr.fbo)
        glDeleteFramebuffers(1, &r->ssr.fbo);
    if (r->ssr.color_tex)
        glDeleteTextures(1, &r->ssr.color_tex);

    r->ssr.fbo = 0;
    r->ssr.color_tex = 0;
    r->ssr.ready = 0;
}

static void ssr_alloc(renderer_t *r)
{
    ssr_destroy(r);

    if (r->fb_size.x < 1 || r->fb_size.y < 1)
        return;

    glGenFramebuffers(1, &r->ssr.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssr.fbo);

    glGenTextures(1, &r->ssr.color_tex);
    glBindTexture(GL_TEXTURE_2D, r->ssr.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, r->fb_size.x, r->fb_size.y, 0, GL_RGBA, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->ssr.color_tex, 0);

    {
        GLenum bufs[] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, bufs);
    }

    r->ssr.ready = 1;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int ssr_init(renderer_t *r)
{
    if (!r)
        return 0;

    memset(&r->ssr, 0, sizeof(r->ssr));

    shader_t *ss = R_new_shader_from_files_with_defines("res/shaders/fs_tri.vert", "res/shaders/ssr.frag");
    if (!ss)
        return 0;

    r->ssr.shader_id = R_add_shader(r, ss);

    ssr_alloc(r);

    return 1;
}

void ssr_shutdown(renderer_t *r)
{
    if (!r)
        return;

    ssr_destroy(r);
}

void ssr_on_resize(renderer_t *r)
{
    if (!r)
        return;

    ssr_alloc(r);
}

void ssr_ensure(renderer_t *r)
{
    if (!r)
        return;
    if (r->fb_size.x < 1 || r->fb_size.y < 1)
        return;
    if (!r->ssr.fbo || !r->ssr.color_tex)
        ssr_alloc(r);
}

void ssr_run(renderer_t *r, uint32_t scene_tex)
{
    if (!r)
        return;
    if (!r->cfg.ssr)
        return;
    if (!r->ssr.ready || !r->ssr.fbo || !r->ssr.color_tex)
        return;

    shader_t *s = R_get_shader(r, r->ssr.shader_id);
    if (!s)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->ssr.fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    shader_bind(s);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_tex);
    shader_set_int(s, "u_Scene", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_depth);
    shader_set_int(s, "u_GDepth", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_normal);
    shader_set_int(s, "u_GNormal", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, r->gbuf_material);
    shader_set_int(s, "u_GMaterial", 3);

    shader_set_mat4(s, "u_View", r->camera.view);
    shader_set_mat4(s, "u_Proj", r->camera.proj);
    shader_set_mat4(s, "u_InvView", r->camera.inv_view);
    shader_set_mat4(s, "u_InvProj", r->camera.inv_proj);

    shader_set_vec2(s, "u_InvResolution", (vec2){ 1.0f / (float)r->fb_size.x, 1.0f / (float)r->fb_size.y });
    shader_set_float(s, "u_Intensity", r->cfg.ssr_intensity);
    shader_set_int(s, "u_Steps", r->cfg.ssr_steps);
    shader_set_float(s, "u_Stride", r->cfg.ssr_stride);
    shader_set_float(s, "u_Thickness", r->cfg.ssr_thickness);
    shader_set_float(s, "u_MaxDist", r->cfg.ssr_max_dist);

    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
