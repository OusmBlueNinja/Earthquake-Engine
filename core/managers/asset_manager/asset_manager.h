#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "utils/logger.h"
#include "vector.h"
#include "handle.h"
#include "asset_types.h"

#define iHANDLE_TYPE_ASSET 1

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
    asset_any_t asset;
} asset_slot_t;

typedef struct asset_job_t
{
    ihandle_t handle;
    asset_type_t type;
    char *path;
} asset_job_t;

typedef struct asset_done_t
{
    ihandle_t handle;
    bool ok;
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

struct asset_manager_t;

typedef bool (*asset_load_fn_t)(struct asset_manager_t *am, const char *path, asset_any_t *out_asset);
typedef bool (*asset_init_fn_t)(struct asset_manager_t *am, asset_any_t *asset);
typedef void (*asset_cleanup_fn_t)(struct asset_manager_t *am, asset_any_t *asset);

typedef struct asset_module_desc_t
{
    asset_type_t type;
    const char *name;
    asset_load_fn_t load_fn;
    asset_init_fn_t init_fn;
    asset_cleanup_fn_t cleanup_fn;
} asset_module_desc_t;

typedef struct asset_manager_desc_t
{
    uint32_t worker_count;
    uint32_t max_inflight_jobs;
    ihandle_type_t handle_type;
} asset_manager_desc_t;

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
} asset_manager_t;

bool asset_manager_init(asset_manager_t *am, const asset_manager_desc_t *desc);
void asset_manager_shutdown(asset_manager_t *am);

bool asset_manager_register_module(asset_manager_t *am, asset_module_desc_t module);

ihandle_t asset_manager_request(asset_manager_t *am, asset_type_t type, const char *path);
ihandle_t asset_manager_submit_raw(asset_manager_t *am, asset_type_t type, const void *raw_asset);

void asset_manager_pump(asset_manager_t *am);

const asset_any_t *asset_manager_get_any(const asset_manager_t *am, ihandle_t h);

static inline const asset_image_t *asset_manager_get_image(const asset_manager_t *am, ihandle_t h)
{
    const asset_any_t *a = asset_manager_get_any(am, h);
    if (!a || a->type != ASSET_IMAGE)
        return NULL;
    return &a->as.image;
}
