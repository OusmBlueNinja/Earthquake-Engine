#include "renderer/bloom.h"
#include "renderer/renderer.h"
#include "shader.h"
#include "utils/logger.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <string.h>
#include <stdlib.h>

static uint32_t bloom_tex_new(int w, int h)
{
    uint32_t t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

static uint32_t bloom_fbo_new(uint32_t color_tex)
{
    uint32_t f = 0;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);

    GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, bufs);

    uint32_t st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
        LOG_ERROR("Bloom FBO incomplete: 0x%x", (unsigned)st);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return f;
}

static void bloom_free_mips(bloom_t *b)
{
    for (uint32_t i = 0; i < 16; ++i)
    {
        if (b->fbo_down[i])
            glDeleteFramebuffers(1, &b->fbo_down[i]);
        if (b->fbo_up[i])
            glDeleteFramebuffers(1, &b->fbo_up[i]);
        if (b->tex_down[i])
            glDeleteTextures(1, &b->tex_down[i]);
        if (b->tex_up[i])
            glDeleteTextures(1, &b->tex_up[i]);
        b->fbo_down[i] = 0;
        b->fbo_up[i] = 0;
        b->tex_down[i] = 0;
        b->tex_up[i] = 0;
    }
    b->mips = 0;
    b->last_w = 0;
    b->last_h = 0;
}

static void bloom_alloc_mips(renderer_t *r, int w, int h, uint32_t mips)
{
    bloom_t *b = &r->bloom;

    bloom_free_mips(b);

    if (mips > 16)
        mips = 16;
    if (mips < 1)
        mips = 1;

    int cw = w;
    int ch = h;

    for (uint32_t i = 0; i < mips; ++i)
    {
        if (i > 0)
        {
            cw = (cw > 1) ? (cw / 2) : 1;
            ch = (ch > 1) ? (ch / 2) : 1;
        }

        b->tex_down[i] = bloom_tex_new(cw, ch);
        b->fbo_down[i] = bloom_fbo_new(b->tex_down[i]);

        b->tex_up[i] = bloom_tex_new(cw, ch);
        b->fbo_up[i] = bloom_fbo_new(b->tex_up[i]);
    }

    b->mips = mips;
    b->last_w = w;
    b->last_h = h;
    b->ready = 1;
}

int bloom_init(renderer_t *r)
{
    if (!r)
        return 0;

    bloom_t *b = &r->bloom;
    memset(b, 0, sizeof(*b));

    shader_t *pref = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/bloom_prefilter.frag");
    shader_t *down = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/bloom_downsample.frag");
    shader_t *bh = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/bloom_blur_h.frag");
    shader_t *bv = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/bloom_blur_v.frag");
    shader_t *up = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/bloom_upsample.frag");
    shader_t *comp = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/present.frag");

    if (!pref || !down || !bh || !bv || !up || !comp)
    {
        if (pref)
        {
            shader_destroy(pref);
            free(pref);
        }
        if (down)
        {
            shader_destroy(down);
            free(down);
        }
        if (bh)
        {
            shader_destroy(bh);
            free(bh);
        }
        if (bv)
        {
            shader_destroy(bv);
            free(bv);
        }
        if (up)
        {
            shader_destroy(up);
            free(up);
        }
        if (comp)
        {
            shader_destroy(comp);
            free(comp);
        }
        return 0;
    }

    b->prefilter_shader_id = R_add_shader(r, pref);
    b->downsample_shader_id = R_add_shader(r, down);
    b->blur_h_shader_id = R_add_shader(r, bh);
    b->blur_v_shader_id = R_add_shader(r, bv);
    b->upsample_shader_id = R_add_shader(r, up);
    b->composite_shader_id = R_add_shader(r, comp);

    b->ready = 0;
    b->last_w = 0;
    b->last_h = 0;
    b->mips = 0;

    return 1;
}

void bloom_shutdown(renderer_t *r)
{
    if (!r)
        return;

    bloom_t *b = &r->bloom;
    bloom_free_mips(b);
    memset(b, 0, sizeof(*b));
}

void bloom_ensure(renderer_t *r)
{
    if (!r)
        return;

    bloom_t *b = &r->bloom;

    uint32_t want_mips = r->cfg.bloom_mips;
    if (want_mips > 16)
        want_mips = 16;
    if (want_mips < 1)
        want_mips = 1;

    int w = r->fb_size.x;
    int h = r->fb_size.y;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    if (!b->ready || b->last_w != w || b->last_h != h || b->mips != want_mips)
        bloom_alloc_mips(r, w, h, want_mips);
}

static void bloom_prefilter(renderer_t *r, uint32_t src_tex)
{
    bloom_t *b = &r->bloom;
    shader_t *s = R_get_shader(r, b->prefilter_shader_id);
    if (!s)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, b->fbo_down[0]);

    int tw = 1;
    int th = 1;
    glBindTexture(GL_TEXTURE_2D, b->tex_down[0]);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);

    glViewport(0, 0, tw, th);

    shader_bind(s);

    shader_set_float(s, "u_Threshold", r->cfg.bloom_threshold);
    shader_set_float(s, "u_Knee", r->cfg.bloom_knee);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    shader_set_int(s, "u_Src", 0);

    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

static void bloom_downsample_chain(renderer_t *r)
{
    bloom_t *b = &r->bloom;
    shader_t *s = R_get_shader(r, b->downsample_shader_id);
    if (!s)
        return;

    shader_bind(s);

    for (uint32_t i = 1; i < b->mips; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, b->fbo_down[i]);

        int tw = 1;
        int th = 1;
        glBindTexture(GL_TEXTURE_2D, b->tex_down[i]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);

        glViewport(0, 0, tw, th);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->tex_down[i - 1]);
        shader_set_int(s, "u_Src", 0);

        shader_set_float(s, "u_TexelX", 1.0f / (float)tw);
        shader_set_float(s, "u_TexelY", 1.0f / (float)th);

        glBindVertexArray(r->fs_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }
}

static void bloom_blur_mips(renderer_t *r)
{
    bloom_t *b = &r->bloom;
    shader_t *sh = R_get_shader(r, b->blur_h_shader_id);
    shader_t *sv = R_get_shader(r, b->blur_v_shader_id);
    if (!sh || !sv)
        return;

    for (uint32_t i = 0; i < b->mips; ++i)
    {
        int tw = 1;
        int th = 1;
        glBindTexture(GL_TEXTURE_2D, b->tex_down[i]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);

        glBindFramebuffer(GL_FRAMEBUFFER, b->fbo_up[i]);
        glViewport(0, 0, tw, th);

        shader_bind(sh);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->tex_down[i]);
        shader_set_int(sh, "u_Src", 0);
        shader_set_float(sh, "u_TexelX", 1.0f / (float)tw);

        glBindVertexArray(r->fs_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glBindFramebuffer(GL_FRAMEBUFFER, b->fbo_down[i]);
        glViewport(0, 0, tw, th);

        shader_bind(sv);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->tex_up[i]);
        shader_set_int(sv, "u_Src", 0);
        shader_set_float(sv, "u_TexelY", 1.0f / (float)th);

        glBindVertexArray(r->fs_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }
}

static float bloom_level_weight(uint32_t i)
{
    if (i == 0)
        return 1.0f;
    if (i == 1)
        return 0.9f;
    if (i == 2)
        return 0.75f;
    if (i == 3)
        return 0.6f;
    if (i == 4)
        return 0.45f;
    return 0.35f;
}

static void bloom_upsample_chain(renderer_t *r)
{
    bloom_t *b = &r->bloom;
    shader_t *s = R_get_shader(r, b->upsample_shader_id);
    if (!s)
        return;

    shader_bind(s);

    uint32_t last = b->mips - 1;

    glBindFramebuffer(GL_FRAMEBUFFER, b->fbo_up[last]);

    int tw = 1;
    int th = 1;
    glBindTexture(GL_TEXTURE_2D, b->tex_up[last]);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);

    glViewport(0, 0, tw, th);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, b->tex_down[last]);
    shader_set_int(s, "u_Low", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    shader_set_int(s, "u_High", 1);

    shader_set_float(s, "u_TexelX", 1.0f / (float)tw);
    shader_set_float(s, "u_TexelY", 1.0f / (float)th);
    shader_set_float(s, "u_Intensity", bloom_level_weight(last));

    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    for (int i = (int)last - 1; i >= 0; --i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, b->fbo_up[i]);

        int w = 1;
        int h = 1;
        glBindTexture(GL_TEXTURE_2D, b->tex_up[i]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

        glViewport(0, 0, w, h);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b->tex_up[i + 1]);
        shader_set_int(s, "u_Low", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, b->tex_down[i]);
        shader_set_int(s, "u_High", 1);

        shader_set_float(s, "u_TexelX", 1.0f / (float)w);
        shader_set_float(s, "u_TexelY", 1.0f / (float)h);
        shader_set_float(s, "u_Intensity", bloom_level_weight((uint32_t)i));

        glBindVertexArray(r->fs_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }
}

void bloom_run(renderer_t *r, uint32_t src_tex, uint32_t black_tex)
{
    (void)black_tex;

    if (!r)
        return;

    if (!r->cfg.bloom)
        return;

    bloom_ensure(r);

    bloom_t *b = &r->bloom;
    if (!b->ready || b->mips == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    bloom_prefilter(r, src_tex);
    bloom_downsample_chain(r);
    bloom_blur_mips(r);
    bloom_upsample_chain(r);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

uint32_t bloom_get_texture(renderer_t *r)
{
    if (!r)
        return 0;
    bloom_t *b = &r->bloom;
    if (!b->ready || b->mips == 0)
        return 0;
    return b->tex_up[0];
}

void bloom_composite_to_final(renderer_t *r, uint32_t scene_tex, uint32_t bloom_tex, uint32_t depth_tex, uint32_t black_tex)
{
    if (!r)
        return;

    bloom_t *b = &r->bloom;
    shader_t *s = (b->composite_shader_id != 0xFF) ? R_get_shader(r, b->composite_shader_id) : 0;
    if (!s)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->final_fbo);
    glViewport(0, 0, r->fb_size.x, r->fb_size.y);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    shader_bind(s);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_tex ? scene_tex : black_tex);
    shader_set_int(s, "u_Scene", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_tex ? bloom_tex : black_tex);
    shader_set_int(s, "u_Bloom", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, depth_tex ? depth_tex : black_tex);
    shader_set_int(s, "u_Depth", 2);

    shader_set_int(s, "u_EnableBloom", (r->cfg.bloom && bloom_tex) ? 1 : 0);
    shader_set_float(s, "u_BloomIntensity", r->cfg.bloom_intensity);
    shader_set_int(s, "u_DebugMode", r->cfg.debug_mode);

    shader_set_float(s, "u_Exposure", r->cfg.exposure);
    shader_set_float(s, "u_OutputGamma", r->cfg.output_gamma);

    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void bloom_set_params(renderer_t *r, float threshold, float knee, float intensity, uint32_t mips)
{
    if (!r)
        return;

    r->cfg.bloom_threshold = threshold;
    r->cfg.bloom_knee = knee;
    r->cfg.bloom_intensity = intensity;

    if (mips < 1)
        mips = 1;

    if (r->cfg.bloom_mips != mips)
    {
        r->cfg.bloom_mips = mips;
        bloom_ensure(r);
    }
}
