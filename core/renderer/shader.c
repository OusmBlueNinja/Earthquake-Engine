#include "shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils/logger.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/glew.h>

static char *sh_strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    if (!r)
        return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static void *sh_realloc_array(void *ptr, size_t elem, size_t count)
{
    if (elem == 0 || count == 0)
    {
        free(ptr);
        return NULL;
    }
    size_t bytes = elem * count;
    return realloc(ptr, bytes);
}

static int sh_define_index(const shader_t *shader, const char *key)
{
    if (!shader || !key)
        return -1;
    for (size_t i = 0; i < shader->define_count; ++i)
        if (shader->defines[i].key && strcmp(shader->defines[i].key, key) == 0)
            return (int)i;
    return -1;
}

static void sh_uniform_cache_clear(shader_t *shader)
{
    if (!shader)
        return;
    for (size_t i = 0; i < shader->uniform_count; ++i)
        free(shader->uniforms[i].name);
    free(shader->uniforms);
    shader->uniforms = NULL;
    shader->uniform_count = 0;
    shader->uniform_capacity = 0;
}

static int sh_uniform_cache_find(const shader_t *shader, const char *name)
{
    for (size_t i = 0; i < shader->uniform_count; ++i)
        if (shader->uniforms[i].name && strcmp(shader->uniforms[i].name, name) == 0)
            return (int)i;
    return -1;
}

static bool sh_uniform_cache_push(shader_t *shader, const char *name, int location)
{
    if (shader->uniform_count + 1 > shader->uniform_capacity)
    {
        size_t nc = shader->uniform_capacity ? shader->uniform_capacity * 2 : 32;
        void *p = sh_realloc_array(shader->uniforms, sizeof(shader_uniform_cache_entry_t), nc);
        if (!p)
            return false;
        shader->uniforms = (shader_uniform_cache_entry_t *)p;
        shader->uniform_capacity = nc;
    }

    shader->uniforms[shader->uniform_count].name = sh_strdup(name);
    shader->uniforms[shader->uniform_count].location = location;
    shader->uniform_count++;
    return true;
}

static int sh_get_uniform_location_cached(shader_t *shader, const char *name)
{
    if (!shader || !shader->linked || shader->program == 0 || !name)
        return -1;

    int idx = sh_uniform_cache_find(shader, name);
    if (idx >= 0)
        return shader->uniforms[idx].location;

    int loc = glGetUniformLocation(shader->program, name);
    sh_uniform_cache_push(shader, name, loc);
    return loc;
}

static char *sh_read_text_file(const char *path)
{
    if (!path)
    {
        LOG_ERROR("path is NULL");
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LOG_ERROR("fopen failed: %s", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        LOG_ERROR("fseek(SEEK_END) failed: %s", path);
        fclose(f);
        return NULL;
    }

    long len = ftell(f);
    if (len < 0)
    {
        LOG_ERROR("ftell failed: %s", path);
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0)
    {
        LOG_ERROR("fseek(SEEK_SET) failed: %s", path);
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf)
    {
        LOG_ERROR("malloc failed (%ld bytes): %s", len, path);
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)len, f);
    if (rd != (size_t)len)
    {
        LOG_ERROR("fread short read (%zu/%ld): %s", rd, len, path);
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    buf[rd] = 0;
    return buf;
}


static void sh_print_shader_log(GLuint shader, const char *stage, const char *label)
{
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1)
        return;

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf)
        return;

    glGetShaderInfoLog(shader, len, NULL, buf);
    buf[len] = 0;

    if (stage && label)
        fprintf(stderr, "[GLSL] %s compile error (%s):\n%s\n", stage, label, buf);
    else if (stage)
        fprintf(stderr, "[GLSL] %s compile error:\n%s\n", stage, buf);
    else
        fprintf(stderr, "[GLSL] compile error:\n%s\n", buf);

    free(buf);
}

static void sh_print_program_log(GLuint program, const char *label)
{
    GLint len = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1)
        return;

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf)
        return;

    glGetProgramInfoLog(program, len, NULL, buf);
    buf[len] = 0;

    if (label)
        fprintf(stderr, "[GLSL] link error (%s):\n%s\n", label, buf);
    else
        fprintf(stderr, "[GLSL] link error:\n%s\n", buf);

    free(buf);
}

static const char *sh_stage_name(GLenum type)
{
    if (type == GL_VERTEX_SHADER)
        return "vertex";
    if (type == GL_FRAGMENT_SHADER)
        return "fragment";
    if (type == GL_GEOMETRY_SHADER)
        return "geometry";
    if (type == GL_COMPUTE_SHADER)
        return "compute";
    return "shader";
}

static GLuint sh_compile_ex(GLenum type, const char *src, const char *label)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        sh_print_shader_log(s, sh_stage_name(type), label);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool sh_link_program(shader_t *shader, GLuint vs, GLuint fs, GLuint cs, const char *label)
{
    if (!shader)
        return false;

    if (shader->program)
        glDeleteProgram(shader->program);

    shader->program = glCreateProgram();

    if (vs)
        glAttachShader(shader->program, vs);
    if (fs)
        glAttachShader(shader->program, fs);
    if (cs)
        glAttachShader(shader->program, cs);

    glLinkProgram(shader->program);

    GLint ok = 0;
    glGetProgramiv(shader->program, GL_LINK_STATUS, &ok);

    if (vs)
        glDetachShader(shader->program, vs);
    if (fs)
        glDetachShader(shader->program, fs);
    if (cs)
        glDetachShader(shader->program, cs);

    if (!ok)
    {
        sh_print_program_log(shader->program, label ? label : "program");
        glDeleteProgram(shader->program);
        shader->program = 0;
        shader->linked = false;
        sh_uniform_cache_clear(shader);
        return false;
    }

    shader->linked = true;
    sh_uniform_cache_clear(shader);
    return true;
}

shader_t shader_create(void)
{
    shader_t s;
    memset(&s, 0, sizeof(s));
    s.program = 0;
    s.kind = SHADER_STAGE_VERTEX;
    s.linked = false;
    return s;
}

void shader_destroy(shader_t *shader)
{
    if (!shader)
        return;

    if (shader->program)
        glDeleteProgram(shader->program);

    for (size_t i = 0; i < shader->define_count; ++i)
    {
        free(shader->defines[i].key);
        free(shader->defines[i].value);
    }
    free(shader->defines);

    sh_uniform_cache_clear(shader);

    memset(shader, 0, sizeof(*shader));
}

bool shader_define(shader_t *shader, const char *key, const char *value)
{
    if (!shader || !key || !value)
        return false;

    int idx = sh_define_index(shader, key);
    if (idx >= 0)
    {
        char *nv = sh_strdup(value);
        if (!nv)
            return false;
        free(shader->defines[idx].value);
        shader->defines[idx].value = nv;
        return true;
    }

    if (shader->define_count + 1 > shader->define_capacity)
    {
        size_t nc = shader->define_capacity ? shader->define_capacity * 2 : 16;
        void *p = sh_realloc_array(shader->defines, sizeof(shader_define_kv_t), nc);
        if (!p)
            return false;
        shader->defines = (shader_define_kv_t *)p;
        shader->define_capacity = nc;
    }

    shader->defines[shader->define_count].key = sh_strdup(key);
    shader->defines[shader->define_count].value = sh_strdup(value);

    if (!shader->defines[shader->define_count].key || !shader->defines[shader->define_count].value)
        return false;

    shader->define_count++;
    return true;
}

const char *shader_get_define(const shader_t *shader, const char *key)
{
    if (!shader || !key)
        return NULL;
    int idx = sh_define_index(shader, key);
    if (idx < 0)
        return NULL;
    return shader->defines[idx].value;
}

bool shader_undefine(shader_t *shader, const char *key)
{
    if (!shader || !key)
        return false;

    int idx = sh_define_index(shader, key);
    if (idx < 0)
        return false;

    free(shader->defines[idx].key);
    free(shader->defines[idx].value);

    size_t last = shader->define_count - 1;
    if ((size_t)idx != last)
        shader->defines[idx] = shader->defines[last];

    shader->define_count--;
    return true;
}

void shader_clear_defines(shader_t *shader)
{
    if (!shader)
        return;
    for (size_t i = 0; i < shader->define_count; ++i)
    {
        free(shader->defines[i].key);
        free(shader->defines[i].value);
    }
    free(shader->defines);
    shader->defines = NULL;
    shader->define_count = 0;
    shader->define_capacity = 0;
}
char *shader_preprocess_glsl(const char *src, const shader_t *shader)
{
    if (!src)
        return NULL;

    size_t src_len = strlen(src);
    size_t extra = 0;

    if (shader)
    {
        for (size_t i = 0; i < shader->define_count; ++i)
        {
            const char *k = shader->defines[i].key ? shader->defines[i].key : "";
            const char *v = shader->defines[i].value ? shader->defines[i].value : "";
            extra += 10 + strlen(k) + 1 + strlen(v) + 1;
        }
    }

    size_t out_len = src_len + extra + 4;
    char *out = (char *)malloc(out_len);
    if (!out)
        return NULL;

    const char *p = src;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;

    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;

    size_t at = 0;

    if (strncmp(p, "#version", 8) == 0)
    {
        const char *line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p + 1) : strlen(p);

        memcpy(out + at, p, line_len);
        at += line_len;

        size_t prefix_len = (size_t)(p - src);
        const char *rest = src + prefix_len + line_len;
        size_t rest_len = src_len - prefix_len - line_len;

        if (shader)
        {
            for (size_t i = 0; i < shader->define_count; ++i)
            {
                const char *k = shader->defines[i].key ? shader->defines[i].key : "";
                const char *v = shader->defines[i].value ? shader->defines[i].value : "";
                int n = snprintf(out + at, out_len - at, "#define %s %s\n", k, v);
                if (n < 0)
                {
                    free(out);
                    return NULL;
                }
                at += (size_t)n;
            }
        }

        memcpy(out + at, rest, rest_len);
        at += rest_len;
        out[at] = 0;
        return out;
    }

    if (shader)
    {
        for (size_t i = 0; i < shader->define_count; ++i)
        {
            const char *k = shader->defines[i].key ? shader->defines[i].key : "";
            const char *v = shader->defines[i].value ? shader->defines[i].value : "";
            int n = snprintf(out + at, out_len - at, "#define %s %s\n", k, v);
            if (n < 0)
            {
                free(out);
                return NULL;
            }
            at += (size_t)n;
        }
    }

    memcpy(out + at, src, src_len + 1);
    return out;
}

static bool shader_load_from_source_labeled(shader_t *shader, const char *vertex_src, const char *fragment_src, const char *vp, const char *fp)
{
    if (!shader || !vertex_src || !fragment_src)
        return false;

    shader->kind = SHADER_STAGE_VERTEX;

    char *v = shader_preprocess_glsl(vertex_src, shader);
    char *f = shader_preprocess_glsl(fragment_src, shader);
    if (!v || !f)
    {
        free(v);
        free(f);
        return false;
    }

    GLuint vs = sh_compile_ex(GL_VERTEX_SHADER, v, vp ? vp : "vertex");
    GLuint fs = sh_compile_ex(GL_FRAGMENT_SHADER, f, fp ? fp : "fragment");

    free(v);
    free(f);

    if (!vs || !fs)
    {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return false;
    }

    bool ok = sh_link_program(shader, vs, fs, 0, "vertex+fragment");

    glDeleteShader(vs);
    glDeleteShader(fs);

    return ok;
}

bool shader_load_from_source(shader_t *shader, const char *vertex_src, const char *fragment_src)
{
    return shader_load_from_source_labeled(shader, vertex_src, fragment_src, "vertex", "fragment");
}

bool shader_load_from_files(shader_t *shader, const char *vertex_path, const char *fragment_path)
{
    if (!shader || !vertex_path || !fragment_path)
        return false;

    char *vs = sh_read_text_file(vertex_path);
    char *fs = sh_read_text_file(fragment_path);
    if (!vs || !fs)
    {
        free(vs);
        free(fs);
        return false;
    }

    bool ok = shader_load_from_source_labeled(shader, vs, fs, vertex_path, fragment_path);

    free(vs);
    free(fs);

    return ok;
}

static bool shader_load_compute_from_source_labeled(shader_t *shader, const char *compute_src, const char *cp)
{
    if (!shader || !compute_src)
        return false;

    shader->kind = SHADER_STAGE_COMPUTE;

    char *c = shader_preprocess_glsl(compute_src, shader);
    if (!c)
        return false;

    GLuint cs = sh_compile_ex(GL_COMPUTE_SHADER, c, cp ? cp : "compute");
    free(c);

    if (!cs)
        return false;

    bool ok = sh_link_program(shader, 0, 0, cs, "compute");

    glDeleteShader(cs);

    return ok;
}

bool shader_load_compute_from_source(shader_t *shader, const char *compute_src)
{
    return shader_load_compute_from_source_labeled(shader, compute_src, "compute");
}

bool shader_load_compute_from_file(shader_t *shader, const char *compute_path)
{
    if (!shader || !compute_path)
        return false;

    char *cs = sh_read_text_file(compute_path);
    if (!cs)
        return false;

    bool ok = shader_load_compute_from_source_labeled(shader, cs, compute_path);
    free(cs);
    return ok;
}

void shader_bind(const shader_t *shader)
{
    if (!shader || !shader->linked || shader->program == 0)
        return;
    glUseProgram(shader->program);
}

void shader_unbind(void)
{
    glUseProgram(0);
}

unsigned int shader_get_program(const shader_t *shader)
{
    return shader ? shader->program : 0;
}

void shader_dispatch_compute(const shader_t *shader, unsigned int groups_x, unsigned int groups_y, unsigned int groups_z)
{
    if (!shader || !shader->linked || shader->program == 0)
        return;
    glUseProgram(shader->program);
    glDispatchCompute(groups_x, groups_y, groups_z);
}

void shader_memory_barrier(unsigned int barrier_bits)
{
    glMemoryBarrier(barrier_bits);
}

bool shader_set_int(const shader_t *shader, const char *name, int value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform1i(loc, value);
    return true;
}

bool shader_set_int_array(const shader_t *shader, const char *name, const int *values, int count)
{
    if (!shader || !name || !values || count <= 0)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform1iv(loc, count, values);
    return true;
}

bool shader_set_uint(const shader_t *shader, const char *name, unsigned int value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform1ui(loc, value);
    return true;
}

bool shader_set_uint_array(const shader_t *shader, const char *name, const unsigned int *values, int count)
{
    if (!shader || !name || !values || count <= 0)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform1uiv(loc, count, values);
    return true;
}

bool shader_set_float(const shader_t *shader, const char *name, float value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform1f(loc, value);
    return true;
}

bool shader_set_float_array(const shader_t *shader, const char *name, const float *values, int count)
{
    if (!shader || !name || !values || count <= 0)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform1fv(loc, count, values);
    return true;
}

bool shader_set_vec2(const shader_t *shader, const char *name, vec2 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform2f(loc, value.x, value.y);
    return true;
}

bool shader_set_vec2_array(const shader_t *shader, const char *name, const vec2 *values, int count)
{
    if (!shader || !name || !values || count <= 0)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform2fv(loc, count, (const float *)values);
    return true;
}

bool shader_set_vec3(const shader_t *shader, const char *name, vec3 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform3f(loc, value.x, value.y, value.z);
    return true;
}

bool shader_set_vec3_array(const shader_t *shader, const char *name, const vec3 *values, int count)
{
    if (!shader || !name || !values || count <= 0)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform3fv(loc, count, (const float *)values);
    return true;
}

bool shader_set_vec4(const shader_t *shader, const char *name, vec4 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform4f(loc, value.x, value.y, value.z, value.w);
    return true;
}

bool shader_set_vec4_array(const shader_t *shader, const char *name, const vec4 *values, int count)
{
    if (!shader || !name || !values || count <= 0)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform4fv(loc, count, (const float *)values);
    return true;
}

bool shader_set_ivec2(const shader_t *shader, const char *name, vec2i value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform2i(loc, value.x, value.y);
    return true;
}

bool shader_set_ivec3(const shader_t *shader, const char *name, vec3i value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform3i(loc, value.x, value.y, value.z);
    return true;
}

bool shader_set_ivec4(const shader_t *shader, const char *name, vec4i value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform4i(loc, value.x, value.y, value.z, value.w);
    return true;
}

bool shader_set_uvec2(const shader_t *shader, const char *name, uvec2 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform2ui(loc, value.x, value.y);
    return true;
}

bool shader_set_uvec3(const shader_t *shader, const char *name, uvec3 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform3ui(loc, value.x, value.y, value.z);
    return true;
}

bool shader_set_uvec4(const shader_t *shader, const char *name, uvec4 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniform4ui(loc, value.x, value.y, value.z, value.w);
    return true;
}

bool shader_set_mat4(const shader_t *shader, const char *name, mat4 value)
{
    if (!shader || !name)
        return false;
    int loc = sh_get_uniform_location_cached((shader_t *)shader, name);
    if (loc < 0)
        return false;
    glUniformMatrix4fv(loc, 1, GL_FALSE, (const float *)value.m);
    return true;
}
