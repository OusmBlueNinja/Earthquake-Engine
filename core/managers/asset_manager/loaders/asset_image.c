#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_manager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

static bool asset_image_load_from_memory(const asset_image_mem_desc_t *src, asset_any_t *out_asset)
{
    if (!src || !out_asset || !src->bytes || src->bytes_n == 0)
        return false;

    LOG_DEBUG("ASSET_IMAGE: load from memory (%s) bytes=%zu", (src->debug_name && src->debug_name[0]) ? src->debug_name : "(unnamed)", src->bytes_n);

    int w = 0;
    int h = 0;
    int c = 0;

    const uint8_t *mem = (const uint8_t *)src->bytes;
    int mem_n = (src->bytes_n > (size_t)INT32_MAX) ? INT32_MAX : (int)src->bytes_n;

    void *pixels = NULL;
    uint32_t channels = 0;
    uint32_t is_float = 0;

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

        pixels = copy;
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

        pixels = copy;
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

    return true;
}

static bool asset_image_load_from_file(const char *path, asset_any_t *out_asset)
{
    if (!path || !path[0] || !out_asset)
        return false;

    LOG_DEBUG("ASSET_IMAGE: load from file (%s)", path);

    int w = 0;
    int h = 0;
    int c = 0;

    const int is_hdr = asset_path_has_ext(path, ".hdr");
    const int want_channels = 4;

    void *pixels = NULL;
    uint32_t channels = 0;
    uint32_t is_float = 0;

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

        pixels = copy;
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

        pixels = copy;
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

    return true;
}

static bool asset_image_load(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset)
{
    (void)am;

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

    if (!img->pixels || img->width == 0 || img->height == 0)
        return false;

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
    }
    else
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        GLenum fmt = (img->channels == 4) ? GL_RGBA : (img->channels == 3 ? GL_RGB : GL_RED);
        GLint internal = (img->channels == 4) ? GL_RGBA8 : (img->channels == 3 ? GL_RGB8 : GL_R8);

        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)img->width, (GLsizei)img->height, 0, fmt, GL_UNSIGNED_BYTE, (const void *)img->pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    img->gl_handle = (uint32_t)tex;
    free(img->pixels);
    img->pixels = NULL;
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
}

asset_module_desc_t asset_module_image(void)
{
    asset_module_desc_t m;
    m.type = ASSET_IMAGE;
    m.name = "ASSET_IMAGE";
    m.load_fn = asset_image_load;
    m.init_fn = asset_image_init;
    m.cleanup_fn = asset_image_cleanup;
    m.save_blob_fn = NULL;
    m.blob_free_fn = NULL;
    return m;
}
