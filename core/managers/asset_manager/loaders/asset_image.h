#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "asset_manager/asset_manager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <GL/glew.h>

static bool asset_image_load(asset_manager_t *am, const char *path, asset_any_t *out_asset)
{
    (void)am;

    int w = 0;
    int h = 0;
    int c = 0;

    unsigned char *data = stbi_load(path, &w, &h, &c, 4);
    if (!data)
        return false;

    size_t sz = (size_t)w * (size_t)h * 4u;
    uint8_t *pixels = (uint8_t *)malloc(sz);
    if (!pixels)
    {
        stbi_image_free(data);
        return false;
    }

    memcpy(pixels, data, sz);
    stbi_image_free(data);

    memset(out_asset, 0, sizeof(*out_asset));
    out_asset->type = ASSET_IMAGE;
    out_asset->state = ASSET_STATE_LOADING;
    out_asset->as.image.width = (uint32_t)w;
    out_asset->as.image.height = (uint32_t)h;
    out_asset->as.image.channels = 4;
    out_asset->as.image.pixels = pixels;
    out_asset->as.image.gl_handle = 0;

    return true;
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

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)img->width, (GLsizei)img->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    img->gl_handle = (uint32_t)tex;
    free(img->pixels);
    img->pixels = NULL;
    LOG_DEBUG("Loaded texture %d", tex);
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
}

static asset_module_desc_t asset_module_image(void)
{
    asset_module_desc_t m;
    m.type = ASSET_IMAGE;
    m.name = "ASSET_IMAGE";
    m.load_fn = asset_image_load;
    m.init_fn = asset_image_init;
    m.cleanup_fn = asset_image_cleanup;
    return m;
}
