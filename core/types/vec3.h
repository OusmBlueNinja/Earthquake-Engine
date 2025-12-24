#pragma once
#include <math.h>

typedef struct
{
    float x;
    float y;
    float z;
} vec3;

static inline vec3 vec3_add(vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline vec3 vec3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline vec3 vec3_mul(vec3 a, vec3 b)
{
    vec3 r;
    r.x = a.x * b.x;
    r.y = a.y * b.y;
    r.z = a.z * b.z;
    return r;
}

static inline vec3 vec3_mul_f(vec3 a, float s) { return (vec3){a.x * s, a.y * s, a.z * s}; }
static inline float vec3_len(vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

static inline vec3 vec3_cross(vec3 a, vec3 b)
{
    vec3 r = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
    return r;
}

static inline vec3 vec3_norm(vec3 v)
{
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    vec3 r = {v.x / l, v.y / l, v.z / l};
    return r;
}

static inline float vec3_dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}