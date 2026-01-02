#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_manager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "image_mips.h"

#include <GL/glew.h>

typedef struct asset_image_mem_desc_t
{
    void *bytes;
    size_t bytes_n;
    char *debug_name;
} asset_image_mem_desc_t;

static int asset_path_has_ext(const char *path, const char *ext_lower)
{
    if (!path || !ext_lower)
        return 0;
    size_t pl = strlen(path);
    size_t el = strlen(ext_lower);
    if (pl < el)
        return 0;
    const char *p = path + (pl - el);
    for (size_t i = 0; i < el; ++i)
    {
        char a = p[i];
        char b = ext_lower[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

static int rgba_has_any_alpha(const uint8_t *rgba, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i)
    {
        if (rgba[i * 4u + 3u] != 255u)
            return 1;
    }
    return 0;
}

static int rgba_has_smooth_alpha(const uint8_t *rgba, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i)
    {
        uint8_t a = rgba[i * 4u + 3u];
        if (a != 0u && a != 255u)
            return 1;
    }
    return 0;
}

static void rgba_dilate_rgb_into_zero_alpha(uint8_t *rgba, uint32_t w, uint32_t h, int passes)
{
    if (!rgba || w == 0 || h == 0 || passes <= 0)
        return;

    size_t n = (size_t)w * (size_t)h * 4u;
    uint8_t *tmp = (uint8_t *)malloc(n);
    if (!tmp)
        return;

    for (int p = 0; p < passes; ++p)
    {
        memcpy(tmp, rgba, n);

        for (uint32_t y = 0; y < h; ++y)
        {
            for (uint32_t x = 0; x < w; ++x)
            {
                size_t i = ((size_t)y * (size_t)w + (size_t)x) * 4u;
                uint8_t a0 = tmp[i + 3u];
                if (a0 != 0u)
                    continue;

                uint8_t best_a = 0u;
                uint8_t best_r = 0u, best_g = 0u, best_b = 0u;

                for (int oy = -1; oy <= 1; ++oy)
                {
                    int yy = (int)y + oy;
                    if (yy < 0 || yy >= (int)h)
                        continue;

                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        int xx = (int)x + ox;
                        if (xx < 0 || xx >= (int)w)
                            continue;
                        if (ox == 0 && oy == 0)
                            continue;

                        size_t j = ((size_t)yy * (size_t)w + (size_t)xx) * 4u;
                        uint8_t aj = tmp[j + 3u];
                        if (aj > best_a)
                        {
                            best_a = aj;
                            best_r = tmp[j + 0u];
                            best_g = tmp[j + 1u];
                            best_b = tmp[j + 2u];
                        }
                    }
                }

                if (best_a != 0u)
                {
                    rgba[i + 0u] = best_r;
                    rgba[i + 1u] = best_g;
                    rgba[i + 2u] = best_b;
                    rgba[i + 3u] = 0u;
                }
            }
        }
    }

    free(tmp);
}

static bool asset_image_load_from_memory(const asset_image_mem_desc_t *src, asset_any_t *out_asset)
{
    if (!src || !out_asset || !src->bytes || src->bytes_n == 0)
        return false;

    int w = 0;
    int h = 0;
    int c = 0;

    const uint8_t *mem = (const uint8_t *)src->bytes;
    int mem_n = (src->bytes_n > (size_t)INT32_MAX) ? INT32_MAX : (int)src->bytes_n;

    void *pixels = NULL;
    uint32_t channels = 0;
    uint32_t is_float = 0;
    uint32_t has_alpha = 0;
    uint32_t has_smooth_alpha = 0;
    asset_image_mip_chain_t *mips = NULL;

    const int is_hdr = stbi_is_hdr_from_memory(mem, mem_n);

    if (is_hdr)
    {
        float *data = stbi_loadf_from_memory(mem, mem_n, &w, &h, &c, 3);
        if (!data)
            return false;

        size_t count = (size_t)w * (size_t)h * 3u;
        size_t sz = count * sizeof(float);

        float *copy = (float *)malloc(sz);
        if (!copy)
        {
            stbi_image_free(data);
            return false;
        }

        memcpy(copy, data, sz);
        stbi_image_free(data);

        if (!asset_image_mips_build_f32(&mips, copy, (uint32_t)w, (uint32_t)h, 3u))
        {
            free(copy);
            return false;
        }

        free(copy);
        pixels = NULL;
        channels = 3;
        is_float = 1;
    }
    else
    {
        const int want_channels = 4;

        unsigned char *data = stbi_load_from_memory(mem, mem_n, &w, &h, &c, want_channels);
        if (!data)
            return false;

        size_t sz = (size_t)w * (size_t)h * (size_t)want_channels;

        uint8_t *copy = (uint8_t *)malloc(sz);
        if (!copy)
        {
            stbi_image_free(data);
            return false;
        }

        memcpy(copy, data, sz);
        stbi_image_free(data);

        if (want_channels == 4 && rgba_has_any_alpha(copy, (uint32_t)w, (uint32_t)h))
        {
            has_alpha = 1u;
            if (rgba_has_smooth_alpha(copy, (uint32_t)w, (uint32_t)h))
                has_smooth_alpha = 1u;
            rgba_dilate_rgb_into_zero_alpha(copy, (uint32_t)w, (uint32_t)h, 6);
        }

        if (!asset_image_mips_build_u8(&mips, copy, (uint32_t)w, (uint32_t)h, (uint32_t)want_channels))
        {
            free(copy);
            return false;
        }

        free(copy);
        pixels = NULL;
        channels = (uint32_t)want_channels;
        is_float = 0;
    }

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_IMAGE;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.image.width = (uint32_t)w;
    out_asset->as.image.height = (uint32_t)h;
    out_asset->as.image.channels = channels;
    out_asset->as.image.pixels = (uint8_t *)pixels;
    out_asset->as.image.gl_handle = 0;
    out_asset->as.image.is_float = is_float;
    out_asset->as.image.has_alpha = has_alpha;
    out_asset->as.image.has_smooth_alpha = has_smooth_alpha;
    out_asset->as.image.mips = mips;
    out_asset->as.image.mip_count = mips ? mips->mip_count : 0u;

    return true;
}

static bool asset_image_load_from_file(const char *path, asset_any_t *out_asset)
{
    if (!path || !path[0] || !out_asset)
        return false;

    int w = 0;
    int h = 0;
    int c = 0;

    const int is_hdr = asset_path_has_ext(path, ".hdr");
    const int want_channels = 4;

    void *pixels = NULL;
    uint32_t channels = 0;
    uint32_t is_float = 0;
    uint32_t has_alpha = 0;
    uint32_t has_smooth_alpha = 0;
    asset_image_mip_chain_t *mips = NULL;

    if (is_hdr)
    {
        float *data = stbi_loadf(path, &w, &h, &c, 3);
        if (!data)
            return false;

        size_t count = (size_t)w * (size_t)h * 3u;
        size_t sz = count * sizeof(float);

        float *copy = (float *)malloc(sz);
        if (!copy)
        {
            stbi_image_free(data);
            return false;
        }

        memcpy(copy, data, sz);
        stbi_image_free(data);

        if (!asset_image_mips_build_f32(&mips, copy, (uint32_t)w, (uint32_t)h, 3u))
        {
            free(copy);
            return false;
        }

        free(copy);
        pixels = NULL;
        channels = 3;
        is_float = 1;
    }
    else
    {
        unsigned char *data = stbi_load(path, &w, &h, &c, want_channels);
        if (!data)
            return false;

        size_t sz = (size_t)w * (size_t)h * (size_t)want_channels;

        uint8_t *copy = (uint8_t *)malloc(sz);
        if (!copy)
        {
            stbi_image_free(data);
            return false;
        }

        memcpy(copy, data, sz);
        stbi_image_free(data);

        if (want_channels == 4 && rgba_has_any_alpha(copy, (uint32_t)w, (uint32_t)h))
        {
            has_alpha = 1u;
            if (rgba_has_smooth_alpha(copy, (uint32_t)w, (uint32_t)h))
                has_smooth_alpha = 1u;
            rgba_dilate_rgb_into_zero_alpha(copy, (uint32_t)w, (uint32_t)h, 6);
        }

        if (!asset_image_mips_build_u8(&mips, copy, (uint32_t)w, (uint32_t)h, (uint32_t)want_channels))
        {
            free(copy);
            return false;
        }

        free(copy);
        pixels = NULL;
        channels = (uint32_t)want_channels;
        is_float = 0;
    }

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_IMAGE;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.image.width = (uint32_t)w;
    out_asset->as.image.height = (uint32_t)h;
    out_asset->as.image.channels = channels;
    out_asset->as.image.pixels = (uint8_t *)pixels;
    out_asset->as.image.gl_handle = 0;
    out_asset->as.image.is_float = is_float;
    out_asset->as.image.has_alpha = has_alpha;
    out_asset->as.image.has_smooth_alpha = has_smooth_alpha;
    out_asset->as.image.mips = mips;
    out_asset->as.image.mip_count = mips ? mips->mip_count : 0u;

    return true;
}

static bool asset_image_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, ihandle_t *out_handle)
{
    (void)am;
    if (out_handle)
        *out_handle = ihandle_invalid();

    if (!out_asset || !path)
        return false;

    if (path_is_ptr)
    {
        const asset_image_mem_desc_t *src = (const asset_image_mem_desc_t *)path;

        bool ok = asset_image_load_from_memory(src, out_asset);

        if (src)
        {
            if (src->bytes)
                free(src->bytes);
            if (src->debug_name)
                free(src->debug_name);
            free((void *)src);
        }

        return ok;
    }

    return asset_image_load_from_file(path, out_asset);
}

static bool asset_image_init(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_IMAGE)
        return false;

    asset_image_t *img = &asset->as.image;
    if (img->gl_handle != 0)
        return true;

    if (!img->mips || !img->mips->data || img->mips->mip_count == 0 || img->width == 0 || img->height == 0)
        return false;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
        return false;

    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const uint32_t mip_count = img->mips->mip_count;
    const uint32_t lowest_mip = (mip_count > 0) ? (mip_count - 1u) : 0u;

    if (img->is_float)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
        const GLint internal = (img->channels == 4) ? GL_RGBA16F : (img->channels == 3 ? GL_RGB16F : GL_R16F);

        glTexStorage2D(GL_TEXTURE_2D, (GLsizei)mip_count, (GLenum)internal, (GLsizei)img->width, (GLsizei)img->height);

        const uint32_t mw = img->mips->width[lowest_mip];
        const uint32_t mh = img->mips->height[lowest_mip];
        const void *src = (const void *)(img->mips->data + (size_t)img->mips->offset[lowest_mip]);
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)lowest_mip, 0, 0, (GLsizei)mw, (GLsizei)mh, fmt, GL_FLOAT, src);
    }
    else
    {
        const int has_alpha = (img->has_alpha != 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, has_alpha ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, has_alpha ? GL_CLAMP_TO_EDGE : GL_REPEAT);

        const GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
        const GLint internal = (img->channels == 4) ? GL_RGBA8 : (img->channels == 3 ? GL_RGB8 : GL_R8);

        glTexStorage2D(GL_TEXTURE_2D, (GLsizei)mip_count, (GLenum)internal, (GLsizei)img->width, (GLsizei)img->height);

        const uint32_t mw = img->mips->width[lowest_mip];
        const uint32_t mh = img->mips->height[lowest_mip];
        const void *src = (const void *)(img->mips->data + (size_t)img->mips->offset[lowest_mip]);
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)lowest_mip, 0, 0, (GLsizei)mw, (GLsizei)mh, fmt, GL_UNSIGNED_BYTE, src);
    }

    // Clamp sampling to resident mip range (starts at lowest mip only).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, (GLint)lowest_mip);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)lowest_mip);

    glBindTexture(GL_TEXTURE_2D, 0);

    img->gl_handle = (uint32_t)tex;

    img->stream_current_top_mip = lowest_mip;
    img->stream_target_top_mip = lowest_mip;
    img->stream_min_safety_mip = lowest_mip;
    img->stream_pending_target_top_mip = lowest_mip;
    img->stream_pending_frames = 0;
    img->stream_priority = 0;
    img->stream_residency_mask = (lowest_mip < 64u) ? (1ull << lowest_mip) : 0ull;
    img->stream_last_used_frame = 0;
    img->stream_last_used_ms = 0;
    img->stream_best_target_mip_frame = 0;
    img->stream_best_target_mip = lowest_mip;
    img->stream_last_upload_frame = 0;
    img->stream_last_evict_frame = 0;
    img->stream_forced = 0;
    img->stream_forced_top_mip = 0;
    img->stream_upload_inflight_mip = 0xFFFFFFFFu;
    img->stream_upload_row = 0u;

    img->vram_bytes = img->mips->size[lowest_mip];

    if (img->pixels)
    {
        free(img->pixels);
        img->pixels = NULL;
    }

    return true;
}

static void asset_image_cleanup(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_IMAGE)
        return;

    asset_image_t *img = &asset->as.image;

    if (img->pixels)
    {
        free(img->pixels);
        img->pixels = NULL;
    }

    if (img->mips)
    {
        asset_image_mips_free(img->mips);
        img->mips = NULL;
    }

    if (img->gl_handle)
    {
        GLuint tex = (GLuint)img->gl_handle;
        glDeleteTextures(1, &tex);
        img->gl_handle = 0;
    }

    img->width = 0;
    img->height = 0;
    img->channels = 0;
    img->is_float = 0;
    img->has_alpha = 0;
    img->has_smooth_alpha = 0;
    img->mip_count = 0;
    img->vram_bytes = 0;
}

static bool asset_image_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;

    if (!path)
        return false;

    if (path_is_ptr)
    {
        const asset_image_mem_desc_t *src = (const asset_image_mem_desc_t *)path;
        if (!src || !src->bytes || src->bytes_n == 0)
            return false;

        const uint8_t *mem = (const uint8_t *)src->bytes;
        int mem_n = (src->bytes_n > (size_t)INT32_MAX) ? INT32_MAX : (int)src->bytes_n;

        return stbi_info_from_memory(mem, mem_n, 0, 0, 0) != 0;
    }

    if (!path[0])
        return false;

    if (asset_path_has_ext(path, ".hdr"))
        return 1;

    if (asset_path_has_ext(path, ".png"))
        return 1;

    if (asset_path_has_ext(path, ".jpg"))
        return 1;

    if (asset_path_has_ext(path, ".jpeg"))
        return 1;

    if (asset_path_has_ext(path, ".bmp"))
        return 1;

    if (asset_path_has_ext(path, ".tga"))
        return 1;

    if (asset_path_has_ext(path, ".psd"))
        return 1;

    if (asset_path_has_ext(path, ".gif"))
        return 1;

    if (asset_path_has_ext(path, ".pic"))
        return 1;

    if (asset_path_has_ext(path, ".pgm"))
        return 1;

    if (asset_path_has_ext(path, ".ppm"))
        return 1;

    return 0;
}

asset_module_desc_t asset_module_image(void)
{
    asset_module_desc_t m;
    m.type = ASSET_IMAGE;
    m.name = "ASSET_IMAGE_STB";
    m.load_fn = asset_image_load;
    m.init_fn = asset_image_init;
    m.cleanup_fn = asset_image_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    m.can_load_fn = asset_image_can_load;
    return m;
}
