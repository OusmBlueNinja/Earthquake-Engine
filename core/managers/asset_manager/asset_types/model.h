#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "vector.h"
#include "handle.h"
#include "types/vec3.h"

typedef struct model_vertex_t
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
} model_vertex_t;

typedef struct model_cpu_lod_t
{
    model_vertex_t *vertices;
    uint32_t vertex_count;

    uint32_t *indices;
    uint32_t index_count;
} model_cpu_lod_t;

typedef struct model_cpu_submesh_t
{
    vector_t lods;

    char *material_name;
    ihandle_t material;
} model_cpu_submesh_t;

typedef struct model_raw_t
{
    vector_t submeshes;
    char *mtllib_path;
    ihandle_t mtllib;
    uint8_t lod_count;
} model_raw_t;

typedef struct aabb_t
{
    vec3 min;
    vec3 max;
} aabb_t;

typedef struct mesh_lod_t
{
    uint32_t vao;
    uint32_t vbo;
    uint32_t ibo;
    uint32_t index_count;
} mesh_lod_t;

typedef struct mesh_t
{
    vector_t lods;
    ihandle_t material;

    aabb_t local_aabb;
    uint8_t has_aabb;
} mesh_t;

typedef struct asset_model_t
{
    vector_t meshes;
} asset_model_t;

model_raw_t model_raw_make(void);
void model_raw_destroy(model_raw_t *r);

asset_model_t asset_model_make(void);
void asset_model_destroy_cpu_only(asset_model_t *m);

aabb_t model_cpu_submesh_compute_aabb(const model_cpu_submesh_t *sm);
void mesh_set_local_aabb_from_cpu(mesh_t *dst, const model_cpu_submesh_t *src);
