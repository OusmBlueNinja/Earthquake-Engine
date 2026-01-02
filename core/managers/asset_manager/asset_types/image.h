#pragma once
#include <stdint.h>

// Maximum mip levels we track/debug. 20 covers up to 2^19 (524288) textures.
// (Typical textures are far smaller; this is just a safety cap.)
#define ASSET_IMAGE_MAX_MIPS 20u

typedef struct asset_image_mip_chain_t
{
    uint32_t mip_count;
    uint32_t bytes_per_pixel;

    uint32_t width[ASSET_IMAGE_MAX_MIPS];
    uint32_t height[ASSET_IMAGE_MAX_MIPS];

    uint64_t offset[ASSET_IMAGE_MAX_MIPS];
    uint64_t size[ASSET_IMAGE_MAX_MIPS];

    uint64_t total_size;
    uint8_t *data;
} asset_image_mip_chain_t;

typedef struct asset_image_t
{
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint8_t *pixels;
    uint32_t gl_handle;
    uint32_t is_float;
    uint32_t has_alpha;
    uint32_t has_smooth_alpha;

    uint32_t mip_count;
    uint32_t reserved0;

    // If non-NULL, contains a full mip chain in RAM (generated on worker threads).
    // The render thread uploads individual mip levels from this staging buffer.
    asset_image_mip_chain_t *mips;

    // Streaming state (mip indices: 0 = highest quality, mip_count-1 = lowest quality).
    uint32_t stream_current_top_mip;
    uint32_t stream_target_top_mip;
    uint32_t stream_min_safety_mip;

    uint32_t stream_pending_target_top_mip;
    uint16_t stream_pending_frames;
    uint16_t stream_priority; // higher = more important

    uint64_t stream_residency_mask; // bit i set => mip i is resident (tracked only for mip_count<=64)
    uint64_t stream_last_used_frame;
    uint64_t stream_last_used_ms;

    uint32_t stream_best_target_mip_frame;
    uint32_t stream_best_target_mip; // aggregator for current frame
    uint32_t stream_last_upload_frame;
    uint32_t stream_last_evict_frame;

    uint8_t stream_forced;
    uint8_t stream_forced_pad0[3];
    uint32_t stream_forced_top_mip;

    // Partial mip upload state (one mip at a time, uploaded in row slices).
    uint32_t stream_upload_inflight_mip; // 0xFFFFFFFF when idle
    uint32_t stream_upload_row;          // next row to upload within inflight mip

    // Streamed-resident bytes (sum of resident mip sizes, not GL allocation size).
    uint64_t vram_bytes;
} asset_image_t;
