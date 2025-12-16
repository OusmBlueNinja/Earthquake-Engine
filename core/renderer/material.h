#pragma once
#include <stdint.h>
#include "shader.h"

typedef struct material_t
{
    shader_t *shader;
    uint32_t flags;
} material_t;

void material_bind(const material_t *mat);
