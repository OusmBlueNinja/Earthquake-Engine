#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "asset_manager/asset_types/material.h"

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
    ihandle_t material;
} model_t;

typedef enum primitive_type_t
{
    PRIM_NONE = 0,
    PRIM_CUBE = 1,
    PRIM_SPHERE = 2
} primitive_type_t;

typedef struct model_factory_t
{
    mesh_t *cube;
    mesh_t *sphere;
} model_factory_t;

bool model_factory_init(model_factory_t *mf);
void model_factory_shutdown(model_factory_t *mf);

mesh_t *model_factory_get_mesh(model_factory_t *mf, primitive_type_t prim);

model_t model_make(mesh_t *mesh, ihandle_t material);
model_t model_make_primitive(model_factory_t *mf, primitive_type_t prim, ihandle_t material);

void model_draw(const model_t *model);
