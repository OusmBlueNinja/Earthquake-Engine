/* asset_manager/loaders/asset_image_itex.c */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "asset_manager/asset_manager.h"

#include "miniz.h"

#include <GL/glew.h>

#if defined(_WIN32)
#define ITEX_STRICMP _stricmp
#else
#include <strings.h>
#define ITEX_STRICMP strcasecmp
#endif

#define ITEX_MAGIC 0x58455449u
#define ITEX_VERSION 1u

#pragma pack(push, 1)
typedef struct itex_header_t
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;

    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t is_float;

    uint32_t has_alpha;
    uint32_t has_smooth_alpha;

    uint32_t uncompressed_size;
    uint32_t compressed_size;

    uint32_t handle_value;
    uint16_t handle_type;
    uint16_t handle_meta;

    uint32_t reserved0;
    uint32_t reserved1;
} itex_header_t;
#pragma pack(pop)

static int asset_path_has_ext_lower(const char *path, const char *ext_lower)
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

static void image_flip_y_bytes(void *pixels, uint32_t w, uint32_t h, uint32_t bytes_per_pixel)
{
    if (!pixels || w == 0 || h == 0 || bytes_per_pixel == 0)
        return;

    const uint32_t row_bytes = w * bytes_per_pixel;
    uint8_t *tmp = (uint8_t *)malloc((size_t)row_bytes);
    if (!tmp)
        return;

    uint8_t *p = (uint8_t *)pixels;
    for (uint32_t y = 0; y < h / 2u; ++y)
    {
        uint8_t *row0 = p + (size_t)y * (size_t)row_bytes;
        uint8_t *row1 = p + (size_t)(h - 1u - y) * (size_t)row_bytes;
        memcpy(tmp, row0, row_bytes);
        memcpy(row0, row1, row_bytes);
        memcpy(row1, tmp, row_bytes);
    }

    free(tmp);
}

static bool itex_can_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr)
{
    (void)am;
    if (!path)
        return false;
    if (!path_is_ptr && !path[0])
        return false;
    if (path_is_ptr)
        return false;
    return asset_path_has_ext_lower(path, ".itex");
}

static bool itex_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, ihandle_t *out_handle)
{
    (void)am;

    if (out_handle)
        *out_handle = ihandle_invalid();

    if (!out_asset || !out_handle || !path || path_is_ptr)
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LOG_ERROR("itex: open failed '%s'", path);
        return false;
    }

    itex_header_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h))
    {
        fclose(f);
        LOG_ERROR("itex: header read failed '%s'", path);
        return false;
    }

    if (h.magic != ITEX_MAGIC || h.version != ITEX_VERSION || h.header_size != (uint16_t)sizeof(itex_header_t))
    {
        fclose(f);
        LOG_ERROR("itex: bad header '%s'", path);
        return false;
    }

    if (h.width == 0 || h.height == 0 || h.channels == 0)
    {
        fclose(f);
        LOG_ERROR("itex: bad dims '%s'", path);
        return false;
    }

    if (h.compressed_size == 0 || h.uncompressed_size == 0)
    {
        fclose(f);
        LOG_ERROR("itex: bad sizes '%s'", path);
        return false;
    }

    uint8_t *comp = (uint8_t *)malloc((size_t)h.compressed_size);
    if (!comp)
    {
        fclose(f);
        LOG_ERROR("itex: oom compressed '%s'", path);
        return false;
    }

    if (fread(comp, 1, (size_t)h.compressed_size, f) != (size_t)h.compressed_size)
    {
        free(comp);
        fclose(f);
        LOG_ERROR("itex: data read failed '%s'", path);
        return false;
    }

    fclose(f);

    uint8_t *pixels = (uint8_t *)malloc((size_t)h.uncompressed_size);
    if (!pixels)
    {
        free(comp);
        LOG_ERROR("itex: oom pixels '%s'", path);
        return false;
    }

    mz_ulong dst_len = (mz_ulong)h.uncompressed_size;
    int z = mz_uncompress(pixels, &dst_len, comp, (mz_ulong)h.compressed_size);
    free(comp);

    if (z != MZ_OK || (uint32_t)dst_len != h.uncompressed_size)
    {
        free(pixels);
        LOG_ERROR("itex: decompress failed '%s'", path);
        return false;
    }

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_IMAGE;
    out_asset->state = ASSET_STATE_LOADING;

    out_asset->as.image.width = h.width;
    out_asset->as.image.height = h.height;
    out_asset->as.image.channels = h.channels;
    out_asset->as.image.pixels = pixels;
    out_asset->as.image.gl_handle = 0;
    out_asset->as.image.is_float = h.is_float;
    out_asset->as.image.has_alpha = h.has_alpha;
    out_asset->as.image.has_smooth_alpha = h.has_smooth_alpha;

    ihandle_t stored;
    stored.value = h.handle_value;
    stored.type = h.handle_type;
    stored.meta = h.handle_meta;
    *out_handle = stored;

    return true;
}

static bool itex_init(asset_manager_t *am, asset_any_t *asset)
{
    (void)am;

    if (!asset || asset->type != ASSET_IMAGE)
        return false;

    asset_image_t *img = &asset->as.image;
    if (img->gl_handle != 0)
        return true;

    if (!img->pixels || img->width == 0 || img->height == 0 || img->channels == 0)
        return false;

    // See `asset_image.c`: keep UVs consistent by flipping at upload time.
    {
        uint32_t bpp = img->channels * (img->is_float ? (uint32_t)sizeof(float) : 1u);
        image_flip_y_bytes(img->pixels, img->width, img->height, bpp);
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
        return false;

    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (img->is_float)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
        GLint internal = (img->channels == 4) ? GL_RGBA16F : (img->channels == 3 ? GL_RGB16F : GL_R16F);

        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)img->width, (GLsizei)img->height, 0, fmt, GL_FLOAT, (const void *)img->pixels);
        img->mip_count = 1;
        img->vram_bytes = (uint64_t)img->width * (uint64_t)img->height * (uint64_t)img->channels * 2ull;
    }
    else
    {
        int has_alpha = (img->has_alpha != 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, has_alpha ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, has_alpha ? GL_CLAMP_TO_EDGE : GL_REPEAT);

        GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
        GLint internal = (img->channels == 4) ? GL_RGBA8 : (img->channels == 3 ? GL_RGB8 : GL_R8);

        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)img->width, (GLsizei)img->height, 0, fmt, GL_UNSIGNED_BYTE, (const void *)img->pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        uint32_t mip_count = 1;
        {
            uint32_t mw = img->width;
            uint32_t mh = img->height;
            while (mw > 1u || mh > 1u)
            {
                if (mw > 1u)
                    mw >>= 1u;
                if (mh > 1u)
                    mh >>= 1u;
                mip_count++;
            }
        }
        img->mip_count = mip_count;

        uint64_t total = 0;
        uint32_t mw = img->width;
        uint32_t mh = img->height;
        const uint32_t bpp = img->channels;
        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            total += (uint64_t)mw * (uint64_t)mh * (uint64_t)bpp;
            if (mw > 1u)
                mw >>= 1u;
            if (mh > 1u)
                mh >>= 1u;
        }
        img->vram_bytes = total;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    img->gl_handle = (uint32_t)tex;
    free(img->pixels);
    img->pixels = NULL;
    return true;
}

static void itex_cleanup(asset_manager_t *am, asset_any_t *asset)
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

static uint32_t itex_bytes_per_pixel(uint32_t channels, uint32_t is_float)
{
    uint32_t b = channels;
    if (is_float)
        b *= 4u;
    return b;
}

static const char *itex_gl_err_str(GLenum e)
{
    switch (e)
    {
    case GL_NO_ERROR:
        return "GL_NO_ERROR";
    case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";
    default:
        return "GL_UNKNOWN_ERROR";
    }
}

static void itex_gl_clear_errors(void)
{
    GLenum e;
    int n = 0;
    while ((e = glGetError()) != GL_NO_ERROR && n < 32)
        ++n;
}

static bool itex_pull_pixels_from_gl(const asset_image_t *img, uint8_t **out_pixels, uint32_t *out_size)
{
    if (!img || !out_pixels || !out_size)
        return false;
    if (!img->gl_handle || img->width == 0 || img->height == 0 || img->channels == 0)
        return false;

    GLenum pre = glGetError();
    if (pre != GL_NO_ERROR)
    {
        LOG_WARN("preexisting GL error %s (0x%X)", itex_gl_err_str(pre), (unsigned)pre);
        itex_gl_clear_errors();
    }

    uint32_t bpp = itex_bytes_per_pixel(img->channels, img->is_float);
    uint32_t sz = img->width * img->height * bpp;

    uint8_t *p = (uint8_t *)malloc((size_t)sz);
    if (!p)
        return false;

    glBindTexture(GL_TEXTURE_2D, (GLuint)img->gl_handle);

    GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
    GLenum type = img->is_float ? GL_FLOAT : GL_UNSIGNED_BYTE;

    glGetTexImage(GL_TEXTURE_2D, 0, fmt, type, (void *)p);

    glBindTexture(GL_TEXTURE_2D, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        LOG_ERROR("glGetTexImage failed err=%s (0x%X) fmt=0x%X type=0x%X gl_handle=%u w=%u h=%u ch=%u is_float=%u",
                  itex_gl_err_str(err), (unsigned)err, (unsigned)fmt, (unsigned)type,
                  (unsigned)img->gl_handle, (unsigned)img->width, (unsigned)img->height,
                  (unsigned)img->channels, (unsigned)img->is_float);
        free(p);
        return false;
    }

    *out_pixels = p;
    *out_size = sz;
    return true;
}

static bool itex_save_blob(asset_manager_t *am, ihandle_t h, const asset_any_t *a, asset_blob_t *out)
{
    (void)am;

    char hb[64];
    handle_hex_triplet_filesafe(hb, h);

    if (!a)
    {
        LOG_ERROR(" asset was NULL (handle=%s)", hb);
        return false;
    }

    if (!out)
    {
        LOG_ERROR(" out blob was NULL (type=%s handle=%s)", ASSET_TYPE_TO_STRING(a->type), hb);
        return false;
    }

    if (a->type != ASSET_IMAGE)
    {
        LOG_ERROR(" wrong asset type=%s (expected ASSET_IMAGE) (handle=%s)",
                  ASSET_TYPE_TO_STRING(a->type), hb);
        return false;
    }

    const asset_image_t *img = &a->as.image;

    if (img->width == 0 || img->height == 0 || img->channels == 0)
    {
        LOG_ERROR(" invalid image dims w=%u h=%u ch=%u is_float=%u (handle=%s)",
                  (unsigned)img->width, (unsigned)img->height, (unsigned)img->channels, (unsigned)img->is_float, hb);
        return false;
    }

    if (img->channels != 1 && img->channels != 3 && img->channels != 4)
    {
        LOG_ERROR(" unsupported channels=%u (handle=%s)", (unsigned)img->channels, hb);
        return false;
    }

    uint8_t *src_pixels = NULL;
    uint32_t src_size = 0;

    if (img->pixels)
    {
        uint32_t bpp = itex_bytes_per_pixel(img->channels, img->is_float);
        if (bpp == 0)
        {
            LOG_ERROR(" bytes-per-pixel computed as 0 (ch=%u is_float=%u) (handle=%s)",
                      (unsigned)img->channels, (unsigned)img->is_float, hb);
            return false;
        }

        if (img->width > 0xFFFFu || img->height > 0xFFFFu)
            LOG_WARN(" very large texture w=%u h=%u (handle=%s)", (unsigned)img->width, (unsigned)img->height, hb);

        uint64_t sz64 = (uint64_t)img->width * (uint64_t)img->height * (uint64_t)bpp;
        if (sz64 == 0 || sz64 > 0xFFFFFFFFull)
        {
            LOG_ERROR(" src_size overflow/invalid (w=%u h=%u bpp=%u => %" PRIu64 ") (handle=%s)",
                      (unsigned)img->width, (unsigned)img->height, (unsigned)bpp, (uint64_t)sz64, hb);
            return false;
        }

        src_size = (uint32_t)sz64;

        src_pixels = (uint8_t *)malloc((size_t)src_size);
        if (!src_pixels)
        {
            LOG_ERROR(" OOM allocating src_pixels (%u bytes) (handle=%s)", (unsigned)src_size, hb);
            return false;
        }

        memcpy(src_pixels, img->pixels, (size_t)src_size);
    }
    else
    {
        if (!img->gl_handle)
        {
            LOG_ERROR(" no CPU pixels and gl_handle=0 (cannot read back) (handle=%s)", hb);
            return false;
        }

        if (!itex_pull_pixels_from_gl(img, &src_pixels, &src_size))
        {
            LOG_ERROR(" failed to pull pixels from GL (gl_handle=%u w=%u h=%u ch=%u is_float=%u) (handle=%s)",
                      (unsigned)img->gl_handle, (unsigned)img->width, (unsigned)img->height,
                      (unsigned)img->channels, (unsigned)img->is_float, hb);
            return false;
        }

        if (!src_pixels || src_size == 0)
        {
            LOG_ERROR(" GL pull returned empty data (data=%p size=%u) (handle=%s)",
                      (void *)src_pixels, (unsigned)src_size, hb);
            free(src_pixels);
            return false;
        }
    }

    mz_ulong comp_cap = mz_compressBound((mz_ulong)src_size);
    if (comp_cap == 0)
    {
        LOG_ERROR(" compressBound returned 0 (src_size=%u) (handle=%s)", (unsigned)src_size, hb);
        free(src_pixels);
        return false;
    }

    if (comp_cap > 0xFFFFFFFFul)
        LOG_WARN(" comp_cap exceeds 4GB (%lu) (handle=%s)", (unsigned long)comp_cap, hb);

    uint8_t *comp = (uint8_t *)malloc((size_t)comp_cap);
    if (!comp)
    {
        LOG_ERROR(" OOM allocating comp buffer (%lu bytes) (handle=%s)", (unsigned long)comp_cap, hb);
        free(src_pixels);
        return false;
    }

    mz_ulong comp_size = comp_cap;
    int z = mz_compress2(comp, &comp_size, (const unsigned char *)src_pixels, (mz_ulong)src_size, 1);
    free(src_pixels);

    if (z != MZ_OK)
    {
        LOG_ERROR(" mz_compress2 failed (z=%d src_size=%u) (handle=%s)", z, (unsigned)src_size, hb);
        free(comp);
        return false;
    }

    if (comp_size == 0)
    {
        LOG_ERROR(" compression produced size=0 (src_size=%u) (handle=%s)", (unsigned)src_size, hb);
        free(comp);
        return false;
    }

    uint64_t total64 = (uint64_t)sizeof(itex_header_t) + (uint64_t)comp_size;
    if (total64 == 0 || total64 > 0xFFFFFFFFull)
    {
        LOG_ERROR(" total size overflow/invalid (header=%u comp=%lu => %" PRIu64 ") (handle=%s)",
                  (unsigned)sizeof(itex_header_t), (unsigned long)comp_size, (uint64_t)total64, hb);
        free(comp);
        return false;
    }

    uint32_t total = (uint32_t)total64;

    uint8_t *buf = (uint8_t *)malloc((size_t)total);
    if (!buf)
    {
        LOG_ERROR(" OOM allocating output buffer (%u bytes) (handle=%s)", (unsigned)total, hb);
        free(comp);
        return false;
    }

    itex_header_t hd;
    memset(&hd, 0, sizeof(hd));
    hd.magic = ITEX_MAGIC;
    hd.version = (uint16_t)ITEX_VERSION;
    hd.header_size = (uint16_t)sizeof(itex_header_t);

    hd.width = img->width;
    hd.height = img->height;
    hd.channels = img->channels;
    hd.is_float = img->is_float;

    hd.has_alpha = img->has_alpha;
    hd.has_smooth_alpha = img->has_smooth_alpha;

    hd.uncompressed_size = src_size;
    hd.compressed_size = (uint32_t)comp_size;

    hd.handle_value = h.value;
    hd.handle_type = h.type;
    hd.handle_meta = h.meta;

    memcpy(buf, &hd, sizeof(hd));
    memcpy(buf + sizeof(hd), comp, (size_t)comp_size);
    free(comp);

    memset(out, 0, sizeof(*out));
    out->data = buf;
    out->size = total;
    out->align = 4;
    out->uncompressed_size = total;
    out->codec = 0;
    out->flags = 0;
    out->reserved = 0;

    return true;
}

static void itex_blob_free(asset_manager_t *am, asset_blob_t *blob)
{
    (void)am;
    if (!blob)
        return;
    if (blob->data)
        free(blob->data);
    memset(blob, 0, sizeof(*blob));
}

asset_module_desc_t asset_module_image_itex(void)
{
    asset_module_desc_t m;
    memset(&m, 0, sizeof(m));
    m.type = ASSET_IMAGE;
    m.name = "ASSET_IMAGE_ITEX";
    m.load_fn = itex_load;
    m.init_fn = itex_init;
    m.cleanup_fn = itex_cleanup;
    m.save_blob_fn = itex_save_blob;
    m.blob_free_fn = itex_blob_free;
    m.can_load_fn = itex_can_load;
    return m;
}
