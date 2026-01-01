#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "utils/logger.h"
#include "vector.h"
#include "handle.h"
#include "asset_types.h"

#define iHANDLE_TYPE_ASSET 1

#define SAVE_FLAG_NONE 0u
#define SAVE_FLAG_SEPARATE_ASSETS (1u << 0)

typedef uint32_t asset_flags_t;

#define ASSET_FLAG_NONE 0u
#define ASSET_FLAG_NO_UNLOAD (1u << 0)

typedef struct mutex_t
{
    void *p;
} mutex_t;
typedef struct cond_t
{
    void *p;
} cond_t;
typedef struct thread_t
{
    void *p;
} thread_t;

typedef struct asset_slot_t
{
    uint16_t generation;
    uint16_t module_index;
    ihandle_t persistent;
    asset_any_t asset;

    asset_flags_t flags;
    uint8_t path_is_ptr;
    uint8_t inflight;
    uint16_t requested_type;
    uint64_t last_touched_frame;
    uint64_t last_requested_ms;
    char *path;
} asset_slot_t;

typedef struct asset_job_t
{
    ihandle_t handle;
    asset_type_t type;
    uint8_t path_is_ptr;
    char *path;
} asset_job_t;

typedef struct asset_done_t
{
    ihandle_t handle;
    ihandle_t persistent;
    bool ok;
    uint16_t module_index;
    asset_any_t asset;
} asset_done_t;

typedef struct job_queue_t
{
    asset_job_t *buf;
    uint32_t cap;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    mutex_t m;
    cond_t cv;
} job_queue_t;

typedef struct done_queue_t
{
    asset_done_t *buf;
    uint32_t cap;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    mutex_t m;
} done_queue_t;

typedef struct asset_blob_t
{
    uint8_t *data;
    uint32_t size;
    uint32_t align;
    uint32_t uncompressed_size;
    uint8_t codec;
    uint8_t flags;
    uint16_t reserved;
} asset_blob_t;

struct asset_manager_t;
typedef struct asset_manager_t asset_manager_t;

typedef bool (*asset_load_fn_t)(asset_manager_t *am, const char *path, uint32_t path_is_ptr, asset_any_t *out, ihandle_t *out_handle);

typedef bool (*asset_init_fn_t)(asset_manager_t *am, asset_any_t *asset);
typedef void (*asset_cleanup_fn_t)(asset_manager_t *am, asset_any_t *asset);

typedef bool (*asset_save_blob_fn_t)(asset_manager_t *am, ihandle_t h, const asset_any_t *a, asset_blob_t *out);

typedef void (*asset_blob_free_fn_t)(asset_manager_t *am, asset_blob_t *blob);
typedef bool (*asset_can_load_fn_t)(asset_manager_t *am, const char *path, uint32_t path_is_ptr);

typedef struct asset_module_desc_t
{
    asset_type_t type;
    const char *name;
    asset_load_fn_t load_fn;
    asset_init_fn_t init_fn;
    asset_cleanup_fn_t cleanup_fn;
    asset_save_blob_fn_t save_blob_fn;
    asset_blob_free_fn_t blob_free_fn;
    asset_can_load_fn_t can_load_fn;
} asset_module_desc_t;

typedef struct asset_manager_desc_t
{
    uint32_t worker_count;
    uint32_t max_inflight_jobs;
    ihandle_type_t handle_type;

    uint64_t vram_budget_bytes;
    uint32_t stream_unused_frames;
    uint32_t stream_unused_ms;
    uint32_t streaming_enabled;

    uint64_t upload_budget_bytes_per_pump;
    uint32_t pump_per_frame;
} asset_manager_desc_t;

typedef struct asset_manager_stats_t
{
    uint64_t frame_index;

    uint64_t vram_budget_bytes;
    uint64_t vram_resident_bytes;

    uint64_t upload_bytes_last_pump;
    uint64_t evicted_bytes_last_pump;

    uint32_t textures_resident;

    uint32_t textures_loaded_total;
    uint32_t textures_reloaded_total;
    uint32_t textures_evicted_total;

    uint32_t jobs_pending;
    uint32_t done_pending;

    uint32_t streaming_enabled;
} asset_manager_stats_t;

typedef struct asset_manager_t
{
    vector_t slots;
    vector_t modules;

    job_queue_t jobs;
    done_queue_t done;

    thread_t *workers;
    uint32_t worker_count;

    ihandle_type_t handle_type;

    uint32_t shutting_down;
    mutex_t state_m;

    uint64_t prng_state;

    uint8_t asset_type_has_save[ASSET_MAX];

    uint64_t frame_index;
    uint64_t vram_budget_bytes;
    uint32_t stream_unused_frames;
    uint32_t stream_unused_ms;
    uint32_t streaming_enabled;

    uint64_t now_ms;
    uint32_t unload_scan_index;

    // Soft budget to avoid hitching: limits how much "new VRAM" we finalize per pump.
    // Note: a single large texture upload can still stall for one frame; this mostly prevents
    // doing many uploads in the same pump.
    uint64_t upload_budget_bytes_per_pump;
    uint32_t pump_per_frame;

    asset_manager_stats_t stats;

    uint32_t asset_get_any_cnt_frame;
    uint32_t asset_get_any_cnt_last_frame;
} asset_manager_t;

enum
{
    ASSET_DEBUG_PATH_MAX = 260
};

typedef struct asset_debug_slot_t
{
    uint32_t slot_index;
    ihandle_t handle;
    ihandle_t persistent;

    asset_type_t type;
    asset_state_t state;
    uint16_t module_index;
    uint16_t requested_type;

    uint8_t inflight;
    uint8_t path_is_ptr;
    uint16_t reserved0;

    asset_flags_t flags;

    uint64_t last_touched_frame;
    uint64_t last_requested_ms;
    uint64_t vram_bytes;

    char path[ASSET_DEBUG_PATH_MAX];
} asset_debug_slot_t;

typedef struct asset_manager_debug_snapshot_t
{
    uint32_t slot_count;
    uint32_t streaming_enabled;
    uint32_t stream_unused_frames;
    uint32_t stream_unused_ms;

    uint32_t unload_scan_index;
    uint32_t jobs_pending;
    uint32_t done_pending;

    uint64_t frame_index;
    uint64_t now_ms;
    uint64_t vram_budget_bytes;
    uint64_t vram_resident_bytes;
} asset_manager_debug_snapshot_t;

bool asset_manager_init(asset_manager_t *am, const asset_manager_desc_t *desc);
void asset_manager_shutdown(asset_manager_t *am);

bool asset_manager_register_module(asset_manager_t *am, asset_module_desc_t module);

ihandle_t asset_manager_request_ptr(asset_manager_t *am, asset_type_t type, void *ptr);
ihandle_t asset_manager_request(asset_manager_t *am, asset_type_t type, const char *path);
ihandle_t asset_manager_submit_raw(asset_manager_t *am, asset_type_t type, const void *raw_asset);

void asset_manager_pump(asset_manager_t *am, uint32_t max_per_frame);
void asset_manager_pump_frame(asset_manager_t *am);

void asset_manager_begin_frame(asset_manager_t *am);

const asset_any_t *asset_manager_get_any(const asset_manager_t *am, ihandle_t h);
void asset_manager_touch(asset_manager_t *am, ihandle_t h);
bool asset_manager_update_flags(asset_manager_t *am, ihandle_t h, asset_flags_t set_mask, asset_flags_t clear_mask);
bool asset_manager_get_stats(const asset_manager_t *am, asset_manager_stats_t *out);
void asset_manager_set_streaming(asset_manager_t *am, uint32_t enabled, uint64_t vram_budget_bytes, uint32_t unused_frames);
void asset_manager_set_upload_budget(asset_manager_t *am, uint64_t bytes_per_pump);
void asset_manager_end_frame(asset_manager_t *am);

uint32_t asset_manager_debug_get_slot_count(const asset_manager_t *am);
bool asset_manager_debug_get_slots(const asset_manager_t *am, asset_debug_slot_t *out_slots, uint32_t cap, asset_manager_debug_snapshot_t *out_snapshot);

bool asset_manager_build_pack(asset_manager_t *am, uint8_t **out_data, uint32_t *out_size);
bool asset_manager_build_pack_ex(asset_manager_t *am, uint8_t **out_data, uint32_t *out_size, uint32_t flags, const char *base_path);
void asset_manager_free_pack(uint8_t *data);

int all_loaded(asset_manager_t *am);

static inline const asset_image_t *asset_manager_get_image(const asset_manager_t *am, ihandle_t h)
{
    const asset_any_t *a = asset_manager_get_any(am, h);
    if (!a || a->type != ASSET_IMAGE)
        return NULL;
    return &a->as.image;
}
