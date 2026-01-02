#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

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

static int hdr_parse_resolution_line(const char *line, int *out_w, int *out_h)
{
    if (!line || !out_w || !out_h)
        return 0;

    // Common variants:
    //   -Y 256 +X 512
    //   +Y 256 +X 512
    //   -Y 256 -X 512
    int y = 0;
    int x = 0;

    if (sscanf(line, " -Y %d +X %d", &y, &x) == 2)
        goto ok;
    if (sscanf(line, " +Y %d +X %d", &y, &x) == 2)
        goto ok;
    if (sscanf(line, " -Y %d -X %d", &y, &x) == 2)
        goto ok;
    if (sscanf(line, " +Y %d -X %d", &y, &x) == 2)
        goto ok;

    return 0;

ok:
    if (x <= 0 || y <= 0)
        return 0;
    *out_w = x;
    *out_h = y;
    return 1;
}

static int hdr_read_scanline_rgbe_rle(FILE *f, uint8_t *out_rgbe4, int width)
{
    if (!f || !out_rgbe4 || width <= 0)
        return 0;

    // RLE scanline marker requires width in [8, 32767].
    if (width < 8 || width > 32767)
        return 0;

    uint8_t hdr[4];
    if (fread(hdr, 1, 4, f) != 4)
        return 0;

    if (hdr[0] != 2 || hdr[1] != 2 || (hdr[2] & 0x80) != 0)
        return 0;

    int w = ((int)hdr[2] << 8) | (int)hdr[3];
    if (w != width)
        return 0;

    // Decode 4 separate channel runs into a temp planar buffer [4][width].
    uint8_t *planar = (uint8_t *)malloc((size_t)width * 4u);
    if (!planar)
        return 0;

    uint8_t *chan[4] = {
        planar + 0 * (size_t)width,
        planar + 1 * (size_t)width,
        planar + 2 * (size_t)width,
        planar + 3 * (size_t)width};

    for (int c = 0; c < 4; ++c)
    {
        int x = 0;
        while (x < width)
        {
            int count = fgetc(f);
            if (count == EOF)
            {
                free(planar);
                return 0;
            }

            if (count > 128)
            {
                int run = count - 128;
                int v = fgetc(f);
                if (v == EOF)
                {
                    free(planar);
                    return 0;
                }
                if (x + run > width)
                {
                    free(planar);
                    return 0;
                }
                memset(chan[c] + x, v, (size_t)run);
                x += run;
            }
            else
            {
                int run = count;
                if (run <= 0)
                {
                    free(planar);
                    return 0;
                }
                if (x + run > width)
                {
                    free(planar);
                    return 0;
                }
                if (fread(chan[c] + x, 1, (size_t)run, f) != (size_t)run)
                {
                    free(planar);
                    return 0;
                }
                x += run;
            }
        }
    }

    for (int x = 0; x < width; ++x)
    {
        out_rgbe4[(size_t)x * 4u + 0u] = chan[0][x];
        out_rgbe4[(size_t)x * 4u + 1u] = chan[1][x];
        out_rgbe4[(size_t)x * 4u + 2u] = chan[2][x];
        out_rgbe4[(size_t)x * 4u + 3u] = chan[3][x];
    }

    free(planar);
    return 1;
}

static int hdr_load_rgbe_downsampled(const char *path, int target_w, int target_h, float **out_rgb3, int *out_w, int *out_h)
{
    if (out_rgb3)
        *out_rgb3 = NULL;
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;

    if (!path || !path[0] || !out_rgb3 || !out_w || !out_h)
        return 0;
    if (target_w <= 0 || target_h <= 0)
        return 0;

    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;

    char line[512];
    int src_w = 0;
    int src_h = 0;

    // Header: read lines until blank line, then a resolution line.
    for (;;)
    {
        if (!fgets(line, (int)sizeof(line), f))
        {
            fclose(f);
            return 0;
        }
        if (line[0] == '\n' || line[0] == '\r' || line[0] == 0)
            break;
    }

    if (!fgets(line, (int)sizeof(line), f))
    {
        fclose(f);
        return 0;
    }

    if (!hdr_parse_resolution_line(line, &src_w, &src_h))
    {
        fclose(f);
        return 0;
    }

    if (src_w <= 0 || src_h <= 0)
    {
        fclose(f);
        return 0;
    }

    float *sum = (float *)calloc((size_t)target_w * (size_t)target_h * 3u, sizeof(float));
    uint32_t *cnt = (uint32_t *)calloc((size_t)target_w * (size_t)target_h, sizeof(uint32_t));
    uint8_t *scan = (uint8_t *)malloc((size_t)src_w * 4u);
    if (!sum || !cnt || !scan)
    {
        free(sum);
        free(cnt);
        free(scan);
        fclose(f);
        return 0;
    }


    for (int y = 0; y < src_h; ++y)
    {
        if (!hdr_read_scanline_rgbe_rle(f, scan, src_w))
        {
            free(sum);
            free(cnt);
            free(scan);
            fclose(f);
            return 0;
        }

        int ty = (int)(((int64_t)y * (int64_t)target_h) / (int64_t)src_h);
        if (ty < 0)
            ty = 0;
        if (ty >= target_h)
            ty = target_h - 1;

        for (int x = 0; x < src_w; ++x)
        {
            int tx = (int)(((int64_t)x * (int64_t)target_w) / (int64_t)src_w);
            if (tx < 0)
                tx = 0;
            if (tx >= target_w)
                tx = target_w - 1;

            const uint8_t *rgbe = scan + (size_t)x * 4u;
            const uint8_t r = rgbe[0];
            const uint8_t g = rgbe[1];
            const uint8_t b = rgbe[2];
            const uint8_t e = rgbe[3];

            float rf = 0.0f, gf = 0.0f, bf = 0.0f;
            if (e != 0)
            {
                // RGBE -> float RGB
                const float scale = ldexpf(1.0f, (int)e - (128 + 8));
                rf = (float)r * scale;
                gf = (float)g * scale;
                bf = (float)b * scale;
            }

            const size_t di = ((size_t)ty * (size_t)target_w + (size_t)tx);
            sum[di * 3u + 0u] += rf;
            sum[di * 3u + 1u] += gf;
            sum[di * 3u + 2u] += bf;
            cnt[di]++;
        }
    }

    free(scan);
    fclose(f);

    float *out = (float *)malloc((size_t)target_w * (size_t)target_h * 3u * sizeof(float));
    if (!out)
    {
        free(sum);
        free(cnt);
        return 0;
    }

    for (int y = 0; y < target_h; ++y)
    {
        for (int x = 0; x < target_w; ++x)
        {
            const size_t di = ((size_t)y * (size_t)target_w + (size_t)x);
            const uint32_t c = cnt[di] ? cnt[di] : 1u;
            out[di * 3u + 0u] = sum[di * 3u + 0u] / (float)c;
            out[di * 3u + 1u] = sum[di * 3u + 1u] / (float)c;
            out[di * 3u + 2u] = sum[di * 3u + 2u] / (float)c;
        }
    }

    free(sum);
    free(cnt);

    *out_rgb3 = out;
    *out_w = target_w;
    *out_h = target_h;
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
        // Huge HDRIs (e.g. 24k) are too big to fully decode to floats (and build mips) in RAM.
        // Fail with a clear message instead of letting stb fail with an opaque allocation error.
        int info_w = 0, info_h = 0, info_c = 0;
        const int info_ok = stbi_info(path, &info_w, &info_h, &info_c);

        const int max_w = 16384;
        const int max_h = 16384;

        int src_w = info_ok ? info_w : 0;
        int src_h = info_ok ? info_h : 0;

        int target_w = 0;
        int target_h = 0;

        if (src_w > 0 && src_h > 0)
        {
            float sx = (float)max_w / (float)src_w;
            float sy = (float)max_h / (float)src_h;
            float s = sx < sy ? sx : sy;
            if (s > 1.0f)
                s = 1.0f;
            target_w = (int)floorf((float)src_w * s + 0.5f);
            target_h = (int)floorf((float)src_h * s + 0.5f);
            if (target_w < 1)
                target_w = 1;
            if (target_h < 1)
                target_h = 1;
        }

        (void)target_w;
        (void)target_h;

        const uint64_t big_pixels = (uint64_t)src_w * (uint64_t)src_h;
        const uint64_t big_threshold = 60000000ull; // ~60MP ~= 960MB for float RGB (no mips), before overhead.
        const int too_big = (src_w > max_w || src_h > max_h || (src_w > 0 && src_h > 0 && big_pixels > big_threshold));

        if (too_big)
        {
            const uint64_t base_bytes = (src_w > 0 && src_h > 0) ? (big_pixels * 3ull * 4ull) : 0ull;
            const uint64_t mip_bytes_approx = (base_bytes * 4ull) / 3ull;
            LOG_ERROR("HDR image too big to load (src=%dx%d, ~%llu MB base, ~%llu MB+mips). Limits: %dx%d and <=%llu MP. (%s)",
                      src_w, src_h,
                      (unsigned long long)(base_bytes / (1024ull * 1024ull)),
                      (unsigned long long)(mip_bytes_approx / (1024ull * 1024ull)),
                      max_w, max_h,
                      (unsigned long long)(big_threshold / 1000000ull),
                      path);
            return false;
        }
        else
        {
            float *data = stbi_loadf(path, &w, &h, &c, 3);
            if (!data)
                return false;

            if (!asset_image_mips_build_f32(&mips, data, (uint32_t)w, (uint32_t)h, 3u))
            {
                stbi_image_free(data);
                return false;
            }

            stbi_image_free(data);
            pixels = NULL;
            channels = 3;
            is_float = 1;
        }
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

    int sparse_ok = 0;
#if !defined(__APPLE__)
    if (GLEW_ARB_sparse_texture != 0)
    {
        GLint num_levels = 0;
        GLenum internal_query = 0;
        if (img->is_float)
            internal_query = (img->channels == 4) ? GL_RGBA16F : (img->channels == 3 ? GL_RGB16F : GL_R16F);
        else
            internal_query = (img->channels == 4) ? GL_RGBA8 : (img->channels == 3 ? GL_RGB8 : GL_R8);

        glGetInternalformativ(GL_TEXTURE_2D, internal_query, GL_NUM_SPARSE_LEVELS_ARB, 1, &num_levels);
        if (glGetError() == GL_NO_ERROR && num_levels > 0)
            sparse_ok = 1;
    }
#endif

    if (img->is_float)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
        const GLint internal = (img->channels == 4) ? GL_RGBA16F : (img->channels == 3 ? GL_RGB16F : GL_R16F);

        if (sparse_ok)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SPARSE_ARB, GL_TRUE);
        glTexStorage2D(GL_TEXTURE_2D, (GLsizei)mip_count, (GLenum)internal, (GLsizei)img->width, (GLsizei)img->height);

        const uint32_t mw = img->mips->width[lowest_mip];
        const uint32_t mh = img->mips->height[lowest_mip];
        const void *src = (const void *)(img->mips->data + (size_t)img->mips->offset[lowest_mip]);
        if (sparse_ok)
            glTexPageCommitmentARB(GL_TEXTURE_2D, (GLint)lowest_mip, 0, 0, 0, (GLsizei)mw, (GLsizei)mh, 1, GL_TRUE);
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

        if (sparse_ok)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SPARSE_ARB, GL_TRUE);
        glTexStorage2D(GL_TEXTURE_2D, (GLsizei)mip_count, (GLenum)internal, (GLsizei)img->width, (GLsizei)img->height);

        const uint32_t mw = img->mips->width[lowest_mip];
        const uint32_t mh = img->mips->height[lowest_mip];
        const void *src = (const void *)(img->mips->data + (size_t)img->mips->offset[lowest_mip]);
        if (sparse_ok)
            glTexPageCommitmentARB(GL_TEXTURE_2D, (GLint)lowest_mip, 0, 0, 0, (GLsizei)mw, (GLsizei)mh, 1, GL_TRUE);
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)lowest_mip, 0, 0, (GLsizei)mw, (GLsizei)mh, fmt, GL_UNSIGNED_BYTE, src);
    }

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
    img->stream_best_priority_frame = 0;
    img->stream_best_priority = 0;
    img->stream_last_upload_frame = 0;
    img->stream_last_evict_frame = 0;
    img->stream_forced = 0;
    img->stream_forced_top_mip = 0;
    img->stream_sparse = sparse_ok ? 1u : 0u;
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
