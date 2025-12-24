// mat4.h
#pragma once

#include <math.h>
#include "vec3.h"

typedef struct
{
    float m[16];
} mat4;

static inline mat4 mat4_identity(void)
{
    mat4 r;
    r.m[0] = 1;
    r.m[1] = 0;
    r.m[2] = 0;
    r.m[3] = 0;
    r.m[4] = 0;
    r.m[5] = 1;
    r.m[6] = 0;
    r.m[7] = 0;
    r.m[8] = 0;
    r.m[9] = 0;
    r.m[10] = 1;
    r.m[11] = 0;
    r.m[12] = 0;
    r.m[13] = 0;
    r.m[14] = 0;
    r.m[15] = 1;
    return r;
}

mat4 mat4_mul(mat4 a, mat4 b);

static inline mat4 mat4_translate(vec3 v)
{
    mat4 r = mat4_identity();
    r.m[12] = v.x;
    r.m[13] = v.y;
    r.m[14] = v.z;
    return r;
}

static inline mat4 mat4_scale(vec3 v)
{
    mat4 r = mat4_identity();
    r.m[0] = v.x;
    r.m[5] = v.y;
    r.m[10] = v.z;
    return r;
}

static inline mat4 mat4_rotate_x(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

static inline mat4 mat4_rotate_y(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
}

static inline mat4 mat4_rotate_z(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

static inline mat4 mat4_perspective(float fov, float aspect, float zn, float zf)
{
    float t = tanf(fov * 0.5f);
    mat4 r = (mat4){0};
    r.m[0] = 1.0f / (aspect * t);
    r.m[5] = 1.0f / t;
    r.m[10] = -(zf + zn) / (zf - zn);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * zf * zn) / (zf - zn);
    return r;
}

static inline mat4 mat4_ortho(float l, float r0, float b, float t, float zn, float zf)
{
    mat4 r = mat4_identity();
    r.m[0] = 2.0f / (r0 - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (zf - zn);
    r.m[12] = -(r0 + l) / (r0 - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(zf + zn) / (zf - zn);
    return r;
}

static inline mat4 mat4_lookat(vec3 eye, vec3 at, vec3 up)
{
    vec3 f = vec3_norm((vec3){at.x - eye.x, at.y - eye.y, at.z - eye.z});
    vec3 s = vec3_norm(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);

    mat4 r = mat4_identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;

    r.m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    return r;
}

mat4 mat4_inverse(mat4 m);
