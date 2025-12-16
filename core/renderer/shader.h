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

typedef struct uniform_cache_entry_t
{
    char *name;
    int location;
} uniform_cache_entry_t;

typedef struct shader_t
{
    unsigned char shader_id;
    unsigned int program;
    uniform_cache_entry_t *uniforms;
    size_t uniform_count;
    size_t uniform_capacity;
} shader_t;

typedef struct shader_define_t
{
    const char *key;
    const char *value;
} shader_define_t;

shader_t *shader_create_from_source(const char *vertex_src,
                                    const char *fragment_src);

shader_t *shader_create_from_source_with_defines(const char *vertex_src,
                                                 const char *fragment_src,
                                                 const shader_define_t *defines,
                                                 size_t define_count);

shader_t *shader_create_from_files(const char *vertex_path,
                                   const char *fragment_path);

shader_t *shader_create_from_files_with_defines(const char *vertex_path,
                                                const char *fragment_path,
                                                const shader_define_t *defines,
                                                size_t define_count);

shader_t *shader_create_compute_from_source(const char *compute_src);

shader_t *shader_create_compute_from_source_with_defines(
    const char *compute_src,
    const shader_define_t *defines,
    size_t define_count);

shader_t *shader_create_compute_from_file(const char *compute_path);

shader_t *shader_create_compute_from_file_with_defines(
    const char *compute_path,
    const shader_define_t *defines,
    size_t define_count);

void shader_destroy(shader_t *shader);
void shader_bind(const shader_t *shader);
void shader_unbind(void);
unsigned int shader_get_program(const shader_t *shader);

void shader_dispatch_compute(const shader_t *shader,
                             unsigned int groups_x,
                             unsigned int groups_y,
                             unsigned int groups_z);

void shader_memory_barrier(unsigned int barrier_bits);

bool shader_set_int(const shader_t *shader,
                    const char *name,
                    int value);

bool shader_set_int_array(const shader_t *shader,
                          const char *name,
                          const int *values,
                          int count);

bool shader_set_uint(const shader_t *shader,
                     const char *name,
                     unsigned int value);

bool shader_set_uint_array(const shader_t *shader,
                           const char *name,
                           const unsigned int *values,
                           int count);

bool shader_set_float(const shader_t *shader,
                      const char *name,
                      float value);

bool shader_set_float_array(const shader_t *shader,
                            const char *name,
                            const float *values,
                            int count);

bool shader_set_vec2(const shader_t *shader,
                     const char *name,
                     vec2 value);

bool shader_set_vec2_array(const shader_t *shader,
                           const char *name,
                           const vec2 *values,
                           int count);

bool shader_set_vec3(const shader_t *shader,
                     const char *name,
                     vec3 value);

bool shader_set_vec3_array(const shader_t *shader,
                           const char *name,
                           const vec3 *values,
                           int count);

bool shader_set_vec4(const shader_t *shader,
                     const char *name,
                     vec4 value);

bool shader_set_vec4_array(const shader_t *shader,
                           const char *name,
                           const vec4 *values,
                           int count);

bool shader_set_ivec2(const shader_t *shader,
                      const char *name,
                      vec2i value);

bool shader_set_ivec3(const shader_t *shader,
                      const char *name,
                      vec3i value);

bool shader_set_ivec4(const shader_t *shader,
                      const char *name,
                      vec4i value);

bool shader_set_uvec2(const shader_t *shader,
                      const char *name,
                      uvec2 value);

bool shader_set_uvec3(const shader_t *shader,
                      const char *name,
                      uvec3 value);

bool shader_set_uvec4(const shader_t *shader,
                      const char *name,
                      uvec4 value);

bool shader_set_mat4(const shader_t *shader,
                     const char *name,
                     mat4 value);

char *shader_preprocess_glsl(const char *src,
                             const shader_define_t *defines,
                             size_t define_count);

#endif
