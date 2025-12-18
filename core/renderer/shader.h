#ifndef LITEENGINE_SHADER_H
#define LITEENGINE_SHADER_H

#include <stdbool.h>
#include <stddef.h>

#include "types/vec2.h"
#include "types/vec2i.h"
#include "types/vec3.h"
#include "types/vec3i.h"
#include "types/vec4.h"
#include "types/vec4i.h"
#include "types/uvec2.h"
#include "types/uvec3.h"
#include "types/uvec4.h"
#include "types/mat4.h"

typedef enum shader_stage_t
{
    SHADER_STAGE_VERTEX = 0,
    SHADER_STAGE_FRAGMENT,
    SHADER_STAGE_COMPUTE
} shader_stage_t;

typedef struct shader_define_kv_t
{
    char *key;
    char *value;
} shader_define_kv_t;

typedef struct shader_uniform_cache_entry_t
{
    char *name;
    int location;
} shader_uniform_cache_entry_t;

typedef struct shader_t
{
    unsigned int program;
    shader_stage_t kind;

    shader_define_kv_t *defines;
    size_t define_count;
    size_t define_capacity;

    shader_uniform_cache_entry_t *uniforms;
    size_t uniform_count;
    size_t uniform_capacity;

    bool linked;
} shader_t;

#define create_shader() shader_create()
#define shader_delete(shader_ptr) shader_destroy((shader_ptr))

shader_t shader_create(void);
void shader_destroy(shader_t *shader);

bool shader_define(shader_t *shader, const char *key, const char *value);
const char *shader_get_define(const shader_t *shader, const char *key);
bool shader_undefine(shader_t *shader, const char *key);
void shader_clear_defines(shader_t *shader);

bool shader_load_from_source(shader_t *shader, const char *vertex_src, const char *fragment_src);
bool shader_load_from_files(shader_t *shader, const char *vertex_path, const char *fragment_path);

bool shader_load_compute_from_source(shader_t *shader, const char *compute_src);
bool shader_load_compute_from_file(shader_t *shader, const char *compute_path);

void shader_bind(const shader_t *shader);
void shader_unbind(void);
unsigned int shader_get_program(const shader_t *shader);

void shader_dispatch_compute(const shader_t *shader,
                             unsigned int groups_x,
                             unsigned int groups_y,
                             unsigned int groups_z);

void shader_memory_barrier(unsigned int barrier_bits);

bool shader_set_int(const shader_t *shader, const char *name, int value);
bool shader_set_int_array(const shader_t *shader, const char *name, const int *values, int count);

bool shader_set_uint(const shader_t *shader, const char *name, unsigned int value);
bool shader_set_uint_array(const shader_t *shader, const char *name, const unsigned int *values, int count);

bool shader_set_float(const shader_t *shader, const char *name, float value);
bool shader_set_float_array(const shader_t *shader, const char *name, const float *values, int count);

bool shader_set_vec2(const shader_t *shader, const char *name, vec2 value);
bool shader_set_vec2_array(const shader_t *shader, const char *name, const vec2 *values, int count);

bool shader_set_vec3(const shader_t *shader, const char *name, vec3 value);
bool shader_set_vec3_array(const shader_t *shader, const char *name, const vec3 *values, int count);

bool shader_set_vec4(const shader_t *shader, const char *name, vec4 value);
bool shader_set_vec4_array(const shader_t *shader, const char *name, const vec4 *values, int count);

bool shader_set_ivec2(const shader_t *shader, const char *name, vec2i value);
bool shader_set_ivec3(const shader_t *shader, const char *name, vec3i value);
bool shader_set_ivec4(const shader_t *shader, const char *name, vec4i value);

bool shader_set_uvec2(const shader_t *shader, const char *name, uvec2 value);
bool shader_set_uvec3(const shader_t *shader, const char *name, uvec3 value);
bool shader_set_uvec4(const shader_t *shader, const char *name, uvec4 value);

bool shader_set_mat4(const shader_t *shader, const char *name, mat4 value);

char *shader_preprocess_glsl(const char *src, const shader_t *shader);

#endif
