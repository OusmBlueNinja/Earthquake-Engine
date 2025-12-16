#pragma once
#include "types/vec3.h"
#include "types/mat4.h"

typedef struct camera_t
{
    mat4 view;
    mat4 proj;
    vec3 position;
} camera_t;
