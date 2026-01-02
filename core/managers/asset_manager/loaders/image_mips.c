#include "image_mips.h"

#include <stdlib.h>
#include <string.h>

static uint32_t image_mip_count(uint32_t w, uint32_t h)
{
    uint32_t n = 1;
    while (w > 1u || h > 1u)
    {
        if (w > 1u)
            w >>= 1u;
        if (h > 1u)
            h >>= 1u;
        n++;
        if (n >= ASSET_IMAGE_MAX_MIPS)
            break;
    }
    return n;
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void downsample_box_u8(uint8_t *dst, uint32_t dw, uint32_t dh, const uint8_t *src, uint32_t sw, uint32_t sh, uint32_t channels)
{
    for (uint32_t y = 0; y < dh; ++y)
    {
        uint32_t sy0 = clamp_u32(y * 2u + 0u, 0u, sh ? (sh - 1u) : 0u);
        uint32_t sy1 = clamp_u32(y * 2u + 1u, 0u, sh ? (sh - 1u) : 0u);
        for (uint32_t x = 0; x < dw; ++x)
        {
            uint32_t sx0 = clamp_u32(x * 2u + 0u, 0u, sw ? (sw - 1u) : 0u);
            uint32_t sx1 = clamp_u32(x * 2u + 1u, 0u, sw ? (sw - 1u) : 0u);

            const uint8_t *p00 = src + ((size_t)sy0 * (size_t)sw + (size_t)sx0) * (size_t)channels;
            const uint8_t *p10 = src + ((size_t)sy0 * (size_t)sw + (size_t)sx1) * (size_t)channels;
            const uint8_t *p01 = src + ((size_t)sy1 * (size_t)sw + (size_t)sx0) * (size_t)channels;
            const uint8_t *p11 = src + ((size_t)sy1 * (size_t)sw + (size_t)sx1) * (size_t)channels;

            uint8_t *d = dst + ((size_t)y * (size_t)dw + (size_t)x) * (size_t)channels;
            for (uint32_t c = 0; c < channels; ++c)
            {
                uint32_t sum = (uint32_t)p00[c] + (uint32_t)p10[c] + (uint32_t)p01[c] + (uint32_t)p11[c];
                d[c] = (uint8_t)((sum + 2u) / 4u);
            }
        }
    }
}

static void downsample_box_f32(float *dst, uint32_t dw, uint32_t dh, const float *src, uint32_t sw, uint32_t sh, uint32_t channels)
{
    for (uint32_t y = 0; y < dh; ++y)
    {
        uint32_t sy0 = clamp_u32(y * 2u + 0u, 0u, sh ? (sh - 1u) : 0u);
        uint32_t sy1 = clamp_u32(y * 2u + 1u, 0u, sh ? (sh - 1u) : 0u);
        for (uint32_t x = 0; x < dw; ++x)
        {
            uint32_t sx0 = clamp_u32(x * 2u + 0u, 0u, sw ? (sw - 1u) : 0u);
            uint32_t sx1 = clamp_u32(x * 2u + 1u, 0u, sw ? (sw - 1u) : 0u);

            const float *p00 = src + ((size_t)sy0 * (size_t)sw + (size_t)sx0) * (size_t)channels;
            const float *p10 = src + ((size_t)sy0 * (size_t)sw + (size_t)sx1) * (size_t)channels;
            const float *p01 = src + ((size_t)sy1 * (size_t)sw + (size_t)sx0) * (size_t)channels;
            const float *p11 = src + ((size_t)sy1 * (size_t)sw + (size_t)sx1) * (size_t)channels;

            float *d = dst + ((size_t)y * (size_t)dw + (size_t)x) * (size_t)channels;
            for (uint32_t c = 0; c < channels; ++c)
            {
                d[c] = 0.25f * (p00[c] + p10[c] + p01[c] + p11[c]);
            }
        }
    }
}

static bool mips_alloc(asset_image_mip_chain_t **out, uint32_t mip_count, uint32_t bytes_per_pixel)
{
    if (!out || mip_count == 0 || mip_count > ASSET_IMAGE_MAX_MIPS || bytes_per_pixel == 0)
        return false;

    asset_image_mip_chain_t *m = (asset_image_mip_chain_t *)calloc(1, sizeof(asset_image_mip_chain_t));
    if (!m)
        return false;

    m->mip_count = mip_count;
    m->bytes_per_pixel = bytes_per_pixel;
    *out = m;
    return true;
}

static bool mips_alloc_data(asset_image_mip_chain_t *m)
{
    if (!m || m->total_size == 0)
        return false;
    m->data = (uint8_t *)malloc((size_t)m->total_size);
    if (!m->data)
        return false;
    return true;
}

bool asset_image_mips_build_u8(asset_image_mip_chain_t **out, const uint8_t *base, uint32_t w, uint32_t h, uint32_t channels)
{
    if (out)
        *out = NULL;
    if (!out || !base || w == 0 || h == 0)
        return false;
    if (channels != 1 && channels != 3 && channels != 4)
        return false;

    const uint32_t mip_count = image_mip_count(w, h);
    const uint32_t bpp = channels;

    asset_image_mip_chain_t *m = NULL;
    if (!mips_alloc(&m, mip_count, bpp))
        return false;

    uint64_t off = 0;
    uint32_t mw = w;
    uint32_t mh = h;
    for (uint32_t i = 0; i < mip_count; ++i)
    {
        uint64_t sz = (uint64_t)mw * (uint64_t)mh * (uint64_t)bpp;
        m->width[i] = mw;
        m->height[i] = mh;
        m->offset[i] = off;
        m->size[i] = sz;
        off += sz;
        if (mw > 1u)
            mw >>= 1u;
        if (mh > 1u)
            mh >>= 1u;
    }
    m->total_size = off;

    if (!mips_alloc_data(m))
    {
        asset_image_mips_free(m);
        return false;
    }

    memcpy(m->data + (size_t)m->offset[0], base, (size_t)m->size[0]);

    for (uint32_t i = 1; i < mip_count; ++i)
    {
        const uint8_t *src = m->data + (size_t)m->offset[i - 1u];
        uint8_t *dst = m->data + (size_t)m->offset[i];
        downsample_box_u8(dst, m->width[i], m->height[i], src, m->width[i - 1u], m->height[i - 1u], channels);
    }

    *out = m;
    return true;
}

bool asset_image_mips_build_f32(asset_image_mip_chain_t **out, const float *base, uint32_t w, uint32_t h, uint32_t channels)
{
    if (out)
        *out = NULL;
    if (!out || !base || w == 0 || h == 0)
        return false;
    if (channels != 1 && channels != 3 && channels != 4)
        return false;

    const uint32_t mip_count = image_mip_count(w, h);
    const uint32_t bpp = channels * 4u;

    asset_image_mip_chain_t *m = NULL;
    if (!mips_alloc(&m, mip_count, bpp))
        return false;

    uint64_t off = 0;
    uint32_t mw = w;
    uint32_t mh = h;
    for (uint32_t i = 0; i < mip_count; ++i)
    {
        uint64_t sz = (uint64_t)mw * (uint64_t)mh * (uint64_t)bpp;
        m->width[i] = mw;
        m->height[i] = mh;
        m->offset[i] = off;
        m->size[i] = sz;
        off += sz;
        if (mw > 1u)
            mw >>= 1u;
        if (mh > 1u)
            mh >>= 1u;
    }
    m->total_size = off;

    if (!mips_alloc_data(m))
    {
        asset_image_mips_free(m);
        return false;
    }

    memcpy(m->data + (size_t)m->offset[0], base, (size_t)m->size[0]);

    for (uint32_t i = 1; i < mip_count; ++i)
    {
        const float *src = (const float *)(const void *)(m->data + (size_t)m->offset[i - 1u]);
        float *dst = (float *)(void *)(m->data + (size_t)m->offset[i]);
        downsample_box_f32(dst, m->width[i], m->height[i], src, m->width[i - 1u], m->height[i - 1u], channels);
    }

    *out = m;
    return true;
}

void asset_image_mips_free(asset_image_mip_chain_t *mips)
{
    if (!mips)
        return;
    free(mips->data);
    mips->data = NULL;
    free(mips);
}

