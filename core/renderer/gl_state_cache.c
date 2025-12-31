#include "renderer/gl_state_cache.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

static uint8_t *gl_state_cap_ptr(gl_state_cache_t *st, uint32_t cap)
{
    if (!st)
        return NULL;
    switch ((GLenum)cap)
    {
    case GL_BLEND:
        return &st->cap_blend;
    case GL_DEPTH_TEST:
        return &st->cap_depth_test;
    case GL_CULL_FACE:
        return &st->cap_cull_face;
    case GL_SCISSOR_TEST:
        return &st->cap_scissor_test;
    case GL_MULTISAMPLE:
        return &st->cap_multisample;
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
        return &st->cap_sample_alpha_to_coverage;
    case GL_POLYGON_OFFSET_FILL:
        return &st->cap_polygon_offset_fill;
    case GL_TEXTURE_CUBE_MAP_SEAMLESS:
        return &st->cap_cube_map_seamless;
    default:
        return NULL;
    }
}

void gl_state_reset(gl_state_cache_t *st)
{
    if (!st)
        return;
    *st = (gl_state_cache_t){0};
}

void gl_state_enable(gl_state_cache_t *st, uint32_t cap)
{
    uint8_t *v = gl_state_cap_ptr(st, cap);
    if (!v)
    {
        glEnable((GLenum)cap);
        return;
    }
    if (*v)
        return;
    glEnable((GLenum)cap);
    *v = 1;
}

void gl_state_disable(gl_state_cache_t *st, uint32_t cap)
{
    uint8_t *v = gl_state_cap_ptr(st, cap);
    if (!v)
    {
        glDisable((GLenum)cap);
        return;
    }
    if (!*v)
        return;
    glDisable((GLenum)cap);
    *v = 0;
}

void gl_state_blend_func(gl_state_cache_t *st, uint32_t src_rgb, uint32_t dst_rgb)
{
    gl_state_blend_func_separate(st, src_rgb, dst_rgb, src_rgb, dst_rgb);
}

void gl_state_blend_func_separate(gl_state_cache_t *st, uint32_t src_rgb, uint32_t dst_rgb, uint32_t src_a, uint32_t dst_a)
{
    if (!st)
    {
        glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb, (GLenum)src_a, (GLenum)dst_a);
        return;
    }
    if (st->blend_func_known &&
        st->blend_src_rgb == src_rgb && st->blend_dst_rgb == dst_rgb &&
        st->blend_src_a == src_a && st->blend_dst_a == dst_a)
        return;
    glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb, (GLenum)src_a, (GLenum)dst_a);
    st->blend_func_known = 1;
    st->blend_src_rgb = src_rgb;
    st->blend_dst_rgb = dst_rgb;
    st->blend_src_a = src_a;
    st->blend_dst_a = dst_a;
}

void gl_state_depth_mask(gl_state_cache_t *st, uint8_t enabled)
{
    if (!st)
    {
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
        return;
    }
    if (st->depth_mask_known && st->depth_mask == (enabled ? 1u : 0u))
        return;
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    st->depth_mask_known = 1;
    st->depth_mask = enabled ? 1u : 0u;
}

void gl_state_depth_func(gl_state_cache_t *st, uint32_t func)
{
    if (!st)
    {
        glDepthFunc((GLenum)func);
        return;
    }
    if (st->depth_func_known && st->depth_func == func)
        return;
    glDepthFunc((GLenum)func);
    st->depth_func_known = 1;
    st->depth_func = func;
}

