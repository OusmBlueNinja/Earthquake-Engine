#pragma once
#include "types/vec3.h"

typedef enum
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT,
} light_type_t;

typedef struct light_t
{
    light_type_t type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
} light_t;
