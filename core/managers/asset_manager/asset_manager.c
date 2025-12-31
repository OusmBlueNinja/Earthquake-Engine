#include "asset_manager.h"
#include "loaders/register_modules.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#endif

#if defined(_WIN32)

typedef struct win_mutex_t
{
    SRWLOCK l;
} win_mutex_t;

typedef struct win_cond_t
{
    CONDITION_VARIABLE cv;
} win_cond_t;

typedef struct win_thread_t
{
    HANDLE h;
    void (*fn)(void *);
    void *arg;
} win_thread_t;

static DWORD WINAPI win_thread_trampoline(LPVOID p)
{
    win_thread_t *t = (win_thread_t *)p;
    t->fn(t->arg);
    return 0;
}

static bool mutex_init_impl(mutex_t *m)
{
    win_mutex_t *x = (win_mutex_t *)malloc(sizeof(win_mutex_t));
    if (!x)
        return false;
    InitializeSRWLock(&x->l);
    m->p = x;
    return true;
}

static void mutex_destroy_impl(mutex_t *m)
{
    free(m->p);
    m->p = NULL;
}

static void mutex_lock_impl(mutex_t *m)
{
    win_mutex_t *x = (win_mutex_t *)m->p;
    AcquireSRWLockExclusive(&x->l);
}

static void mutex_unlock_impl(mutex_t *m)
{
    win_mutex_t *x = (win_mutex_t *)m->p;
    ReleaseSRWLockExclusive(&x->l);
}

static bool cond_init_impl(cond_t *c)
{
    win_cond_t *x = (win_cond_t *)malloc(sizeof(win_cond_t));
    if (!x)
        return false;
    InitializeConditionVariable(&x->cv);
    c->p = x;
    return true;
}

static void cond_destroy_impl(cond_t *c)
{
    free(c->p);
    c->p = NULL;
}

static void cond_wait_impl(cond_t *c, mutex_t *m)
{
    win_cond_t *cv = (win_cond_t *)c->p;
    win_mutex_t *mx = (win_mutex_t *)m->p;
    SleepConditionVariableSRW(&cv->cv, &mx->l, INFINITE, 0);
}

static void cond_signal_impl(cond_t *c)
{
    win_cond_t *cv = (win_cond_t *)c->p;
    WakeConditionVariable(&cv->cv);
}

static void cond_broadcast_impl(cond_t *c)
{
    win_cond_t *cv = (win_cond_t *)c->p;
    WakeAllConditionVariable(&cv->cv);
}

static bool thread_create_impl(thread_t *t, void (*fn)(void *), void *arg)
{
    win_thread_t *x = (win_thread_t *)malloc(sizeof(win_thread_t));
    if (!x)
        return false;
    x->fn = fn;
    x->arg = arg;
    x->h = CreateThread(NULL, 0, win_thread_trampoline, x, 0, NULL);
    if (!x->h)
    {
        free(x);
        return false;
    }
    t->p = x;
    return true;
}

static void thread_join_impl(thread_t *t)
{
    win_thread_t *x = (win_thread_t *)t->p;
    if (!x)
        return;
    WaitForSingleObject(x->h, INFINITE);
    CloseHandle(x->h);
    free(x);
    t->p = NULL;
}

#else

typedef struct posix_mutex_t
{
    pthread_mutex_t m;
} posix_mutex_t;

typedef struct posix_cond_t
{
    pthread_cond_t c;
} posix_cond_t;

typedef struct posix_thread_t
{
    pthread_t t;
    void (*fn)(void *);
    void *arg;
} posix_thread_t;

static void *posix_thread_trampoline(void *p)
{
    posix_thread_t *x = (posix_thread_t *)p;
    x->fn(x->arg);
    return NULL;
}

static bool mutex_init_impl(mutex_t *m)
{
    posix_mutex_t *x = (posix_mutex_t *)malloc(sizeof(posix_mutex_t));
    if (!x)
        return false;
    if (pthread_mutex_init(&x->m, NULL) != 0)
    {
        free(x);
        return false;
    }
    m->p = x;
    return true;
}

static void mutex_destroy_impl(mutex_t *m)
{
    posix_mutex_t *x = (posix_mutex_t *)m->p;
    if (!x)
        return;
    pthread_mutex_destroy(&x->m);
    free(x);
    m->p = NULL;
}

static void mutex_lock_impl(mutex_t *m)
{
    posix_mutex_t *x = (posix_mutex_t *)m->p;
    pthread_mutex_lock(&x->m);
}

static void mutex_unlock_impl(mutex_t *m)
{
    posix_mutex_t *x = (posix_mutex_t *)m->p;
    pthread_mutex_unlock(&x->m);
}

static bool cond_init_impl(cond_t *c)
{
    posix_cond_t *x = (posix_cond_t *)malloc(sizeof(posix_cond_t));
    if (!x)
        return false;
    if (pthread_cond_init(&x->c, NULL) != 0)
    {
        free(x);
        return false;
    }
    c->p = x;
    return true;
}

static void cond_destroy_impl(cond_t *c)
{
    posix_cond_t *x = (posix_cond_t *)c->p;
    if (!x)
        return;
    pthread_cond_destroy(&x->c);
    free(x);
    c->p = NULL;
}

static void cond_wait_impl(cond_t *c, mutex_t *m)
{
    posix_cond_t *cv = (posix_cond_t *)c->p;
    posix_mutex_t *mx = (posix_mutex_t *)m->p;
    pthread_cond_wait(&cv->c, &mx->m);
}

static void cond_signal_impl(cond_t *c)
{
    posix_cond_t *cv = (posix_cond_t *)c->p;
    pthread_cond_signal(&cv->c);
}

static void cond_broadcast_impl(cond_t *c)
{
    posix_cond_t *cv = (posix_cond_t *)c->p;
    pthread_cond_broadcast(&cv->c);
}

static bool thread_create_impl(thread_t *t, void (*fn)(void *), void *arg)
{
    posix_thread_t *x = (posix_thread_t *)malloc(sizeof(posix_thread_t));
    if (!x)
        return false;
    x->fn = fn;
    x->arg = arg;
    if (pthread_create(&x->t, NULL, posix_thread_trampoline, x) != 0)
    {
        free(x);
        return false;
    }
    t->p = x;
    return true;
}

static void thread_join_impl(thread_t *t)
{
    posix_thread_t *x = (posix_thread_t *)t->p;
    if (!x)
        return;
    pthread_join(x->t, NULL);
    free(x);
    t->p = NULL;
}

#endif

static void asset_zero(asset_any_t *a)
{
    memset(a, 0, sizeof(*a));
    a->type = ASSET_NONE;
    a->state = ASSET_STATE_EMPTY;
}

static const asset_module_desc_t *asset_manager_get_module_by_index(const asset_manager_t *am, uint32_t idx)
{
    if (!am || idx >= am->modules.size)
        return NULL;
    return (const asset_module_desc_t *)vector_impl_at((vector_t *)&am->modules, idx);
}

static uint32_t asset_manager_find_first_module_index(const asset_manager_t *am, asset_type_t type)
{
    for (uint32_t i = 0; i < am->modules.size; ++i)
    {
        const asset_module_desc_t *m = (const asset_module_desc_t *)vector_impl_at((vector_t *)&am->modules, i);
        if (m && m->type == type)
            return i;
    }
    return 0xFFFFFFFFu;
}

static uint32_t asset_manager_find_first_save_module_index(const asset_manager_t *am, asset_type_t type)
{
    if (!am)
        return 0xFFFFFFFFu;

    if ((unsigned)type >= (unsigned)ASSET_MAX)
        return 0xFFFFFFFFu;

    if (!am->asset_type_has_save[type])
        return 0xFFFFFFFFu;

    for (uint32_t i = 0; i < am->modules.size; ++i)
    {
        const asset_module_desc_t *m = (const asset_module_desc_t *)vector_impl_at((vector_t *)&am->modules, i);
        if (!m)
            continue;

        if (m->type != type)
            continue;

        if (!m->save_blob_fn)
            continue;

        if (!m->blob_free_fn)
            continue;

        return i;
    }

    return 0xFFFFFFFFu;
}

static const asset_module_desc_t *find_module_const(const asset_manager_t *am, asset_type_t type)
{
    for (uint32_t i = 0; i < am->modules.size; ++i)
    {
        const asset_module_desc_t *m = (const asset_module_desc_t *)vector_impl_at((vector_t *)&am->modules, i);
        if (m && m->type == type)
            return m;
    }
    return NULL;
}

static const char *asset_type_name(const asset_manager_t *am, asset_type_t type)
{
    const asset_module_desc_t *m = find_module_const(am, type);
    if (m && m->name)
        return m->name;
    return "ASSET_UNKNOWN";
}

static void asset_cleanup_by_module(asset_manager_t *am, asset_any_t *a, uint16_t module_index)
{
    if (!a)
        return;

    const asset_module_desc_t *m = NULL;
    if (module_index != 0xFFFFu)
        m = asset_manager_get_module_by_index(am, module_index);

    if (!m || m->type != a->type)
        m = find_module_const(am, a->type);

    if (m && m->cleanup_fn)
        m->cleanup_fn(am, a);

    asset_zero(a);
}

static void slot_cleanup_asset_only(asset_manager_t *am, asset_slot_t *s)
{
    if (!s)
        return;
    asset_cleanup_by_module(am, &s->asset, s->module_index);
    s->module_index = 0xFFFFu;
}

static void slot_destroy(asset_manager_t *am, asset_slot_t *s)
{
    if (!s)
        return;

    slot_cleanup_asset_only(am, s);

    if (!s->path_is_ptr && s->path)
        free(s->path);
    s->path = NULL;
    s->path_is_ptr = 0;
    s->inflight = 0;
    s->requested_type = 0;
    s->last_touched_frame = 0;

    s->persistent = ihandle_invalid();
}

static bool slot_valid_locked(asset_manager_t *am, ihandle_t h, asset_slot_t **out_slot)
{
    if (!ihandle_is_valid(h))
        return false;
    if (ihandle_type(h) != am->handle_type)
        return false;

    uint16_t idx = ihandle_index(h);
    if (idx == 0)
        return false;

    uint32_t i = (uint32_t)(idx - 1);
    if (i >= am->slots.size)
        return false;

    asset_slot_t *s = (asset_slot_t *)vector_impl_at((vector_t *)&am->slots, i);
    if (s->generation != ihandle_generation(h))
        return false;

    if (out_slot)
        *out_slot = s;
    return true;
}

static uint64_t prng_next_u64(asset_manager_t *am)
{
    uint64_t x = am->prng_state;
    if (x == 0)
        x = 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    am->prng_state = x;
    return x * 2685821657736338717ull;
}

static ihandle_t make_persistent_handle(asset_manager_t *am, asset_type_t type)
{
    uint64_t r = prng_next_u64(am);
    uint32_t v = (uint32_t)(r ^ (r >> 32) ^ (uint32_t)type);
    if (v == 0)
        v = 1u;
    ihandle_t h;
    h.value = v;
    h.type = (ihandle_type_t)type;
    h.meta = 0;
    return h;
}

static void jobq_init(job_queue_t *q, uint32_t cap)
{
    q->buf = (asset_job_t *)calloc((size_t)cap, sizeof(asset_job_t));
    q->cap = cap;
    q->head = q->tail = q->count = 0;
    mutex_init_impl(&q->m);
    cond_init_impl(&q->cv);
}

static void jobq_destroy(job_queue_t *q)
{
    if (q->buf)
    {
        for (uint32_t i = 0; i < q->count; ++i)
        {
            uint32_t idx = (q->head + i) % q->cap;
            if (!q->buf[idx].path_is_ptr)
                free(q->buf[idx].path);
        }
        free(q->buf);
    }
    cond_destroy_impl(&q->cv);
    mutex_destroy_impl(&q->m);
    memset(q, 0, sizeof(*q));
}

static void jobq_drain(job_queue_t *q)
{
    mutex_lock_impl(&q->m);
    while (q->count > 0)
    {
        asset_job_t *j = &q->buf[q->head];
        if (!j->path_is_ptr)
            free(j->path);
        j->path = NULL;
        j->path_is_ptr = 0;
        q->head = (q->head + 1) % q->cap;
        q->count--;
    }
    q->head = 0;
    q->tail = 0;
    mutex_unlock_impl(&q->m);
}

static bool jobq_push(job_queue_t *q, const asset_job_t *j)
{
    mutex_lock_impl(&q->m);
    if (q->count == q->cap)
    {
        mutex_unlock_impl(&q->m);
        return false;
    }
    q->buf[q->tail] = *j;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    cond_signal_impl(&q->cv);
    mutex_unlock_impl(&q->m);
    return true;
}

static bool jobq_pop_blocking(job_queue_t *q, asset_job_t *out, uint32_t *shutting_down, mutex_t *state_m)
{
    mutex_lock_impl(&q->m);
    for (;;)
    {
        mutex_lock_impl(state_m);
        uint32_t sd = *shutting_down;
        mutex_unlock_impl(state_m);

        if (sd)
        {
            mutex_unlock_impl(&q->m);
            return false;
        }

        if (q->count > 0)
            break;

        cond_wait_impl(&q->cv, &q->m);
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    mutex_unlock_impl(&q->m);
    return true;
}

static void doneq_init(done_queue_t *q, uint32_t cap)
{
    q->buf = (asset_done_t *)calloc((size_t)cap, sizeof(asset_done_t));
    q->cap = cap;
    q->head = q->tail = q->count = 0;
    mutex_init_impl(&q->m);
}

static void doneq_destroy(done_queue_t *q)
{
    free(q->buf);
    mutex_destroy_impl(&q->m);
    memset(q, 0, sizeof(*q));
}

static bool doneq_push(done_queue_t *q, const asset_done_t *d)
{
    bool ok = false;
    mutex_lock_impl(&q->m);
    if (q->count < q->cap)
    {
        q->buf[q->tail] = *d;
        q->tail = (q->tail + 1) % q->cap;
        q->count++;
        ok = true;
    }
    mutex_unlock_impl(&q->m);
    return ok;
}

static bool doneq_pop(done_queue_t *q, asset_done_t *out)
{
    bool ok = false;
    mutex_lock_impl(&q->m);
    if (q->count > 0)
    {
        *out = q->buf[q->head];
        q->head = (q->head + 1) % q->cap;
        q->count--;
        ok = true;
    }
    mutex_unlock_impl(&q->m);
    return ok;
}

typedef struct worker_ctx_t
{
    asset_manager_t *am;
} worker_ctx_t;

static const char *asset_path_ext(const char *path)
{
    if (!path)
        return "";
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path)
        return "";
    return dot;
}

static bool asset_try_load_any(asset_manager_t *am, asset_type_t type, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, uint16_t *out_module_index, ihandle_t *out_persistent)
{
    if (!am || !out_asset || !out_module_index || !out_persistent)
        return false;
    if (!path)
        return false;

    if (path_is_ptr)
    {
        if ((uintptr_t)path < (uintptr_t)0x10000u)
            return false;
    }
    else
    {
        if (!path[0])
            return false;
    }

    *out_module_index = 0xFFFFu;
    *out_persistent = ihandle_invalid();
    asset_zero(out_asset);

    uint8_t have_can = 0;
    uint8_t have_any_load = 0;

    uint16_t strong[512];
    uint32_t strong_n = 0;

    uint16_t weak[512];
    uint32_t weak_n = 0;

    for (uint32_t i = 0; i < am->modules.size; ++i)
    {
        const asset_module_desc_t *m = (const asset_module_desc_t *)vector_impl_at(&am->modules, i);
        if (!m || m->type != type)
            continue;

        if (m->load_fn)
        {
            if ((uintptr_t)m->load_fn < (uintptr_t)0x10000u)
                continue;
            have_any_load = 1;
        }
        else
        {
            continue;
        }

        if (m->can_load_fn)
        {
            if ((uintptr_t)m->can_load_fn < (uintptr_t)0x10000u)
                continue;

            have_can = 1;

            bool ok = m->can_load_fn(am, path, path_is_ptr);
            if (ok && strong_n < (uint32_t)(sizeof(strong) / sizeof(strong[0])))
                strong[strong_n++] = (uint16_t)i;
        }
        else
        {
            if (weak_n < (uint32_t)(sizeof(weak) / sizeof(weak[0])))
                weak[weak_n++] = (uint16_t)i;
        }
    }

    if (!have_any_load)
    {
        const char *ext = path_is_ptr ? "" : asset_path_ext(path);

        if (path_is_ptr)
            LOG_ERROR("No loader modules registered for asset type %u (path=%p).", (unsigned)type, (void *)path);
        else
            LOG_ERROR("No loader modules registered for asset type %u (file=%s%s%s).",
                      (unsigned)type,
                      path,
                      ext[0] ? " ext=" : "",
                      ext[0] ? ext : "");

        return false;
    }

    for (uint32_t k = 0; k < strong_n; ++k)
    {
        uint16_t i = strong[k];
        const asset_module_desc_t *m = asset_manager_get_module_by_index(am, i);
        if (!m || !m->load_fn)
            continue;
        if ((uintptr_t)m->load_fn < (uintptr_t)0x10000u)
            continue;

        asset_any_t tmp;
        asset_zero(&tmp);

        ihandle_t hid = ihandle_invalid();
        if (m->load_fn(am, path, path_is_ptr, &tmp, &hid))
        {
            *out_asset = tmp;
            *out_module_index = i;
            *out_persistent = hid;
            return true;
        }
    }

    if (!have_can)
    {
        for (uint32_t i = 0; i < am->modules.size; ++i)
        {
            const asset_module_desc_t *m = (const asset_module_desc_t *)vector_impl_at(&am->modules, i);
            if (!m || m->type != type || !m->load_fn)
                continue;
            if ((uintptr_t)m->load_fn < (uintptr_t)0x10000u)
                continue;

            asset_any_t tmp;
            asset_zero(&tmp);

            ihandle_t hid = ihandle_invalid();
            if (m->load_fn(am, path, path_is_ptr, &tmp, &hid))
            {
                *out_asset = tmp;
                *out_module_index = (uint16_t)i;
                *out_persistent = hid;
                return true;
            }
        }

        {
            const char *ext = path_is_ptr ? "" : asset_path_ext(path);
            if (path_is_ptr)
                LOG_ERROR("Failed to load asset: no loader succeeded for type=%u path_ptr=%p", (unsigned)type, (void *)path);
            else
                LOG_ERROR("Failed to load asset: no loader succeeded for type=%u ext=%s path=%s", (unsigned)type, ext[0] ? ext : "(none)", path);
        }

        return false;
    }

    for (uint32_t k = 0; k < weak_n; ++k)
    {
        uint16_t i = weak[k];
        const asset_module_desc_t *m = asset_manager_get_module_by_index(am, i);
        if (!m || !m->load_fn)
            continue;
        if ((uintptr_t)m->load_fn < (uintptr_t)0x10000u)
            continue;

        asset_any_t tmp;
        asset_zero(&tmp);

        ihandle_t hid = ihandle_invalid();
        if (m->load_fn(am, path, path_is_ptr, &tmp, &hid))
        {
            *out_asset = tmp;
            *out_module_index = i;
            *out_persistent = hid;
            return true;
        }
    }

    {
        const char *ext = path_is_ptr ? "" : asset_path_ext(path);
        if (path_is_ptr)
            LOG_ERROR("Failed to find a compatible module for type=%u path_ptr=%p", (unsigned)type, (void *)path);
        else
            LOG_ERROR("Failed to find a compatible module for type=%u ext=%s path=%s", (unsigned)type, ext[0] ? ext : "(none)", path);
    }

    return false;
}

static void worker_main(void *p)
{
    worker_ctx_t *ctx = (worker_ctx_t *)p;
    asset_manager_t *am = ctx->am;

    for (;;)
    {
        asset_job_t j;
        if (!jobq_pop_blocking(&am->jobs, &j, &am->shutting_down, &am->state_m))
            break;

        mutex_lock_impl(&am->state_m);
        uint32_t sd = am->shutting_down;
        mutex_unlock_impl(&am->state_m);

        if (sd)
        {
            if (!j.path_is_ptr)
                free(j.path);
            break;
        }

        asset_done_t d;
        memset(&d, 0, sizeof(d));
        d.handle = j.handle;
        d.persistent = ihandle_invalid();
        d.ok = false;
        d.module_index = 0xFFFFu;
        asset_zero(&d.asset);

        asset_any_t out;
        uint16_t midx = 0xFFFFu;
        ihandle_t ph = ihandle_invalid();

        bool ok = asset_try_load_any(am, j.type, j.path, j.path_is_ptr, &out, &midx, &ph);
        d.ok = ok;
        d.module_index = midx;
        d.persistent = ph;

        if (ok)
            d.asset = out;

        if (!j.path_is_ptr)
            free(j.path);
        doneq_push(&am->done, &d);
    }

    free(ctx);
}

bool asset_manager_register_module(asset_manager_t *am, asset_module_desc_t module)
{
    if (!am)
    {
        LOG_ERROR("asset_manager_register_module: am is NULL");
        return false;
    }

    if (module.type == ASSET_NONE)
    {
        LOG_ERROR("asset_manager_register_module: invalid module type (ASSET_NONE)");
        return false;
    }

    if (!module.name || !module.name[0])
    {
        LOG_ERROR("asset_manager_register_module: module name is NULL/empty (type=%s)",
                  ASSET_TYPE_TO_STRING(module.type));
        return false;
    }

    if (!module.load_fn &&
        !module.init_fn &&
        !module.cleanup_fn &&
        !module.save_blob_fn &&
        !module.blob_free_fn &&
        !module.can_load_fn)
    {
        LOG_ERROR("asset_manager_register_module: all module functions are NULL (type=%s, name=%s)",
                  ASSET_TYPE_TO_STRING(module.type), module.name);
        return false;
    }

    if (module.save_blob_fn && !module.blob_free_fn)
    {
        LOG_ERROR("asset_manager_register_module: save_blob_fn provided but blob_free_fn is NULL (type=%s, name=%s)",
                  ASSET_TYPE_TO_STRING(module.type), module.name);
        return false;
    }

    if ((unsigned)module.type < (unsigned)ASSET_MAX)
    {
        if (module.save_blob_fn)
        {
            if (am->asset_type_has_save[module.type])
                LOG_WARN("%s already has a save function, overwriting.", ASSET_TYPE_TO_STRING(module.type));
            am->asset_type_has_save[module.type] = 1;
        }
    }

    vector_impl_push_back(&am->modules, &module);
    return true;
}

static bool asset_from_raw(asset_type_t type, const void *raw, asset_any_t *out)
{
    if (!raw || !out)
        return false;

    asset_zero(out);
    out->type = type;
    out->state = ASSET_STATE_LOADING;

    switch (type)
    {
    case ASSET_IMAGE:
        out->as.image = *(const asset_image_t *)raw;
        return true;
    case ASSET_MATERIAL:
        out->as.material = *(const asset_material_t *)raw;
        return true;
    default:
        return false;
    }
}

static asset_slot_t *alloc_slot_locked(asset_manager_t *am, asset_type_t type, ihandle_t *out_handle)
{
    asset_slot_t s;
    memset(&s, 0, sizeof(s));
    s.generation = 1;
    s.module_index = 0xFFFFu;
    s.persistent = ihandle_invalid();
    asset_zero(&s.asset);
    s.asset.type = type;
    s.asset.state = ASSET_STATE_LOADING;
    s.path_is_ptr = 0;
    s.inflight = 0;
    s.requested_type = (uint16_t)type;
    s.last_touched_frame = 0;
    s.path = NULL;

    vector_impl_push_back(&am->slots, &s);

    uint32_t idx0 = (uint32_t)(am->slots.size - 1);
    asset_slot_t *slot = (asset_slot_t *)vector_impl_at(&am->slots, idx0);

    if (out_handle)
        *out_handle = ihandle_make(am->handle_type, (uint16_t)(idx0 + 1), slot->generation);

    return slot;
}

bool asset_manager_init(asset_manager_t *am, const asset_manager_desc_t *desc)
{
    memset(am, 0, sizeof(*am));
    memset(am->asset_type_has_save, 0, sizeof(am->asset_type_has_save));
    uint32_t wc = 4;
    uint32_t cap = 1024;
    ihandle_type_t ht = 1;
    uint64_t vram_budget_bytes = 0;
    uint32_t stream_unused_frames = 120;
    uint32_t streaming_enabled = 0;

    if (desc)
    {
        if (desc->worker_count)
            wc = desc->worker_count;
        if (desc->max_inflight_jobs)
            cap = desc->max_inflight_jobs;
        if (desc->handle_type)
            ht = desc->handle_type;
        if (desc->vram_budget_bytes)
            vram_budget_bytes = desc->vram_budget_bytes;
        if (desc->stream_unused_frames)
            stream_unused_frames = desc->stream_unused_frames;
        if (desc->streaming_enabled)
            streaming_enabled = desc->streaming_enabled;
    }

    am->handle_type = ht;
    am->frame_index = 0;
    am->vram_budget_bytes = vram_budget_bytes;
    am->stream_unused_frames = stream_unused_frames;
    am->streaming_enabled = streaming_enabled ? 1u : 0u;

    memset(&am->stats, 0, sizeof(am->stats));
    am->stats.frame_index = 0;
    am->stats.vram_budget_bytes = am->vram_budget_bytes;
    am->stats.streaming_enabled = am->streaming_enabled;

    am->slots = vector_impl_create_vector(sizeof(asset_slot_t));
    am->modules = vector_impl_create_vector(sizeof(asset_module_desc_t));

    jobq_init(&am->jobs, cap);
    doneq_init(&am->done, cap);

    mutex_init_impl(&am->state_m);
    am->shutting_down = 0;

    am->prng_state = ((uint64_t)(uintptr_t)am << 1) ^ ((uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull) ^ 0xD1B54A32D192ED03ull;
    if (am->prng_state == 0)
        am->prng_state = 0xA0761D6478BD642Full;

    register_asset_modules(am);

    am->workers = (thread_t *)calloc((size_t)wc, sizeof(thread_t));
    am->worker_count = wc;

    for (uint32_t i = 0; i < wc; ++i)
    {
        worker_ctx_t *ctx = (worker_ctx_t *)malloc(sizeof(worker_ctx_t));
        if (!ctx)
            return false;
        ctx->am = am;
        if (!thread_create_impl(&am->workers[i], worker_main, ctx))
            return false;
    }

    return true;
}

void asset_manager_shutdown(asset_manager_t *am)
{
    mutex_lock_impl(&am->state_m);
    am->shutting_down = 1;
    mutex_unlock_impl(&am->state_m);

    mutex_lock_impl(&am->jobs.m);
    cond_broadcast_impl(&am->jobs.cv);
    mutex_unlock_impl(&am->jobs.m);

    jobq_drain(&am->jobs);

    for (uint32_t i = 0; i < am->worker_count; ++i)
        thread_join_impl(&am->workers[i]);

    free(am->workers);
    am->workers = NULL;
    am->worker_count = 0;

    asset_done_t d;
    while (doneq_pop(&am->done, &d))
        asset_cleanup_by_module(am, &d.asset, d.module_index);

    mutex_lock_impl(&am->state_m);
    for (uint32_t i = 0; i < am->slots.size; ++i)
    {
        asset_slot_t *s = (asset_slot_t *)vector_impl_at(&am->slots, i);
        slot_destroy(am, s);
    }
    mutex_unlock_impl(&am->state_m);

    jobq_destroy(&am->jobs);
    doneq_destroy(&am->done);

    vector_impl_free(&am->modules);
    vector_impl_free(&am->slots);

    mutex_destroy_impl(&am->state_m);
    memset(am, 0, sizeof(*am));
}

ihandle_t asset_manager_request(asset_manager_t *am, asset_type_t type, const char *path)
{
    if (!am || !path || !path[0])
        return ihandle_invalid();

    {
        FILE *f = fopen(path, "rb");
        if (!f)
        {
            LOG_ERROR("File Not Found: '%s'(%u)", path, (unsigned)type);
            return ihandle_invalid();
        }
        fclose(f);
    }

    mutex_lock_impl(&am->state_m);
    uint32_t sd = am->shutting_down;
    if (sd)
    {
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }

    ihandle_t h;
    asset_slot_t *slot = alloc_slot_locked(am, type, &h);
    if (slot)
    {
        slot->path_is_ptr = 0;
        slot->inflight = 1;
        slot->requested_type = (uint16_t)type;
        slot->last_touched_frame = am->frame_index;

        size_t pn = strlen(path);
        slot->path = (char *)malloc(pn + 1);
        if (slot->path)
        {
            memcpy(slot->path, path, pn + 1);
        }
    }
    mutex_unlock_impl(&am->state_m);

    asset_job_t j;
    memset(&j, 0, sizeof(j));
    j.handle = h;
    j.type = type;
    j.path_is_ptr = 0;

    size_t n = strlen(path);
    j.path = (char *)malloc(n + 1);
    if (!j.path)
    {
        mutex_lock_impl(&am->state_m);
        asset_slot_t *s = NULL;
        if (slot_valid_locked(am, h, &s))
        {
            s->asset.state = ASSET_STATE_FAILED;
            s->module_index = 0xFFFFu;
        }
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }
    memcpy(j.path, path, n + 1);

    if (!jobq_push(&am->jobs, &j))
    {
        free(j.path);

        mutex_lock_impl(&am->state_m);
        asset_slot_t *s = NULL;
        if (slot_valid_locked(am, h, &s))
        {
            s->asset.state = ASSET_STATE_FAILED;
            s->module_index = 0xFFFFu;
            s->inflight = 0;
        }
        mutex_unlock_impl(&am->state_m);

        return ihandle_invalid();
    }

    return h;
}

ihandle_t asset_manager_request_ptr(asset_manager_t *am, asset_type_t type, void *ptr)
{
    if (!am || !ptr)
        return ihandle_invalid();

    mutex_lock_impl(&am->state_m);
    uint32_t sd = am->shutting_down;
    if (sd)
    {
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }

    ihandle_t h;
    asset_slot_t *slot = alloc_slot_locked(am, type, &h);
    if (slot)
    {
        slot->path = (char *)ptr;
        slot->path_is_ptr = 1;
        slot->inflight = 1;
        slot->requested_type = (uint16_t)type;
        slot->last_touched_frame = am->frame_index;
    }
    mutex_unlock_impl(&am->state_m);

    asset_job_t j;
    memset(&j, 0, sizeof(j));
    j.handle = h;
    j.type = type;
    j.path = (char *)ptr;
    j.path_is_ptr = 1;

    if (!jobq_push(&am->jobs, &j))
    {
        mutex_lock_impl(&am->state_m);
        asset_slot_t *s = NULL;
        if (slot_valid_locked(am, h, &s))
        {
            s->asset.state = ASSET_STATE_FAILED;
            s->module_index = 0xFFFFu;
            s->inflight = 0;
        }
        mutex_unlock_impl(&am->state_m);

        return ihandle_invalid();
    }

    return h;
}

ihandle_t asset_manager_submit_raw(asset_manager_t *am, asset_type_t type, const void *raw_asset)
{
    if (!am || !raw_asset || type == ASSET_NONE)
        return ihandle_invalid();

    mutex_lock_impl(&am->state_m);
    uint32_t sd = am->shutting_down;
    if (sd)
    {
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }

    uint32_t midx32 = asset_manager_find_first_module_index(am, type);
    if (midx32 == 0xFFFFFFFFu)
    {
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }

    ihandle_t h;
    asset_slot_t *slot = alloc_slot_locked(am, type, &h);
    mutex_unlock_impl(&am->state_m);

    if (!slot)
        return ihandle_invalid();

    asset_any_t a;
    if (!asset_from_raw(type, raw_asset, &a))
    {
        mutex_lock_impl(&am->state_m);
        slot->asset.state = ASSET_STATE_FAILED;
        slot->module_index = 0xFFFFu;
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }

    const asset_module_desc_t *mod = asset_manager_get_module_by_index(am, midx32);
    bool init_ok = true;
    if (mod && mod->init_fn)
        init_ok = mod->init_fn(am, &a);

    if (!init_ok)
    {
        asset_cleanup_by_module(am, &a, (uint16_t)midx32);

        mutex_lock_impl(&am->state_m);
        slot_cleanup_asset_only(am, slot);
        slot->asset.state = ASSET_STATE_FAILED;
        slot->module_index = 0xFFFFu;
        slot->inflight = 0;
        mutex_unlock_impl(&am->state_m);

        return ihandle_invalid();
    }

    mutex_lock_impl(&am->state_m);
    slot_cleanup_asset_only(am, slot);
    a.state = ASSET_STATE_READY;
    slot->asset = a;
    slot->module_index = (uint16_t)midx32;
    slot->persistent = make_persistent_handle(am, type);
    slot->inflight = 0;
    mutex_unlock_impl(&am->state_m);

    return h;
}

void asset_manager_set_streaming(asset_manager_t *am, uint32_t enabled, uint64_t vram_budget_bytes, uint32_t unused_frames)
{
    if (!am)
        return;
    mutex_lock_impl(&am->state_m);
    am->streaming_enabled = enabled ? 1u : 0u;
    am->vram_budget_bytes = vram_budget_bytes;
    am->stream_unused_frames = unused_frames;
    mutex_unlock_impl(&am->state_m);
}

static uint64_t asset_vram_bytes_if_resident(const asset_any_t *a)
{
    if (!a || a->state != ASSET_STATE_READY)
        return 0;
    if (a->type != ASSET_IMAGE)
        return 0;
    return a->as.image.vram_bytes;
}

typedef struct am_evict_cand_t
{
    uint32_t slot_index;
    uint64_t last_used;
    uint64_t bytes;
} am_evict_cand_t;

static int am_evict_cand_cmp(const void *a, const void *b)
{
    const am_evict_cand_t *x = (const am_evict_cand_t *)a;
    const am_evict_cand_t *y = (const am_evict_cand_t *)b;
    if (x->last_used < y->last_used)
        return -1;
    if (x->last_used > y->last_used)
        return 1;
    return 0;
}

static void asset_manager_try_evict_textures_locked(asset_manager_t *am)
{
    if (!am || !am->streaming_enabled)
        return;

    const uint64_t budget = am->vram_budget_bytes;
    if (budget == 0)
        return;

    if (am->stats.vram_resident_bytes <= budget)
        return;

    const uint64_t min_age = (uint64_t)am->stream_unused_frames;

    uint32_t cap = (uint32_t)am->slots.size;
    am_evict_cand_t *cands = (am_evict_cand_t *)malloc((size_t)cap * sizeof(am_evict_cand_t));
    if (!cands)
        return;

    uint32_t n = 0;
    for (uint32_t i = 0; i < cap; ++i)
    {
        asset_slot_t *s = (asset_slot_t *)vector_impl_at(&am->slots, i);
        if (!s)
            continue;
        if (s->asset.type != ASSET_IMAGE || s->asset.state != ASSET_STATE_READY)
            continue;
        if (s->path_is_ptr || !s->path || !s->path[0])
            continue;

        uint64_t age = (am->frame_index > s->last_touched_frame) ? (am->frame_index - s->last_touched_frame) : 0;
        if (age < min_age)
            continue;

        uint64_t bytes = s->asset.as.image.vram_bytes;
        if (!bytes)
            continue;

        cands[n++] = (am_evict_cand_t){i, s->last_touched_frame, bytes};
    }

    if (n == 0)
    {
        free(cands);
        return;
    }

    qsort(cands, (size_t)n, sizeof(am_evict_cand_t), am_evict_cand_cmp);

    for (uint32_t k = 0; k < n && am->stats.vram_resident_bytes > budget; ++k)
    {
        asset_slot_t *s = (asset_slot_t *)vector_impl_at(&am->slots, cands[k].slot_index);
        if (!s)
            continue;
        if (s->asset.type != ASSET_IMAGE || s->asset.state != ASSET_STATE_READY)
            continue;

        uint64_t bytes = s->asset.as.image.vram_bytes;
        asset_cleanup_by_module(am, &s->asset, s->module_index);
        s->asset.type = (asset_type_t)s->requested_type;
        s->asset.state = ASSET_STATE_EMPTY;
        memset(&s->asset.as, 0, sizeof(s->asset.as));
        s->module_index = 0xFFFFu;
        s->inflight = 0;

        if (am->stats.vram_resident_bytes >= bytes)
            am->stats.vram_resident_bytes -= bytes;
        else
            am->stats.vram_resident_bytes = 0;

        if (am->stats.textures_resident)
            am->stats.textures_resident--;

        am->stats.evicted_bytes_last_pump += bytes;
        am->stats.textures_evicted_total++;
    }

    free(cands);
}

void asset_manager_touch(asset_manager_t *am, ihandle_t h)
{
    if (!am || !ihandle_is_valid(h))
        return;

    mutex_lock_impl(&am->state_m);
    asset_slot_t *slot = NULL;
    if (!slot_valid_locked(am, h, &slot) || !slot)
    {
        mutex_unlock_impl(&am->state_m);
        return;
    }

    slot->last_touched_frame = am->frame_index;

    const uint32_t can_stream = (am->streaming_enabled != 0) && (slot->path_is_ptr == 0) && (slot->path && slot->path[0]);
    const uint32_t should_reload = (slot->asset.state != ASSET_STATE_READY) && (slot->inflight == 0);

    if (!can_stream || !should_reload)
    {
        mutex_unlock_impl(&am->state_m);
        return;
    }

    const asset_type_t type = (asset_type_t)slot->requested_type;
    const char *path = slot->path;

    asset_job_t j;
    memset(&j, 0, sizeof(j));
    j.handle = h;
    j.type = type;
    j.path_is_ptr = 0;

    size_t n = strlen(path);
    j.path = (char *)malloc(n + 1);
    if (!j.path)
    {
        slot->asset.state = ASSET_STATE_FAILED;
        mutex_unlock_impl(&am->state_m);
        return;
    }
    memcpy(j.path, path, n + 1);

    slot->inflight = 1;
    slot->asset.type = type;
    slot->asset.state = ASSET_STATE_LOADING;
    mutex_unlock_impl(&am->state_m);

    if (!jobq_push(&am->jobs, &j))
    {
        free(j.path);
        mutex_lock_impl(&am->state_m);
        if (slot_valid_locked(am, h, &slot) && slot)
        {
            slot->inflight = 0;
            slot->asset.state = ASSET_STATE_FAILED;
        }
        mutex_unlock_impl(&am->state_m);
    }
    else
    {
        mutex_lock_impl(&am->state_m);
        am->stats.textures_reloaded_total++;
        mutex_unlock_impl(&am->state_m);
    }
}

bool asset_manager_get_stats(const asset_manager_t *am, asset_manager_stats_t *out)
{
    if (!am || !out)
        return false;
    asset_manager_t *mut = (asset_manager_t *)am;
    mutex_lock_impl(&mut->state_m);
    *out = mut->stats;
    out->frame_index = mut->frame_index;
    out->vram_budget_bytes = mut->vram_budget_bytes;
    out->streaming_enabled = mut->streaming_enabled;
    mutex_unlock_impl(&mut->state_m);
    return true;
}

void asset_manager_pump(asset_manager_t *am, uint32_t max_per_frame)
{
    if (!am)
        return;

    if (max_per_frame == 0)
        return;

    mutex_lock_impl(&am->state_m);
    am->frame_index++;
    am->stats.frame_index = am->frame_index;
    am->stats.upload_bytes_last_pump = 0;
    am->stats.evicted_bytes_last_pump = 0;
    am->stats.vram_budget_bytes = am->vram_budget_bytes;
    am->stats.streaming_enabled = am->streaming_enabled;
    mutex_unlock_impl(&am->state_m);

    {
        mutex_lock_impl(&am->jobs.m);
        am->stats.jobs_pending = am->jobs.count;
        mutex_unlock_impl(&am->jobs.m);

        mutex_lock_impl(&am->done.m);
        am->stats.done_pending = am->done.count;
        mutex_unlock_impl(&am->done.m);
    }

    asset_done_t d;
    uint32_t processed = 0;

    while (processed < max_per_frame && doneq_pop(&am->done, &d))
    {
        processed++;

        mutex_lock_impl(&am->state_m);
        asset_slot_t *slot = NULL;
        bool ok_slot = slot_valid_locked(am, d.handle, &slot);
        if (ok_slot && slot)
            slot->inflight = 0;
        mutex_unlock_impl(&am->state_m);

        if (!ok_slot)
        {
            asset_cleanup_by_module(am, &d.asset, d.module_index);
            continue;
        }

        if (!d.ok)
        {
            asset_any_t old;
            asset_zero(&old);

            mutex_lock_impl(&am->state_m);
            slot = NULL;
            if (slot_valid_locked(am, d.handle, &slot))
            {
                old = slot->asset;
                slot_cleanup_asset_only(am, slot);
                asset_zero(&slot->asset);
                slot->asset.state = ASSET_STATE_FAILED;
                slot->module_index = 0xFFFFu;
                slot->asset.type = (asset_type_t)slot->requested_type;
                slot->inflight = 0;
            }
            mutex_unlock_impl(&am->state_m);

            asset_cleanup_by_module(am, &old, 0xFFFFu);
            asset_cleanup_by_module(am, &d.asset, d.module_index);
            continue;
        }

        const asset_module_desc_t *mod = asset_manager_get_module_by_index(am, d.module_index);
        bool init_ok = true;
        if (mod && mod->init_fn)
            init_ok = mod->init_fn(am, &d.asset);

        if (!init_ok)
        {
            asset_cleanup_by_module(am, &d.asset, d.module_index);

            asset_any_t old;
            asset_zero(&old);

            mutex_lock_impl(&am->state_m);
            slot = NULL;
            if (slot_valid_locked(am, d.handle, &slot))
            {
                old = slot->asset;
                slot_cleanup_asset_only(am, slot);
                asset_zero(&slot->asset);
                slot->asset.state = ASSET_STATE_FAILED;
                slot->module_index = 0xFFFFu;
                slot->asset.type = (asset_type_t)slot->requested_type;
                slot->inflight = 0;
            }
            mutex_unlock_impl(&am->state_m);

            asset_cleanup_by_module(am, &old, 0xFFFFu);
            continue;
        }

        asset_any_t old;
        asset_zero(&old);

        mutex_lock_impl(&am->state_m);
        slot = NULL;
        if (slot_valid_locked(am, d.handle, &slot))
        {
            uint64_t old_vram = asset_vram_bytes_if_resident(&slot->asset);
            uint64_t new_vram = asset_vram_bytes_if_resident(&d.asset);

            old = slot->asset;
            slot_cleanup_asset_only(am, slot);

            ihandle_t ph = d.persistent;
            if (!ihandle_is_valid(ph))
                ph = make_persistent_handle(am, d.asset.type);

            d.asset.state = ASSET_STATE_READY;
            slot->asset = d.asset;
            slot->module_index = d.module_index;
            if (!ihandle_is_valid(slot->persistent))
                slot->persistent = ph;

            if (old_vram)
            {
                if (am->stats.vram_resident_bytes >= old_vram)
                    am->stats.vram_resident_bytes -= old_vram;
                else
                    am->stats.vram_resident_bytes = 0;
                if (am->stats.textures_resident)
                    am->stats.textures_resident--;
            }

            if (new_vram)
            {
                am->stats.vram_resident_bytes += new_vram;
                am->stats.textures_resident++;
                am->stats.upload_bytes_last_pump += new_vram;
                am->stats.textures_loaded_total++;
            }

            asset_zero(&d.asset);
            d.persistent = ihandle_invalid();
        }
        mutex_unlock_impl(&am->state_m);

        asset_cleanup_by_module(am, &old, 0xFFFFu);
    }
}

void asset_manager_end_frame(asset_manager_t *am)
{
    if (!am)
        return;

    mutex_lock_impl(&am->state_m);
    asset_manager_try_evict_textures_locked(am);
    mutex_unlock_impl(&am->state_m);
}

const asset_any_t *asset_manager_get_any(const asset_manager_t *am, ihandle_t h)
{
    if (!am)
        return NULL;

    asset_manager_t *am_mut = (asset_manager_t *)am;

    mutex_lock_impl(&am_mut->state_m);
    asset_slot_t *slot = NULL;
    bool ok = slot_valid_locked(am_mut, h, &slot);
    if (ok && slot && slot->asset.type == ASSET_IMAGE && slot->asset.state == ASSET_STATE_READY)
        slot->last_touched_frame = am_mut->frame_index;
    const asset_any_t *ret = ok ? &slot->asset : NULL;
    mutex_unlock_impl(&am_mut->state_m);

    return ret;
}

typedef struct pack_hdr_t
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint32_t toc_count;
    uint32_t toc_offset;
    uint32_t data_offset;
    uint32_t file_size;
} pack_hdr_t;

typedef struct pack_toc_t
{
    uint64_t key;
    uint16_t type;
    uint16_t variant;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
    uint32_t uncompressed_size;
    uint8_t codec;
    uint8_t reserved1;
    uint16_t reserved2;
} pack_toc_t;

typedef struct buf_t
{
    uint8_t *p;
    uint32_t size;
    uint32_t cap;
} buf_t;

static bool buf_reserve(buf_t *b, uint32_t add)
{
    uint32_t need = b->size + add;
    if (need <= b->cap)
        return true;
    uint32_t nc = b->cap ? b->cap : 4096u;
    while (nc < need)
        nc = nc + (nc >> 1) + 1024u;
    uint8_t *np = (uint8_t *)realloc(b->p, (size_t)nc);
    if (!np)
        return false;
    b->p = np;
    b->cap = nc;
    return true;
}

static bool buf_push_bytes(buf_t *b, const void *src, uint32_t n)
{
    if (!buf_reserve(b, n))
        return false;
    memcpy(b->p + b->size, src, (size_t)n);
    b->size += n;
    return true;
}

static bool buf_push_zero(buf_t *b, uint32_t n)
{
    if (!buf_reserve(b, n))
        return false;
    memset(b->p + b->size, 0, (size_t)n);
    b->size += n;
    return true;
}

static bool buf_align(buf_t *b, uint32_t align)
{
    if (align == 0)
        return true;
    uint32_t m = align - 1u;
    uint32_t pad = (uint32_t)((align - (b->size & m)) & m);
    if (pad)
        return buf_push_zero(b, pad);
    return true;
}

static bool write_file_all(const char *path, const void *data, uint32_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = true;
    if (size)
    {
        size_t w = fwrite(data, 1, (size_t)size, f);
        ok = (w == (size_t)size);
    }

    fclose(f);
    return ok;
}

static bool ensure_directory(const char *path)
{
    if (!path || !path[0])
        return true;

#if defined(_WIN32)
    int r = _mkdir(path);
    if (r == 0)
        return true;
    DWORD e = GetLastError();
    return e == ERROR_ALREADY_EXISTS;
#else
    int r = mkdir(path, 0755);
    if (r == 0)
        return true;
    return errno == EEXIST;
#endif
}

static bool asset_save_blob(asset_manager_t *am, const asset_module_desc_t *m, ihandle_t persistent, const asset_any_t *a, asset_blob_t *out_blob)
{
    if (!out_blob)
    {
        LOG_ERROR("out_blob was NULL");
        return false;
    }

    memset(out_blob, 0, sizeof(*out_blob));
    out_blob->align = 64;

    char hb[64];
    handle_hex_triplet_filesafe(hb, persistent);

    const char *mod_name = (m && m->name) ? m->name : "<null>";
    const char *type_str = (a) ? ASSET_TYPE_TO_STRING(a->type) : "<null>";

    if (!m)
    {
        LOG_ERROR("module was NULL (type=%s handle=%s)", type_str, hb);
        return false;
    }

    if (!m->save_blob_fn)
    {
        LOG_ERROR("module has no save_blob_fn (type=%s handle=%s module=%s)", type_str, hb, mod_name);
        return false;
    }

    if (!a)
    {
        LOG_ERROR("asset pointer was NULL (handle=%s module=%s)", hb, mod_name);
        return false;
    }

    if (!m->save_blob_fn(am, persistent, a, out_blob))
    {
        LOG_ERROR("save_blob_fn returned false (type=%s handle=%s module=%s)", type_str, hb, mod_name);
        return false;
    }

    if (!out_blob->data)
    {
        LOG_ERROR("module returned NULL blob.data (type=%s handle=%s module=%s size=%u)",
                  type_str, hb, mod_name, (unsigned)out_blob->size);
        return false;
    }

    if (out_blob->size == 0)
    {
        LOG_ERROR("module returned blob.size=0 (type=%s handle=%s module=%s data=%p)",
                  type_str, hb, mod_name, out_blob->data);
        return false;
    }

    return true;
}

static void asset_free_blob(asset_manager_t *am, const asset_module_desc_t *m, asset_blob_t *blob)
{
    if (!blob)
        return;

    if (m && m->blob_free_fn)
        m->blob_free_fn(am, blob);
    else
        free(blob->data);

    memset(blob, 0, sizeof(*blob));
}

static bool asset_manager_build_pack_locked(asset_manager_t *am, uint8_t **out_data, uint32_t *out_size)
{
    *out_data = NULL;
    *out_size = 0;

    vector_t tocs = vector_impl_create_vector(sizeof(pack_toc_t));

    buf_t data;
    memset(&data, 0, sizeof(data));

    pack_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = 0x4B434150u;
    hdr.version = 1;

    bool ok = true;

    if (!buf_push_bytes(&data, &hdr, (uint32_t)sizeof(hdr)))
        ok = false;

    uint32_t slot_count = (uint32_t)am->slots.size;

    for (uint32_t i = 0; ok && i < slot_count; ++i)
    {
        asset_slot_t *s = (asset_slot_t *)vector_impl_at(&am->slots, i);
        if (!s)
            continue;

        if (s->asset.state != ASSET_STATE_READY)
            continue;

        if ((unsigned)s->asset.type >= (unsigned)ASSET_MAX)
            continue;

        if (!am->asset_type_has_save[s->asset.type])
        {
            char hb[64];
            ihandle_t persistent0 = s->persistent;
            if (!ihandle_is_valid(persistent0))
                persistent0 = make_persistent_handle(am, s->asset.type);
            handle_hex_triplet(hb, persistent0);
            LOG_ERROR("Asset has no save function registered: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            continue;
        }

        ihandle_t persistent = s->persistent;
        if (!ihandle_is_valid(persistent))
            persistent = make_persistent_handle(am, s->asset.type);

        uint32_t midx32 = asset_manager_find_first_save_module_index(am, s->asset.type);
        if (midx32 == 0xFFFFFFFFu)
        {
            char hb[64];
            handle_hex_triplet(hb, persistent);
            LOG_ERROR("Asset has no save module: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            continue;
        }

        const asset_module_desc_t *m = asset_manager_get_module_by_index(am, midx32);
        if (!m || !m->save_blob_fn || !m->blob_free_fn)
        {
            char hb[64];
            handle_hex_triplet(hb, persistent);
            LOG_ERROR("Asset save module invalid: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            continue;
        }

        asset_blob_t blob;
        memset(&blob, 0, sizeof(blob));

        if (!asset_save_blob(am, m, persistent, &s->asset, &blob))
        {
            char hb[64];
            handle_hex_triplet(hb, persistent);
            LOG_ERROR("Asset save failed: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            asset_free_blob(am, m, &blob);
            continue;
        }

        if (!buf_align(&data, blob.align ? blob.align : 64u))
        {
            ok = false;
            asset_free_blob(am, m, &blob);
            break;
        }

        uint32_t off = data.size;
        if (!buf_push_bytes(&data, blob.data, blob.size))
        {
            ok = false;
            asset_free_blob(am, m, &blob);
            break;
        }

        pack_toc_t e;
        memset(&e, 0, sizeof(e));
        e.key = ((uint64_t)((uint16_t)s->asset.type) << 48) | (uint64_t)persistent.value;
        e.type = (uint16_t)s->asset.type;
        e.variant = 0;
        e.flags = (uint32_t)blob.flags;
        e.offset = off;
        e.size = blob.size;
        e.uncompressed_size = blob.uncompressed_size;
        e.codec = blob.codec;

        vector_impl_push_back(&tocs, &e);

        asset_free_blob(am, m, &blob);
    }

    if (ok)
    {
        hdr.toc_count = (uint32_t)tocs.size;
        hdr.toc_offset = data.size;
        hdr.data_offset = (uint32_t)sizeof(pack_hdr_t);

        if (!buf_align(&data, 16))
            ok = false;
    }

    for (uint32_t i = 0; ok && i < tocs.size; ++i)
    {
        pack_toc_t *e = (pack_toc_t *)vector_impl_at(&tocs, i);
        if (!buf_push_bytes(&data, e, (uint32_t)sizeof(*e)))
            ok = false;
    }

    if (ok)
    {
        hdr.file_size = data.size;
        memcpy(data.p, &hdr, sizeof(hdr));
        *out_data = data.p;
        *out_size = data.size;
    }
    else
    {
        free(data.p);
        *out_data = NULL;
        *out_size = 0;
    }

    vector_impl_free(&tocs);
    return ok;
}

static bool asset_manager_save_separate_assets_locked(asset_manager_t *am, const char *base_path)
{
    const char *dir = (base_path && base_path[0]) ? base_path : ".";
    if (strcmp(dir, ".") != 0)
    {
        if (!ensure_directory(dir))
            return false;
    }

    uint32_t slot_count = (uint32_t)am->slots.size;

    for (uint32_t i = 0; i < slot_count; ++i)
    {
        asset_slot_t *s = (asset_slot_t *)vector_impl_at(&am->slots, i);
        if (!s)
            continue;

        if (s->asset.state != ASSET_STATE_READY)
            continue;

        if ((unsigned)s->asset.type >= (unsigned)ASSET_MAX)
            continue;

        if (!am->asset_type_has_save[s->asset.type])
        {
            ihandle_t persistent0 = s->persistent;
            if (!ihandle_is_valid(persistent0))
                persistent0 = make_persistent_handle(am, s->asset.type);
            char hb0[64];
            handle_hex_triplet_filesafe(hb0, persistent0);
            LOG_ERROR("Asset has no save function registered: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb0);
            continue;
        }

        ihandle_t persistent = s->persistent;
        if (!ihandle_is_valid(persistent))
            persistent = make_persistent_handle(am, s->asset.type);

        uint32_t midx32 = asset_manager_find_first_save_module_index(am, s->asset.type);
        if (midx32 == 0xFFFFFFFFu)
        {
            char hb[64];
            handle_hex_triplet_filesafe(hb, persistent);
            LOG_ERROR("Asset has no save module: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            continue;
        }

        const asset_module_desc_t *m = asset_manager_get_module_by_index(am, midx32);
        if (!m || !m->save_blob_fn || !m->blob_free_fn)
        {
            char hb[64];
            handle_hex_triplet_filesafe(hb, persistent);
            LOG_ERROR("Asset save module invalid: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            continue;
        }

        asset_blob_t blob;
        memset(&blob, 0, sizeof(blob));

        if (!asset_save_blob(am, m, persistent, &s->asset, &blob))
        {
            char hb[64];
            handle_hex_triplet_filesafe(hb, persistent);
            LOG_ERROR("Asset save failed: type=%s handle=%s", ASSET_TYPE_TO_STRING(s->asset.type), hb);
            asset_free_blob(am, m, &blob);
            continue;
        }

        char hb[64];
        handle_hex_triplet_filesafe(hb, persistent);

        char path[512];
#if defined(_WIN32)
        snprintf(path, sizeof(path), "%s\\%s_%s.iasset", dir, ASSET_TYPE_TO_STRING(s->asset.type), hb);
#else
        snprintf(path, sizeof(path), "%s/%s_%s.iasset", dir, ASSET_TYPE_TO_STRING(s->asset.type), hb);
#endif

        if (!write_file_all(path, blob.data, blob.size))
            LOG_ERROR("Failed to write asset file: %s", path);

        asset_free_blob(am, m, &blob);
    }

    return true;
}

bool asset_manager_build_pack(asset_manager_t *am, uint8_t **out_data, uint32_t *out_size)
{
    return asset_manager_build_pack_ex(am, out_data, out_size, SAVE_FLAG_NONE, NULL);
}

bool asset_manager_build_pack_ex(asset_manager_t *am, uint8_t **out_data, uint32_t *out_size, uint32_t flags, const char *base_path)
{
    if (!am || !out_data || !out_size)
        return false;

    *out_data = NULL;
    *out_size = 0;

    bool ok = false;

    mutex_lock_impl(&am->state_m);
    if (flags & SAVE_FLAG_SEPARATE_ASSETS)
        ok = asset_manager_save_separate_assets_locked(am, base_path);
    else
        ok = asset_manager_build_pack_locked(am, out_data, out_size);
    mutex_unlock_impl(&am->state_m);

    return ok;
}

void asset_manager_free_pack(uint8_t *data)
{
    free(data);
}

int all_loaded(asset_manager_t *am)
{
    int done = 1;

    mutex_lock_impl(&am->state_m);
    uint32_t n = (uint32_t)am->slots.size;
    for (uint32_t i = 0; i < n; i++)
    {
        asset_slot_t *s = (asset_slot_t *)vector_at(&am->slots, i);
        if (!s)
            continue;

        if (s->asset.state == ASSET_STATE_LOADING)
        {
            done = 0;
            break;
        }
    }
    mutex_unlock_impl(&am->state_m);

    return done;
}
