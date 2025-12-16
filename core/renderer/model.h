#pragma once
#include <stdint.h>
#include "material.h"

typedef struct mesh_t
{
    uint32_t vao;
    uint32_t vbo;
    uint32_t ibo;
    uint32_t index_count;
} mesh_t;

typedef struct model_t
{
    mesh_t *mesh;
    material_t *material;
} model_t;

void model_draw(const model_t *model);
