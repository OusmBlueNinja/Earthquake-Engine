#include "shader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <GL/glew.h>

static int shader_uniform_location(shader_t *shader, const char *name)
{
    for (size_t i = 0; i < shader->uniform_count; i++)
        if (strcmp(shader->uniforms[i].name, name) == 0)
            return shader->uniforms[i].location;

    int loc = glGetUniformLocation(shader->program, name);
    if (loc == -1)
        return -1;

    if (shader->uniform_count == shader->uniform_capacity)
    {
        shader->uniform_capacity = shader->uniform_capacity ? shader->uniform_capacity * 2 : 8;
        shader->uniforms = realloc(shader->uniforms,
                                   shader->uniform_capacity * sizeof(uniform_cache_entry_t));
    }

    shader->uniforms[shader->uniform_count].name = strdup(name);
    shader->uniforms[shader->uniform_count].location = loc;
    shader->uniform_count++;
    return loc;
}

static GLuint compile_stage(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        glDeleteShader(s);
        return 0;
    }

    return s;
}

static shader_t *shader_link(GLuint *stages, size_t count)
{
    GLuint program = glCreateProgram();
    for (size_t i = 0; i < count; i++)
        glAttachShader(program, stages[i]);
    glLinkProgram(program);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        glDeleteProgram(program);
        return NULL;
    }

    for (size_t i = 0; i < count; i++)
        glDeleteShader(stages[i]);

    shader_t *s = calloc(1, sizeof(shader_t));
    s->program = program;
    return s;
}

char *shader_preprocess_glsl(const char *src,
                             const shader_define_t *defines,
                             size_t define_count)
{
    size_t cap = strlen(src) + 1;
    for (size_t i = 0; i < define_count; i++)
        cap += strlen(defines[i].key) + strlen(defines[i].value) + 16;

    char *out = malloc(cap);
    out[0] = 0;

    for (size_t i = 0; i < define_count; i++)
    {
        strcat(out, "#define ");
        strcat(out, defines[i].key);
        strcat(out, " ");
        strcat(out, defines[i].value);
        strcat(out, "\n");
    }

    strcat(out, src);
    return out;
}

static char *load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

shader_t *shader_create_from_source(const char *vs, const char *fs)
{
    GLuint stages[2];
    stages[0] = compile_stage(GL_VERTEX_SHADER, vs);
    stages[1] = compile_stage(GL_FRAGMENT_SHADER, fs);
    if (!stages[0] || !stages[1])
        return NULL;
    return shader_link(stages, 2);
}

shader_t *shader_create_from_source_with_defines(const char *vs,
                                                 const char *fs,
                                                 const shader_define_t *defs,
                                                 size_t n)
{
    char *v = shader_preprocess_glsl(vs, defs, n);
    char *f = shader_preprocess_glsl(fs, defs, n);
    shader_t *s = shader_create_from_source(v, f);
    free(v);
    free(f);
    return s;
}

shader_t *shader_create_from_files(const char *vp, const char *fp)
{
    char *v = load_file(vp);
    char *f = load_file(fp);
    if (!v || !f)
        return NULL;
    shader_t *s = shader_create_from_source(v, f);
    free(v);
    free(f);
    return s;
}

shader_t *shader_create_from_files_with_defines(const char *vp,
                                                const char *fp,
                                                const shader_define_t *d,
                                                size_t n)
{
    char *v = load_file(vp);
    char *f = load_file(fp);
    if (!v || !f)
        return NULL;
    shader_t *s = shader_create_from_source_with_defines(v, f, d, n);
    free(v);
    free(f);
    return s;
}

shader_t *shader_create_compute_from_source(const char *cs)
{
    GLuint s = compile_stage(GL_COMPUTE_SHADER, cs);
    if (!s)
        return NULL;
    return shader_link(&s, 1);
}

shader_t *shader_create_compute_from_source_with_defines(
    const char *cs,
    const shader_define_t *d,
    size_t n)
{
    char *c = shader_preprocess_glsl(cs, d, n);
    shader_t *s = shader_create_compute_from_source(c);
    free(c);
    return s;
}

shader_t *shader_create_compute_from_file(const char *cp)
{
    char *c = load_file(cp);
    if (!c)
        return NULL;
    shader_t *s = shader_create_compute_from_source(c);
    free(c);
    return s;
}

shader_t *shader_create_compute_from_file_with_defines(
    const char *cp,
    const shader_define_t *d,
    size_t n)
{
    char *c = load_file(cp);
    if (!c)
        return NULL;
    shader_t *s = shader_create_compute_from_source_with_defines(c, d, n);
    free(c);
    return s;
}

void shader_destroy(shader_t *s)
{
    if (!s)
        return;
    for (size_t i = 0; i < s->uniform_count; i++)
        free(s->uniforms[i].name);
    free(s->uniforms);
    glDeleteProgram(s->program);
    free(s);
}

void shader_bind(const shader_t *s)
{
    glUseProgram(s ? s->program : 0);
}

void shader_unbind(void)
{
    glUseProgram(0);
}

unsigned int shader_get_program(const shader_t *s)
{
    return s->program;
}

void shader_dispatch_compute(const shader_t *s,
                             unsigned int x,
                             unsigned int y,
                             unsigned int z)
{
    glUseProgram(s->program);
    glDispatchCompute(x, y, z);
}

void shader_memory_barrier(unsigned int bits)
{
    glMemoryBarrier(bits);
}

#define SET_UNIFORM(name, call)                                  \
    int loc = shader_uniform_location((shader_t *)shader, name); \
    if (loc < 0)                                                 \
        return false;                                            \
    call;                                                        \
    return true;

bool shader_set_int(const shader_t *shader, const char *name, int v)
{
    SET_UNIFORM(name, glUniform1i(loc, v))
}

bool shader_set_int_array(const shader_t *shader, const char *name, const int *v, int count)
{
    SET_UNIFORM(name, glUniform1iv(loc, count, v))
}

bool shader_set_uint(const shader_t *shader, const char *name, unsigned int v)
{
    SET_UNIFORM(name, glUniform1ui(loc, v))
}

bool shader_set_uint_array(const shader_t *shader, const char *name, const unsigned int *v, int count)
{
    SET_UNIFORM(name, glUniform1uiv(loc, count, v))
}

bool shader_set_float(const shader_t *shader, const char *name, float v)
{
    SET_UNIFORM(name, glUniform1fv(loc, 1, &v))
}

bool shader_set_float_array(const shader_t *shader, const char *name, const float *v, int count)
{
    SET_UNIFORM(name, glUniform1fv(loc, count, v))
}

bool shader_set_vec2(const shader_t *shader, const char *name, vec2 v)
{
    float data[2] = {v.x, v.y};
    SET_UNIFORM(name, glUniform2fv(loc, 1, data))
}

bool shader_set_vec2_array(const shader_t *shader, const char *name, const vec2 *v, int count)
{
    float *data = (float *)v;
    SET_UNIFORM(name, glUniform2fv(loc, count, data))
}

bool shader_set_vec3(const shader_t *shader, const char *name, vec3 v)
{
    float data[3] = {v.x, v.y, v.z};
    SET_UNIFORM(name, glUniform3fv(loc, 1, data))
}

bool shader_set_vec3_array(const shader_t *shader, const char *name, const vec3 *v, int count)
{
    float *data = (float *)v;
    SET_UNIFORM(name, glUniform3fv(loc, count, data))
}

bool shader_set_vec4(const shader_t *shader, const char *name, vec4 v)
{
    float data[4] = {v.x, v.y, v.z, v.w};
    SET_UNIFORM(name, glUniform4fv(loc, 1, data))
}

bool shader_set_vec4_array(const shader_t *shader, const char *name, const vec4 *v, int count)
{
    float *data = (float *)v;
    SET_UNIFORM(name, glUniform4fv(loc, count, data))
}

bool shader_set_ivec2(const shader_t *shader, const char *name, vec2i v)
{
    int data[2] = {v.x, v.y};
    SET_UNIFORM(name, glUniform2iv(loc, 1, data))
}

bool shader_set_ivec3(const shader_t *shader, const char *name, vec3i v)
{
    int data[3] = {v.x, v.y, v.z};
    SET_UNIFORM(name, glUniform3iv(loc, 1, data))
}

bool shader_set_ivec4(const shader_t *shader, const char *name, vec4i v)
{
    int data[4] = {v.x, v.y, v.z, v.w};
    SET_UNIFORM(name, glUniform4iv(loc, 1, data))
}

bool shader_set_uvec2(const shader_t *shader, const char *name, uvec2 v)
{
    unsigned int data[2] = {v.x, v.y};
    SET_UNIFORM(name, glUniform2uiv(loc, 1, data))
}

bool shader_set_uvec3(const shader_t *shader, const char *name, uvec3 v)
{
    unsigned int data[3] = {v.x, v.y, v.z};
    SET_UNIFORM(name, glUniform3uiv(loc, 1, data))
}

bool shader_set_uvec4(const shader_t *shader, const char *name, uvec4 v)
{
    unsigned int data[4] = {v.x, v.y, v.z, v.w};
    SET_UNIFORM(name, glUniform4uiv(loc, 1, data))
}

bool shader_set_mat4(const shader_t *shader, const char *name, mat4 m)
{
    SET_UNIFORM(name, glUniformMatrix4fv(loc, 1, GL_FALSE, m.m))
}
