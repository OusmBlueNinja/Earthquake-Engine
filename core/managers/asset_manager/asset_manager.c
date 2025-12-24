#include "asset_manager.h"
#include "loaders/register_modules.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
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

static void slot_cleanup(asset_manager_t *am, asset_slot_t *s)
{
    if (!s)
        return;
    asset_cleanup_by_module(am, &s->asset, s->module_index);
    s->module_index = 0xFFFFu;
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

static bool asset_try_load_any(asset_manager_t *am, asset_type_t type, const char *path, uint32_t path_is_ptr, asset_any_t *out_asset, uint16_t *out_module_index)
{
    if (!am || !out_asset || !out_module_index)
        return false;

    if (!path)
        return false;

    if (!path_is_ptr && (!path[0]))
        return false;

    *out_module_index = 0xFFFFu;
    asset_zero(out_asset);

    for (uint32_t i = 0; i < am->modules.size; ++i)
    {
        const asset_module_desc_t *m = (const asset_module_desc_t *)vector_impl_at(&am->modules, i);
        if (!m || m->type != type || !m->load_fn)
            continue;

        asset_any_t tmp;
        asset_zero(&tmp);

        if (m->load_fn(am, path, path_is_ptr, &tmp))
        {
            *out_asset = tmp;
            *out_module_index = (uint16_t)i;
            return true;
        }
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
        d.ok = false;
        d.module_index = 0xFFFFu;
        asset_zero(&d.asset);

        asset_any_t out;
        uint16_t midx = 0xFFFFu;

        bool ok = asset_try_load_any(am, j.type, j.path, j.path_is_ptr, &out, &midx);
        d.ok = ok;
        d.module_index = midx;

        if (ok)
        {
            d.asset = out;
        }
        else
        {
            if (j.path_is_ptr)
                LOG_ERROR("Failed to load asset: [%s](%p)", asset_type_name(am, j.type), (void *)j.path);
            else
                LOG_ERROR("Failed to load asset: [%s](%s)", asset_type_name(am, j.type), j.path);
        }

        if (!j.path_is_ptr)
            free(j.path);
        doneq_push(&am->done, &d);
    }

    free(ctx);
}

bool asset_manager_register_module(asset_manager_t *am, asset_module_desc_t module)
{
    if (module.type == ASSET_NONE)
        return false;
    if (!module.load_fn && !module.init_fn && !module.cleanup_fn && !module.save_blob_fn)
        return false;

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
    asset_zero(&s.asset);
    s.asset.type = type;
    s.asset.state = ASSET_STATE_LOADING;

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

    uint32_t wc = 4;
    uint32_t cap = 1024;
    ihandle_type_t ht = 1;

    if (desc)
    {
        if (desc->worker_count)
            wc = desc->worker_count;
        if (desc->max_inflight_jobs)
            cap = desc->max_inflight_jobs;
        if (desc->handle_type)
            ht = desc->handle_type;
    }

    am->handle_type = ht;

    am->slots = vector_impl_create_vector(sizeof(asset_slot_t));
    am->modules = vector_impl_create_vector(sizeof(asset_module_desc_t));

    jobq_init(&am->jobs, cap);
    doneq_init(&am->done, cap);

    mutex_init_impl(&am->state_m);
    am->shutting_down = 0;

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
        slot_cleanup(am, s);
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

    mutex_lock_impl(&am->state_m);
    uint32_t sd = am->shutting_down;
    if (sd)
    {
        mutex_unlock_impl(&am->state_m);
        return ihandle_invalid();
    }

    ihandle_t h;
    alloc_slot_locked(am, type, &h);
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
    alloc_slot_locked(am, type, &h);
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
        slot_cleanup(am, slot);
        slot->asset.state = ASSET_STATE_FAILED;
        slot->module_index = 0xFFFFu;
        mutex_unlock_impl(&am->state_m);

        return ihandle_invalid();
    }

    mutex_lock_impl(&am->state_m);
    slot_cleanup(am, slot);
    a.state = ASSET_STATE_READY;
    slot->asset = a;
    slot->module_index = (uint16_t)midx32;
    mutex_unlock_impl(&am->state_m);

    return h;
}

void asset_manager_pump(asset_manager_t *am)
{
    if (!am)
        return;

    asset_done_t d;
    while (doneq_pop(&am->done, &d))
    {
        mutex_lock_impl(&am->state_m);
        asset_slot_t *slot = NULL;
        bool ok_slot = slot_valid_locked(am, d.handle, &slot);
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
                slot_cleanup(am, slot);
                asset_zero(&slot->asset);
                slot->asset.state = ASSET_STATE_FAILED;
                slot->module_index = 0xFFFFu;
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
                slot_cleanup(am, slot);
                asset_zero(&slot->asset);
                slot->asset.state = ASSET_STATE_FAILED;
                slot->module_index = 0xFFFFu;
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
            old = slot->asset;
            slot_cleanup(am, slot);
            d.asset.state = ASSET_STATE_READY;
            slot->asset = d.asset;
            slot->module_index = d.module_index;
            asset_zero(&d.asset);
        }
        mutex_unlock_impl(&am->state_m);

        asset_cleanup_by_module(am, &old, 0xFFFFu);
    }
}

const asset_any_t *asset_manager_get_any(const asset_manager_t *am, ihandle_t h)
{
    if (!am)
        return NULL;

    asset_manager_t *am_mut = (asset_manager_t *)am;

    mutex_lock_impl(&am_mut->state_m);
    asset_slot_t *slot = NULL;
    bool ok = slot_valid_locked(am_mut, h, &slot);
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

static void handle_hex_triplet(char out[64], ihandle_t h)
{
    snprintf(out, 64, "%08X:%04X:%04X", (unsigned)h.value, (unsigned)h.type, (unsigned)h.meta);
}

bool asset_manager_build_pack(asset_manager_t *am, uint8_t **out_data, uint32_t *out_size)
{
    if (!am || !out_data || !out_size)
        return false;

    *out_data = NULL;
    *out_size = 0;

    vector_t tocs = vector_impl_create_vector(sizeof(pack_toc_t));

    buf_t data;
    memset(&data, 0, sizeof(data));

    pack_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = 0x4B434150u;
    hdr.version = 1;

    if (!buf_push_bytes(&data, &hdr, (uint32_t)sizeof(hdr)))
        goto fail;

    mutex_lock_impl(&am->state_m);
    uint32_t slot_count = (uint32_t)am->slots.size;
    for (uint32_t i = 0; i < slot_count; ++i)
    {
        asset_slot_t *s = (asset_slot_t *)vector_impl_at(&am->slots, i);
        if (!s)
            continue;

        if (s->asset.state != ASSET_STATE_READY)
            continue;

        ihandle_t h = ihandle_make(am->handle_type, (uint16_t)(i + 1), s->generation);

        uint32_t midx32 = asset_manager_find_first_module_index(am, s->asset.type);
        if (midx32 == 0xFFFFFFFFu)
        {
            char hb[64];
            handle_hex_triplet(hb, h);
            LOG_ERROR("Asset has no module for save: type=%s handle=%s", asset_type_name(am, s->asset.type), hb);
            continue;
        }

        const asset_module_desc_t *m = asset_manager_get_module_by_index(am, midx32);
        if (!m || !m->save_blob_fn)
        {
            char hb[64];
            handle_hex_triplet(hb, h);
            LOG_ERROR("Asset has no save thingamabob: type=%s handle=%s", asset_type_name(am, s->asset.type), hb);
            continue;
        }

        asset_blob_t blob;
        memset(&blob, 0, sizeof(blob));
        blob.align = 64;

        if (!m->save_blob_fn(am, h, &s->asset, &blob))
        {
            char hb[64];
            handle_hex_triplet(hb, h);
            LOG_ERROR("Asset save thingamabob failed: type=%s handle=%s", asset_type_name(am, s->asset.type), hb);
            if (m->blob_free_fn)
                m->blob_free_fn(am, &blob);
            else
                free(blob.data);
            continue;
        }

        if (!blob.data || blob.size == 0)
        {
            char hb[64];
            handle_hex_triplet(hb, h);
            LOG_ERROR("Asset save thingamabob returned empty blob: type=%s handle=%s", asset_type_name(am, s->asset.type), hb);
            if (m->blob_free_fn)
                m->blob_free_fn(am, &blob);
            else
                free(blob.data);
            continue;
        }

        if (!buf_align(&data, blob.align ? blob.align : 64u))
        {
            if (m->blob_free_fn)
                m->blob_free_fn(am, &blob);
            else
                free(blob.data);
            goto fail_locked;
        }

        uint32_t off = data.size;
        if (!buf_push_bytes(&data, blob.data, blob.size))
        {
            if (m->blob_free_fn)
                m->blob_free_fn(am, &blob);
            else
                free(blob.data);
            goto fail_locked;
        }

        pack_toc_t e;
        memset(&e, 0, sizeof(e));
        e.key = ((uint64_t)((uint16_t)s->asset.type) << 48) | (uint64_t)h.value;
        e.type = (uint16_t)s->asset.type;
        e.variant = 0;
        e.flags = (uint32_t)blob.flags;
        e.offset = off;
        e.size = blob.size;
        e.uncompressed_size = blob.uncompressed_size;
        e.codec = blob.codec;

        vector_impl_push_back(&tocs, &e);

        if (m->blob_free_fn)
            m->blob_free_fn(am, &blob);
        else
            free(blob.data);
    }
    mutex_unlock_impl(&am->state_m);

    hdr.toc_count = (uint32_t)tocs.size;
    hdr.toc_offset = data.size;
    hdr.data_offset = (uint32_t)sizeof(pack_hdr_t);

    if (!buf_align(&data, 16))
        goto fail;

    for (uint32_t i = 0; i < tocs.size; ++i)
    {
        pack_toc_t *e = (pack_toc_t *)vector_impl_at(&tocs, i);
        if (!buf_push_bytes(&data, e, (uint32_t)sizeof(*e)))
            goto fail;
    }

    hdr.file_size = data.size;
    memcpy(data.p, &hdr, sizeof(hdr));

    vector_impl_free(&tocs);

    *out_data = data.p;
    *out_size = data.size;
    return true;

fail_locked:
    mutex_unlock_impl(&am->state_m);
fail:
    vector_impl_free(&tocs);
    free(data.p);
    *out_data = NULL;
    *out_size = 0;
    return false;
}

void asset_manager_free_pack(uint8_t *data)
{
    free(data);
}
