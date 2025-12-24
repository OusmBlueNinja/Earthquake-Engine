#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "vector.h"
#include "handle.h"

typedef struct model_vertex_t
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
} model_vertex_t;

typedef struct model_cpu_submesh_t
{
    model_vertex_t *vertices;
    uint32_t vertex_count;

    uint32_t *indices;
    uint32_t index_count;

    char *material_name;
    ihandle_t material;
} model_cpu_submesh_t;

typedef struct model_raw_t
{
    vector_t submeshes;
    char *mtllib_path;
    ihandle_t mtllib;
} model_raw_t;

typedef struct mesh_t
{
    uint32_t vao;
    uint32_t vbo;
    uint32_t ibo;
    uint32_t index_count;
    ihandle_t material;
} mesh_t;

typedef struct asset_model_t
{
    vector_t meshes;
} asset_model_t;

model_raw_t model_raw_make(void);
void model_raw_destroy(model_raw_t *r);

asset_model_t asset_model_make(void);
void asset_model_destroy_cpu_only(asset_model_t *m);
