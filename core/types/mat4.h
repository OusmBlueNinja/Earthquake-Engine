// mat4.h
#pragma once

#include "vec3.h"

typedef struct
{
    float m[16];
} mat4;

mat4 mat4_identity(void);
mat4 mat4_translate(vec3 v);
mat4 mat4_scale(vec3 v);
mat4 mat4_rotate_x(float a);
mat4 mat4_rotate_y(float a);
mat4 mat4_rotate_z(float a);
mat4 mat4_perspective(float fov, float aspect, float zn, float zf);
mat4 mat4_ortho(float l, float r, float b, float t, float zn, float zf);
mat4 mat4_lookat(vec3 eye, vec3 at, vec3 up);

mat4 mat4_mul(mat4 a, mat4 b);
mat4 mat4_inverse(mat4 m);
