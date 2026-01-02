/* renderer/ibl.c */
#include "renderer/ibl.h"
#include "renderer/renderer.h"
#include "shader.h"
#include "types/mat4.h"
#include "types/vec3.h"
#include "utils/logger.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <string.h>
#include <math.h>

static mat4 ibl_capture_views[6];
static mat4 ibl_capture_proj;

static mat4 M_persp(float fovy, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    mat4 m;
    memset(&m, 0, sizeof(m));
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (zf + zn) / (zn - zf);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * zf * zn) / (zn - zf);
    return m;
}

static mat4 M_look(vec3 eye, vec3 at, vec3 up)
{
    vec3 f = vec3_norm(vec3_sub(at, eye));
    vec3 s = vec3_norm(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);

    mat4 m = mat4_identity();
    m.m[0] = s.x;
    m.m[4] = s.y;
    m.m[8] = s.z;
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    return m;
}

static void ibl_setup_mats(void)
{
    ibl_capture_proj = M_persp(3.14159265f * 0.5f, 1.0f, 0.1f, 10.0f);

    vec3 o = (vec3){0, 0, 0};

    ibl_capture_views[0] = M_look(o, (vec3){1, 0, 0}, (vec3){0, -1, 0});
    ibl_capture_views[1] = M_look(o, (vec3){-1, 0, 0}, (vec3){0, -1, 0});

    ibl_capture_views[2] = M_look(o, (vec3){0, 1, 0}, (vec3){0, 0, 1});
    ibl_capture_views[3] = M_look(o, (vec3){0, -1, 0}, (vec3){0, 0, -1});

    ibl_capture_views[4] = M_look(o, (vec3){0, 0, 1}, (vec3){0, -1, 0});
    ibl_capture_views[5] = M_look(o, (vec3){0, 0, -1}, (vec3){0, -1, 0});
}

static void ibl_make_cubemap(uint32_t *out, uint32_t size, int mips)
{
    glGenTextures(1, out);
    glBindTexture(GL_TEXTURE_CUBE_MAP, *out);

    for (int face = 0; face < 6; ++face)
        glTexImage2D((GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGB16F, (GLsizei)size, (GLsizei)size, 0, GL_RGB, GL_FLOAT, 0);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (mips)
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

static void ibl_make_2d(uint32_t *out, uint32_t size)
{
    glGenTextures(1, out);
    glBindTexture(GL_TEXTURE_2D, *out);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, (GLsizei)size, (GLsizei)size, 0, GL_RG, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void ibl_free_maps(renderer_t *r)
{
    if (r->ibl.env_cubemap)
        glDeleteTextures(1, &r->ibl.env_cubemap);
    if (r->ibl.irradiance_map)
        glDeleteTextures(1, &r->ibl.irradiance_map);
    if (r->ibl.prefilter_map)
        glDeleteTextures(1, &r->ibl.prefilter_map);
    if (r->ibl.brdf_lut)
        glDeleteTextures(1, &r->ibl.brdf_lut);

    r->ibl.env_cubemap = 0;
    r->ibl.irradiance_map = 0;
    r->ibl.prefilter_map = 0;
    r->ibl.brdf_lut = 0;
    r->ibl.ready = 0;
}

static void ibl_destroy(renderer_t *r)
{
    ibl_free_maps(r);

    if (r->ibl.capture_fbo)
        glDeleteFramebuffers(1, &r->ibl.capture_fbo);
    if (r->ibl.capture_rbo)
        glDeleteRenderbuffers(1, &r->ibl.capture_rbo);

    r->ibl.capture_fbo = 0;
    r->ibl.capture_rbo = 0;
    r->ibl.src_hdri = ihandle_invalid();
    r->ibl.src_hdri_top_mip = 0;
    r->ibl.ready = 0;
}

int ibl_init(renderer_t *r)
{
    if (!r)
        return 0;

    memset(&r->ibl, 0, sizeof(r->ibl));

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    r->ibl.env_size = 512;
    r->ibl.irradiance_size = 32;
    r->ibl.prefilter_size = 128;
    r->ibl.brdf_size = 256;

    r->ibl.src_hdri = ihandle_invalid();
    r->ibl.src_hdri_top_mip = 0;

    glGenFramebuffers(1, &r->ibl.capture_fbo);
    glGenRenderbuffers(1, &r->ibl.capture_rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, r->ibl.capture_fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, r->ibl.capture_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)r->ibl.env_size, (GLsizei)r->ibl.env_size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->ibl.capture_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    shader_t *s0 = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/ibl_equirect_to_cube.frag");
    shader_t *s1 = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/ibl_irradiance_convolve.frag");
    shader_t *s2 = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/ibl_prefilter_env.frag");
    shader_t *s3 = R_new_shader_from_files("res/shaders/fs_tri.vert", "res/shaders/ibl_brdf_lut.frag");

    if (!s0 || !s1 || !s2 || !s3)
        return 0;

    r->ibl.equirect_to_cube_shader_id = R_add_shader(r, s0);
    r->ibl.irradiance_shader_id = R_add_shader(r, s1);
    r->ibl.prefilter_shader_id = R_add_shader(r, s2);
    r->ibl.brdf_shader_id = R_add_shader(r, s3);

    ibl_setup_mats();

    return 1;
}

void ibl_shutdown(renderer_t *r)
{
    if (!r)
        return;
    ibl_destroy(r);
}

void ibl_on_resize(renderer_t *r)
{
    (void)r;
}

static uint32_t R_resolve_image_gl_local(const renderer_t *r, ihandle_t h)
{
    if (!r || !r->assets)
        return 0;
    if (!ihandle_is_valid(h))
        return 0;

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a)
        return 0;
    if (a->type != ASSET_IMAGE)
        return 0;
    if (a->state != ASSET_STATE_READY)
        return 0;

    return a->as.image.gl_handle;
}

static uint32_t R_resolve_image_stream_info_local(const renderer_t *r,
                                                  ihandle_t h,
                                                  uint32_t *out_top_mip,
                                                  uint32_t *out_w,
                                                  uint32_t *out_h,
                                                  uint32_t *out_mip_count)
{
    if (out_top_mip)
        *out_top_mip = 0;
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;
    if (out_mip_count)
        *out_mip_count = 0;

    if (!r || !r->assets)
        return 0;
    if (!ihandle_is_valid(h))
        return 0;

    const asset_any_t *a = asset_manager_get_any(r->assets, h);
    if (!a || a->type != ASSET_IMAGE || a->state != ASSET_STATE_READY)
        return 0;

    if (out_top_mip)
        *out_top_mip = a->as.image.stream_current_top_mip;
    if (out_w)
        *out_w = a->as.image.width;
    if (out_h)
        *out_h = a->as.image.height;
    if (out_mip_count)
        *out_mip_count = a->as.image.mip_count;

    return a->as.image.gl_handle;
}

static uint32_t ibl_desired_top_mip(uint32_t w, uint32_t h, uint32_t mip_count, float coverage_px)
{
    if (mip_count == 0 || w == 0 || h == 0)
        return 0;

    float px = coverage_px;
    if (!(px > 0.0f))
        px = 1.0f;

    const uint32_t max_dim = (w > h) ? w : h;
    float ratio = (float)max_dim / px;
    if (ratio <= 1.0f)
        return 0;

    float mipf = log2f(ratio);
    if (!(mipf > 0.0f))
        return 0;

    uint32_t mip = (uint32_t)(mipf + 1e-6f);
    if (mip >= mip_count)
        mip = mip_count - 1u;
    return mip;
}

static void ibl_fix_hdri_sampling(uint32_t hdri_tex_2d, uint32_t top_mip)
{
    if (!hdri_tex_2d)
        return;

    glBindTexture(GL_TEXTURE_2D, hdri_tex_2d);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Textures are streamable and may not have mip 0 resident yet.
    // Clamp to the highest-quality mip that is currently resident to avoid sampling undefined data.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, (GLint)top_mip);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)top_mip);

    glBindTexture(GL_TEXTURE_2D, 0);
}

static int ibl_check_capture_fbo(const char *tag)
{
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERROR("IBL capture FBO incomplete (%s): 0x%x", tag ? tag : "?", (unsigned)st);
        return 0;
    }
    return 1;
}

static void ibl_render_to_cubemap(renderer_t *r, uint32_t cubemap, uint32_t size, uint8_t shader_id, uint32_t input_tex, int input_is_cubemap)
{
    shader_t *s = R_get_shader(r, shader_id);
    if (!s)
        return;

    if (!r->ibl.capture_fbo || !r->ibl.capture_rbo)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->ibl.capture_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glViewport(0, 0, (GLsizei)size, (GLsizei)size);

    glBindRenderbuffer(GL_RENDERBUFFER, r->ibl.capture_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)size, (GLsizei)size);

    gl_state_disable(&r->gl, GL_CULL_FACE);
    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
    gl_state_depth_mask(&r->gl, 1);

    shader_bind(s);
    shader_set_mat4(s, "u_Proj", ibl_capture_proj);

    glActiveTexture(GL_TEXTURE0);
    if (!input_tex)
    {
        glBindTexture(input_is_cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 0);
    }
    else if (input_is_cubemap)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, input_tex);
        shader_set_int(s, "u_EnvMap", 0);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, input_tex);
        shader_set_int(s, "u_Equirect", 0);
    }

    for (int face = 0; face < 6; ++face)
    {
        shader_set_mat4(s, "u_View", ibl_capture_views[face]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), cubemap, 0);

        if (!ibl_check_capture_fbo("to_cubemap_face"))
            break;

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindVertexArray(r->fs_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void ibl_render_prefilter(renderer_t *r, uint32_t cubemap, uint32_t base_size, uint8_t shader_id, uint32_t env)
{
    shader_t *s = R_get_shader(r, shader_id);
    if (!s)
        return;

    if (!r->ibl.capture_fbo || !r->ibl.capture_rbo)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->ibl.capture_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    gl_state_disable(&r->gl, GL_CULL_FACE);
    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_enable(&r->gl, GL_DEPTH_TEST);
    gl_state_depth_func(&r->gl, GL_LEQUAL);
    gl_state_depth_mask(&r->gl, 1);

    shader_bind(s);
    shader_set_mat4(s, "u_Proj", ibl_capture_proj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, env);
    shader_set_int(s, "u_EnvMap", 0);

    int max_mips = 0;
    {
        uint32_t sz = base_size;
        while (sz > 1)
        {
            sz >>= 1;
            max_mips++;
        }
        if (max_mips < 1)
            max_mips = 1;
        if (max_mips > 7)
            max_mips = 7;
    }

    for (int mip = 0; mip < max_mips; ++mip)
    {
        uint32_t mip_size = base_size >> mip;
        if (mip_size < 1)
            mip_size = 1;

        glViewport(0, 0, (GLsizei)mip_size, (GLsizei)mip_size);

        glBindRenderbuffer(GL_RENDERBUFFER, r->ibl.capture_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)mip_size, (GLsizei)mip_size);

        float rough = (max_mips > 1) ? ((float)mip / (float)(max_mips - 1)) : 0.0f;
        shader_set_float(s, "u_Roughness", rough);

        for (int face = 0; face < 6; ++face)
        {
            shader_set_mat4(s, "u_View", ibl_capture_views[face]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), cubemap, mip);

            if (!ibl_check_capture_fbo("prefilter_face_mip"))
                break;

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glBindVertexArray(r->fs_vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void ibl_render_brdf(renderer_t *r, uint32_t lut, uint32_t size, uint8_t shader_id)
{
    shader_t *s = R_get_shader(r, shader_id);
    if (!s)
        return;

    if (!r->ibl.capture_fbo || !r->ibl.capture_rbo)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, r->ibl.capture_fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glViewport(0, 0, (GLsizei)size, (GLsizei)size);

    glBindRenderbuffer(GL_RENDERBUFFER, r->ibl.capture_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)size, (GLsizei)size);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lut, 0);

    if (!ibl_check_capture_fbo("brdf_lut"))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    gl_state_disable(&r->gl, GL_CULL_FACE);
    gl_state_disable(&r->gl, GL_BLEND);
    gl_state_disable(&r->gl, GL_DEPTH_TEST);

    shader_bind(s);

    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(r->fs_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ibl_ensure(renderer_t *r)
{
    if (!r)
        return;

    // Ensure the HDRI actually streams in (otherwise it can remain at 1x1 safety mip forever,
    // producing a flat/white env cubemap).
    if (r->assets && ihandle_is_valid(r->hdri_tex))
    {
        // Request enough detail to build a reasonable env cubemap.
        // Using ~2*faceSize as "screen coverage" yields a mip that still contains angular detail.
        float coverage_px = (float)(r->ibl.env_size ? (r->ibl.env_size * 2u) : 1024u);
        asset_manager_image_stream_record_use(r->assets, r->hdri_tex, coverage_px, 1.0f, 65000u);
    }

    uint32_t hdri_top_mip = 0;
    uint32_t hdri_w = 0, hdri_h = 0, hdri_mips = 0;
    uint32_t hdri_gl = R_resolve_image_stream_info_local(r, r->hdri_tex, &hdri_top_mip, &hdri_w, &hdri_h, &hdri_mips);
    if (!hdri_gl)
    {
        r->ibl.ready = 0;
        r->ibl.src_hdri = ihandle_invalid();
        r->ibl.src_hdri_top_mip = 0;
        return;
    }

    // Only rebuild when HDRI reaches a "good enough" mip for the cubemap size, and avoid rebuilding for every single mip step.
    float coverage_px = (float)(r->ibl.env_size ? (r->ibl.env_size * 2u) : 1024u);
    uint32_t desired_mip = ibl_desired_top_mip(hdri_w, hdri_h, hdri_mips, coverage_px);

    // If the same HDRI is still set and we haven't gained any higher-quality mip since last build, keep current maps.
    if (r->ibl.ready && ihandle_eq(r->ibl.src_hdri, r->hdri_tex) && hdri_top_mip >= r->ibl.src_hdri_top_mip)
        return;

    if (ihandle_eq(r->ibl.src_hdri, r->hdri_tex))
    {
        // Rebuild only if we improved enough, or we crossed into the desired mip threshold.
        const uint32_t old_mip = r->ibl.src_hdri_top_mip;
        const uint32_t new_mip = hdri_top_mip;
        const uint32_t improved_by = (old_mip > new_mip) ? (old_mip - new_mip) : 0u;
        const int crossed_desired = (old_mip > desired_mip) && (new_mip <= desired_mip);
        if (!crossed_desired && improved_by < 2u)
            return;
    }

    // If the resident mip is extremely tiny, wait (prevents "1x1 average color" skybox).
    if (!r->ibl.ready)
    {
        uint32_t rw = hdri_w >> hdri_top_mip;
        uint32_t rh = hdri_h >> hdri_top_mip;
        if (rw < 1u)
            rw = 1u;
        if (rh < 1u)
            rh = 1u;
        if (rw <= 2u && rh <= 2u)
            return;
    }

    ibl_fix_hdri_sampling(hdri_gl, hdri_top_mip);

    ibl_free_maps(r);

    ibl_make_cubemap(&r->ibl.env_cubemap, r->ibl.env_size, 1);
    ibl_make_cubemap(&r->ibl.irradiance_map, r->ibl.irradiance_size, 0);
    ibl_make_cubemap(&r->ibl.prefilter_map, r->ibl.prefilter_size, 1);
    ibl_make_2d(&r->ibl.brdf_lut, r->ibl.brdf_size);

    ibl_render_to_cubemap(r, r->ibl.env_cubemap, r->ibl.env_size, r->ibl.equirect_to_cube_shader_id, hdri_gl, 0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, r->ibl.env_cubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    ibl_render_to_cubemap(r, r->ibl.irradiance_map, r->ibl.irradiance_size, r->ibl.irradiance_shader_id, r->ibl.env_cubemap, 1);
    ibl_render_prefilter(r, r->ibl.prefilter_map, r->ibl.prefilter_size, r->ibl.prefilter_shader_id, r->ibl.env_cubemap);
    ibl_render_brdf(r, r->ibl.brdf_lut, r->ibl.brdf_size, r->ibl.brdf_shader_id);

    r->ibl.src_hdri = r->hdri_tex;
    r->ibl.src_hdri_top_mip = hdri_top_mip;
    r->ibl.ready = 1;
}

uint32_t ibl_get_env(const renderer_t *r) { return r ? r->ibl.env_cubemap : 0; }
uint32_t ibl_get_irradiance(const renderer_t *r) { return r ? r->ibl.irradiance_map : 0; }
uint32_t ibl_get_prefilter(const renderer_t *r) { return r ? r->ibl.prefilter_map : 0; }
uint32_t ibl_get_brdf_lut(const renderer_t *r) { return r ? r->ibl.brdf_lut : 0; }
int ibl_is_ready(const renderer_t *r) { return r ? r->ibl.ready : 0; }
