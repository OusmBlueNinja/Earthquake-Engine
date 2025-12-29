#pragma once
#include <math.h>
#include <stdint.h>

typedef struct
{
    float x;
    float y;
    float z;
} vec3;

vec3 vec3_make(float x, float y, float z);
vec3 vec3_splat(float s);

vec3 vec3_add(vec3 a, vec3 b);
vec3 vec3_sub(vec3 a, vec3 b);
vec3 vec3_mul(vec3 a, vec3 b);
vec3 vec3_div(vec3 a, vec3 b);

vec3 vec3_mul_f(vec3 a, float s);
vec3 vec3_div_f(vec3 a, float s);

vec3 vec3_neg(vec3 v);
vec3 vec3_abs(vec3 v);

float vec3_dot(vec3 a, vec3 b);
vec3 vec3_cross(vec3 a, vec3 b);

float vec3_len(vec3 v);
float vec3_len_sq(vec3 v);

vec3 vec3_norm(vec3 v);
vec3 vec3_norm_safe(vec3 v, float eps);

float vec3_dist(vec3 a, vec3 b);
float vec3_dist_sq(vec3 a, vec3 b);

vec3 vec3_min(vec3 a, vec3 b);
vec3 vec3_max(vec3 a, vec3 b);
vec3 vec3_clamp(vec3 v, vec3 lo, vec3 hi);

vec3 vec3_lerp(vec3 a, vec3 b, float t);

vec3 vec3_reflect(vec3 v, vec3 n);
vec3 vec3_project(vec3 v, vec3 onto);
vec3 vec3_reject(vec3 v, vec3 onto);

int vec3_near_equal(vec3 a, vec3 b, float eps);
