#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void *(*ui_realloc_fn)(void *user, void *ptr, uint32_t size);

typedef struct ui_vec2_t { float x, y; } ui_vec2_t;
typedef struct ui_vec3_t { float x, y, z; } ui_vec3_t;
typedef struct ui_vec4_t { float x, y, z, w; } ui_vec4_t;
typedef struct ui_vec2i_t { int32_t x, y; } ui_vec2i_t;
typedef struct ui_color_t
{
    ui_vec3_t rgb;
    float a;
} ui_color_t;

static inline ui_vec2_t ui_v2(float x, float y) { ui_vec2_t v; v.x = x; v.y = y; return v; }
static inline ui_vec3_t ui_v3(float x, float y, float z) { ui_vec3_t v; v.x = x; v.y = y; v.z = z; return v; }
static inline ui_vec4_t ui_v4(float x, float y, float z, float w) { ui_vec4_t v; v.x = x; v.y = y; v.z = z; v.w = w; return v; }
static inline ui_vec2i_t ui_v2i(int32_t x, int32_t y) { ui_vec2i_t v; v.x = x; v.y = y; return v; }

static inline float ui_maxf(float a, float b) { return a > b ? a : b; }
static inline float ui_minf(float a, float b) { return a < b ? a : b; }

static inline int ui_pt_in_rect(ui_vec2_t p, ui_vec4_t r)
{
    return p.x >= r.x && p.y >= r.y && p.x < (r.x + r.z) && p.y < (r.y + r.w);
}

static inline ui_vec2_t ui_rect_center(ui_vec4_t r)
{
    return ui_v2(r.x + r.z * 0.5f, r.y + r.w * 0.5f);
}

typedef struct ui_array_t
{
    void *data;
    uint32_t count;
    uint32_t cap;
    uint32_t stride;
    ui_realloc_fn rfn;
    void *ruser;
} ui_array_t;

static inline void ui_array_init(ui_array_t *a, uint32_t stride, ui_realloc_fn rfn, void *ruser)
{
    a->data = 0;
    a->count = 0;
    a->cap = 0;
    a->stride = stride;
    a->rfn = rfn;
    a->ruser = ruser;
}

static inline void ui_array_free(ui_array_t *a)
{
    if (a->rfn && a->data) a->rfn(a->ruser, a->data, 0);
    a->data = 0;
    a->count = 0;
    a->cap = 0;
}

static inline void ui_array_clear(ui_array_t *a)
{
    a->count = 0;
}

static inline int ui_array_reserve(ui_array_t *a, uint32_t cap)
{
    if (cap <= a->cap) return 1;
    uint32_t new_cap = a->cap ? a->cap : 1;
    while (new_cap < cap) new_cap *= 2;
    if (!a->rfn) return 0;
    void *p = a->rfn(a->ruser, a->data, new_cap * a->stride);
    if (!p) return 0;
    a->data = p;
    a->cap = new_cap;
    return 1;
}

static inline void *ui_array_push(ui_array_t *a)
{
    if (a->count + 1 > a->cap)
        if (!ui_array_reserve(a, a->count + 1)) return 0;
    uint8_t *p = (uint8_t *)a->data + (size_t)a->count * a->stride;
    a->count++;
    return p;
}

static inline void *ui_array_at(ui_array_t *a, uint32_t i)
{
    return (uint8_t *)a->data + (size_t)i * a->stride;
}

static inline void *ui_array_back(ui_array_t *a)
{
    return a->count ? ui_array_at(a, a->count - 1) : 0;
}

static inline const void *ui_array_data(const ui_array_t *a) { return a->data; }
static inline uint32_t ui_array_count(const ui_array_t *a) { return a->count; }

typedef struct ui_string_arena_t
{
    ui_array_t bytes;
} ui_string_arena_t;

static inline void ui_str_arena_init(ui_string_arena_t *s, ui_realloc_fn rfn, void *ruser)
{
    ui_array_init(&s->bytes, 1, rfn, ruser);
    char *z = (char *)ui_array_push(&s->bytes);
    if (z) *z = 0;
}

static inline void ui_str_arena_free(ui_string_arena_t *s)
{
    ui_array_free(&s->bytes);
}

static inline void ui_str_arena_reset(ui_string_arena_t *s)
{
    ui_array_clear(&s->bytes);
    char *z = (char *)ui_array_push(&s->bytes);
    if (z) *z = 0;
}
