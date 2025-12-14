#pragma once
#include <math.h>
#include <float.h>
#include <stdbool.h>

typedef struct vec4
{
    union
    {
        struct
        {
            float x, y, z, w;
        };
        struct
        {
            float r, g, b, a;
        };
        float v[4];
    };
} vec4;

static inline vec4 vec4_make(float x, float y, float z, float w) { return (vec4){x, y, z, w}; }
static inline vec4 vec4_splat(float s) { return (vec4){s, s, s, s}; }
static inline vec4 vec4_zero(void) { return (vec4){0.f, 0.f, 0.f, 0.f}; }

static inline vec4 vec4_add(vec4 a, vec4 b) { return (vec4){a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
static inline vec4 vec4_sub(vec4 a, vec4 b) { return (vec4){a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
static inline vec4 vec4_mul(vec4 a, vec4 b) { return (vec4){a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
static inline vec4 vec4_div(vec4 a, vec4 b) { return (vec4){a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w}; }

static inline vec4 vec4_scale(vec4 a, float s) { return (vec4){a.x * s, a.y * s, a.z * s, a.w * s}; }

static inline float vec4_dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

static inline float vec4_len2(vec4 a) { return vec4_dot(a, a); }

static inline float vec4_len(vec4 a)
{
    float l2 = vec4_len2(a);
    return (l2 > 0.f) ? sqrtf(l2) : 0.f;
}

static inline vec4 vec4_norm(vec4 a)
{
    float l = vec4_len(a);
    if (l <= FLT_EPSILON)
        return vec4_zero();
    return vec4_scale(a, 1.0f / l);
}

static inline vec4 vec4_min(vec4 a, vec4 b)
{
    return (vec4){
        fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z), fminf(a.w, b.w)};
}

static inline vec4 vec4_max(vec4 a, vec4 b)
{
    return (vec4){
        fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z), fmaxf(a.w, b.w)};
}

static inline bool vec4_near(vec4 a, vec4 b, float eps)
{
    return (fabsf(a.x - b.x) <= eps) &&
           (fabsf(a.y - b.y) <= eps) &&
           (fabsf(a.z - b.z) <= eps) &&
           (fabsf(a.w - b.w) <= eps);
}

static inline vec4 vec4_div_safe(vec4 num, vec4 den, float eps, float fallback)
{
    return (vec4){
        (fabsf(den.x) > eps) ? (num.x / den.x) : fallback,
        (fabsf(den.y) > eps) ? (num.y / den.y) : fallback,
        (fabsf(den.z) > eps) ? (num.z / den.z) : fallback,
        (fabsf(den.w) > eps) ? (num.w / den.w) : fallback};
}
