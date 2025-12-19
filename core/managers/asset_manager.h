#include <stdint.h>
#include <string.h>
#include "vector.h"

typedef uint32_t asset_id_t;

typedef enum asset_type_t
{
    ASSET_NONE = 0,
    ASSET_TEXTURE = 1,
    ASSET_MESH = 2,
    ASSET_MATERIAL = 3
} asset_type_t;

typedef struct texture_asset_t
{
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t gl_handle;
} texture_asset_t;

typedef struct mesh_asset_t
{
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vbo;
    uint32_t ibo;
    uint32_t vao;
} mesh_asset_t;

typedef struct material_asset_t
{
    asset_id_t albedo_tex;
    asset_id_t normal_tex;
    float roughness;
    float metallic;
} material_asset_t;

typedef union asset_data_u
{
    texture_asset_t texture;
    mesh_asset_t mesh;
    material_asset_t material;
} asset_data_u;

typedef struct asset_t
{
    asset_id_t id;
    uint8_t type;
    uint8_t flags;
    uint16_t pad;
    asset_data_u data;
} asset_t;

typedef struct asset_manager_t
{
    vector_t assets;
} asset_manager_t;
