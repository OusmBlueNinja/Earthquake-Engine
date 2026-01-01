#include "cvar.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "utils/logger.h"
#include "systems/iKv1.h"

typedef struct
{
    const char *name;
    uint32_t hash;
    cvar_type_t type;
    uint32_t flags;
    cvar_changed_fn on_changed;
    union
    {
        int32_t i;
        bool b;
        float f;
        char s[64];
    } value;
    union
    {
        int32_t i;
        bool b;
        float f;
        const char *s;
    } def;
} cvar_entry_t;

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
    {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static bool g_cheats_permission = false;

static cvar_entry_t g_cvars[SV_CVAR_COUNT] = {
    [SV_CHEATS] = {.name = "sv_cheats", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NO_LOAD | CVAR_FLAG_NO_SAVE},

    [SV_MAX_PLAYERS] = {.name = "sv_max_players", .type = CVAR_INT, .def.i = 20, .flags = CVAR_FLAG_NO_LOAD},
    [SV_HOST] = {.name = "sv_host", .type = CVAR_STRING, .def.s = "0.0.0.0", .flags = CVAR_FLAG_READONLY | CVAR_FLAG_NO_LOAD},
    [SV_PORT] = {.name = "sv_port", .type = CVAR_INT, .def.i = 0, .flags = CVAR_FLAG_READONLY | CVAR_FLAG_NO_LOAD},

    [CL_VSYNC] = {.name = "cl_vsync", .type = CVAR_BOOL, .def.b = true, .flags = CVAR_FLAG_NONE},
    [CL_BLOOM] = {.name = "cl_bloom", .type = CVAR_BOOL, .def.b = true, .flags = CVAR_FLAG_NONE},
    [CL_MSAA] = {.name = "cl_msaa_enabled", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},
    [CL_MSAA_SAMPLES] = {.name = "cl_msaa_samples", .type = CVAR_INT, .def.i = 4, .flags = CVAR_FLAG_NONE},
    [CL_RENDER_DEBUG] = {.name = "cl_render_debug", .type = CVAR_INT, .def.i = 0, .flags = CVAR_FLAG_NO_LOAD | CVAR_FLAG_NO_SAVE},
    [CL_CPU_THREADS] = {.name = "cl_cpu_threads", .type = CVAR_INT, .def.i = 1, .flags = CVAR_FLAG_READONLY | CVAR_FLAG_NO_LOAD},
    [CL_LOG_LEVEL] = {.name = "cl_log_level", .type = CVAR_INT, .def.i = LOG_LEVEL_INFO, .flags = CVAR_FLAG_NONE},
    [CL_R_SHADOWS] = {.name = "cl_r_shadows", .type = CVAR_BOOL, .def.b = true, .flags = CVAR_FLAG_NONE},
    [CL_AUTO_EXPOSURE] = {.name = "cl_auto_exposure", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},
    [CL_AUTO_EXPOSURE_HZ] = {.name = "cl_auto_exposure_hz", .type = CVAR_INT, .def.i = 10, .flags = CVAR_FLAG_NONE},
    [CL_R_RESTORE_GL_STATE] = {.name = "cl_r_restore_gl_state", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},
    [CL_R_FORCE_LOD_LEVEL] = {.name = "cl_r_force_lod_level", .type = CVAR_INT, .def.i = -1, .flags = CVAR_FLAG_NO_LOAD | CVAR_FLAG_NO_SAVE},
    [CL_R_WIREFRAME] = {.name = "cl_r_wireframe", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NO_LOAD | CVAR_FLAG_NO_SAVE},
};

void cvar_set_cheats_permission(bool allowed)
{
    g_cheats_permission = allowed;
}

bool cvar_get_cheats_permission(void)
{
    return g_cheats_permission;
}

static bool cvar_cheats_enabled(void)
{
    return g_cvars[SV_CHEATS].type == CVAR_BOOL ? g_cvars[SV_CHEATS].value.b : false;
}

static bool cvar_can_set_internal(sv_cvar_key_t k, bool bypass_cheats, bool bypass_readonly)
{
    if (k >= SV_CVAR_COUNT)
        return false;

    if (!bypass_readonly && (g_cvars[k].flags & CVAR_FLAG_READONLY))
        return false;

    if (k == SV_CHEATS && !g_cheats_permission)
        return false;

    if (!bypass_cheats && (g_cvars[k].flags & CVAR_FLAG_CHEATS))
    {
        if (!cvar_cheats_enabled() && k != SV_CHEATS)
            return false;
        if (!g_cheats_permission)
            return false;
    }

    return true;
}

static void cvar_fire_changed(sv_cvar_key_t k, const void *oldv, const void *newv)
{
    if (k >= SV_CVAR_COUNT)
        return;
    if (g_cvars[k].on_changed)
        g_cvars[k].on_changed(k, oldv, newv);
}

static sv_cvar_key_t find_key(const char *name)
{
    if (!name || !name[0])
        return SV_CVAR_COUNT;

    uint32_t h = fnv1a(name);

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        if (g_cvars[i].hash != h)
            continue;

        if (g_cvars[i].name && strcmp(g_cvars[i].name, name) == 0)
            return (sv_cvar_key_t)i;
    }

    return SV_CVAR_COUNT;
}

int cvar_init(void)
{
    for (int i = 0; i < SV_CVAR_COUNT; ++i)
        g_cvars[i].hash = fnv1a(g_cvars[i].name);

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
        for (int j = i + 1; j < SV_CVAR_COUNT; ++j)
            if (g_cvars[i].hash == g_cvars[j].hash)
            {
                LOG_ERROR("CVar hash collision: '%s' and '%s' both hash to 0x%08X",
                          g_cvars[i].name ? g_cvars[i].name : "<null>",
                          g_cvars[j].name ? g_cvars[j].name : "<null>",
                          (unsigned int)g_cvars[i].hash);
                return 1;
            }

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            g_cvars[i].value.i = g_cvars[i].def.i;
            break;
        case CVAR_BOOL:
            g_cvars[i].value.b = g_cvars[i].def.b;
            break;
        case CVAR_FLOAT:
            g_cvars[i].value.f = g_cvars[i].def.f;
            break;
        case CVAR_STRING:
            strncpy(g_cvars[i].value.s, g_cvars[i].def.s ? g_cvars[i].def.s : "", sizeof(g_cvars[i].value.s) - 1);
            g_cvars[i].value.s[sizeof(g_cvars[i].value.s) - 1] = 0;
            break;
        default:
            break;
        }

    return 0;
}

void cvar_shutdown(void) {}

int32_t cvar_get_int_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_INT ? g_cvars[k].value.i : 0;
}

bool cvar_get_bool_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_BOOL ? g_cvars[k].value.b : false;
}

float cvar_get_float_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_FLOAT ? g_cvars[k].value.f : 0.0f;
}

const char *cvar_get_string_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_STRING ? g_cvars[k].value.s : "";
}

static bool cvar_set_int_name_ex(const char *name, int32_t v, bool bypass_cheats, bool bypass_readonly)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_INT)
        return false;

    if (!cvar_can_set_internal(k, bypass_cheats, bypass_readonly))
        return false;

    int32_t oldv = g_cvars[k].value.i;
    if (oldv == v)
        return true;

    g_cvars[k].value.i = v;
    cvar_fire_changed(k, &oldv, &g_cvars[k].value.i);
    return true;
}

static bool cvar_set_bool_name_ex(const char *name, bool v, bool bypass_cheats, bool bypass_readonly)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_BOOL)
        return false;

    if (!cvar_can_set_internal(k, bypass_cheats, bypass_readonly))
        return false;

    bool oldv = g_cvars[k].value.b;
    if (oldv == v)
        return true;

    g_cvars[k].value.b = v;
    cvar_fire_changed(k, &oldv, &g_cvars[k].value.b);
    return true;
}

static bool cvar_set_float_name_ex(const char *name, float v, bool bypass_cheats, bool bypass_readonly)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_FLOAT)
        return false;

    if (!cvar_can_set_internal(k, bypass_cheats, bypass_readonly))
        return false;

    float oldv = g_cvars[k].value.f;
    if (oldv == v)
        return true;

    g_cvars[k].value.f = v;
    cvar_fire_changed(k, &oldv, &g_cvars[k].value.f);
    return true;
}

static bool cvar_set_string_name_ex(const char *name, const char *v, bool bypass_cheats, bool bypass_readonly)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_STRING)
        return false;

    if (!cvar_can_set_internal(k, bypass_cheats, bypass_readonly))
        return false;

    const char *in = v ? v : "";
    if (strncmp(g_cvars[k].value.s, in, sizeof(g_cvars[k].value.s)) == 0)
        return true;

    char oldv[64];
    strncpy(oldv, g_cvars[k].value.s, sizeof(oldv) - 1);
    oldv[sizeof(oldv) - 1] = 0;

    strncpy(g_cvars[k].value.s, in, sizeof(g_cvars[k].value.s) - 1);
    g_cvars[k].value.s[sizeof(g_cvars[k].value.s) - 1] = 0;

    cvar_fire_changed(k, oldv, g_cvars[k].value.s);
    return true;
}

bool cvar_set_int_name(const char *name, int32_t v)
{
    return cvar_set_int_name_ex(name, v, false, false);
}

bool cvar_set_bool_name(const char *name, bool v)
{
    return cvar_set_bool_name_ex(name, v, false, false);
}

bool cvar_set_float_name(const char *name, float v)
{
    return cvar_set_float_name_ex(name, v, false, false);
}

bool cvar_set_string_name(const char *name, const char *v)
{
    return cvar_set_string_name_ex(name, v, false, false);
}

bool cvar_force_set_int_name(const char *name, int32_t v)
{
    return cvar_set_int_name_ex(name, v, true, true);
}

bool cvar_force_set_bool_name(const char *name, bool v)
{
    return cvar_set_bool_name_ex(name, v, true, true);
}

bool cvar_force_set_float_name(const char *name, float v)
{
    return cvar_set_float_name_ex(name, v, true, true);
}

bool cvar_force_set_string_name(const char *name, const char *v)
{
    return cvar_set_string_name_ex(name, v, true, true);
}

bool cvar_set_callback_key(sv_cvar_key_t key, cvar_changed_fn fn)
{
    if (key >= SV_CVAR_COUNT)
    {
        LOG_ERROR("CVar '%d' does not exist", key);
        return false;
    }
    g_cvars[key].on_changed = fn;
    return true;
}

bool cvar_set_callback_name(const char *name, cvar_changed_fn fn)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT)
    {
        LOG_ERROR("CVar '%s' does not exist", name);
        return false;
    }
    g_cvars[k].on_changed = fn;
    return true;
}

static bool ikv_root_is_named(const ikv_node_t *root, const char *name)
{
    if (!root || root->type != IKV_OBJECT)
        return false;
    if (!name || !name[0])
        return false;
    if (!root->key || !root->key[0])
        return false;
    return strcmp(root->key, name) == 0;
}

static void cvar_strlower(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }

    size_t i = 0;
    for (; src[i] && i + 1 < cap; ++i)
    {
        char c = src[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
    }
    dst[i] = 0;
}

static const char *cvar_map_prefix(const char *tok_lower)
{
    if (!tok_lower || !tok_lower[0])
        return "misc";

    if (strcmp(tok_lower, "sv") == 0)
        return "server";
    if (strcmp(tok_lower, "cl") == 0)
        return "cl";

    return tok_lower;
}

static int cvar_split_tokens(char parts[][64], int max_parts, const char *name)
{
    if (!parts || max_parts <= 0 || !name || !name[0])
        return 0;

    int count = 0;
    const char *p = name;

    while (*p && count < max_parts)
    {
        while (*p == '_')
            ++p;
        if (!*p)
            break;

        char buf[64];
        size_t bi = 0;

        while (*p && *p != '_' && bi + 1 < sizeof(buf))
            buf[bi++] = *p++;

        buf[bi] = 0;

        cvar_strlower(parts[count], 64, buf);
        ++count;

        while (*p == '_')
            ++p;
    }

    if (count > 0)
    {
        const char *mapped = cvar_map_prefix(parts[0]);
        if (mapped != parts[0])
            cvar_strlower(parts[0], 64, mapped);
    }

    return count;
}

static void cvar_build_slash_key(char *dst, size_t cap, const char *name)
{
    if (!dst || cap == 0)
        return;
    dst[0] = 0;

    char parts[16][64];
    int n = cvar_split_tokens(parts, 16, name);
    if (n <= 0)
        return;

    size_t at = 0;
    for (int i = 0; i < n; ++i)
    {
        const char *seg = parts[i];
        if (!seg[0])
            continue;

        if (i > 0)
        {
            if (at + 1 >= cap)
                break;
            dst[at++] = '/';
            dst[at] = 0;
        }

        size_t need = strlen(seg);
        if (at + need >= cap)
            need = cap - at - 1;

        memcpy(dst + at, seg, need);
        at += need;
        dst[at] = 0;

        if (at + 1 >= cap)
            break;
    }
}

static ikv_node_t *cvar_ensure_object(ikv_node_t *parent_obj, const char *key)
{
    if (!parent_obj || parent_obj->type != IKV_OBJECT || !key || !key[0])
        return NULL;

    ikv_node_t *n = ikv_object_get(parent_obj, key);
    if (n)
        return n->type == IKV_OBJECT ? n : NULL;

    return ikv_object_add_object(parent_obj, key);
}

static ikv_node_t *cvar_find_node_nested(const ikv_node_t *root, const char *cvar_name)
{
    char parts[16][64];
    int n = cvar_split_tokens(parts, 16, cvar_name);
    if (n <= 0)
        return NULL;

    const ikv_node_t *cur = root;

    for (int i = 0; i < n - 1; ++i)
    {
        if (!cur || cur->type != IKV_OBJECT)
            return NULL;

        cur = ikv_object_get(cur, parts[i]);
        if (!cur || cur->type != IKV_OBJECT)
            return NULL;
    }

    if (!cur || cur->type != IKV_OBJECT)
        return NULL;

    return ikv_object_get(cur, parts[n - 1]);
}

static void cvar_set_value_nested(ikv_node_t *root, const cvar_entry_t *cv)
{
    char parts[16][64];
    int n = cvar_split_tokens(parts, 16, cv->name);
    if (n <= 0)
        return;

    ikv_node_t *cur = root;

    for (int i = 0; i < n - 1; ++i)
    {
        cur = cvar_ensure_object(cur, parts[i]);
        if (!cur)
            return;
    }

    const char *leaf = parts[n - 1];

    switch (cv->type)
    {
    case CVAR_INT:
        ikv_object_set_int(cur, leaf, (int64_t)cv->value.i);
        break;
    case CVAR_BOOL:
        ikv_object_set_bool(cur, leaf, cv->value.b);
        break;
    case CVAR_FLOAT:
        ikv_object_set_float(cur, leaf, (double)cv->value.f);
        break;
    case CVAR_STRING:
        ikv_object_set_string(cur, leaf, cv->value.s);
        break;
    default:
        break;
    }
}

static bool cvar_node_type_matches(const cvar_entry_t *cv, const ikv_node_t *n)
{
    if (!cv || !n)
        return false;

    switch (cv->type)
    {
    case CVAR_INT:
        return n->type == IKV_INT;
    case CVAR_BOOL:
        return n->type == IKV_BOOL || n->type == IKV_INT;
    case CVAR_FLOAT:
        return n->type == IKV_FLOAT || n->type == IKV_INT;
    case CVAR_STRING:
        return n->type == IKV_STRING;
    default:
        return false;
    }
}

bool cvar_save(const char *filename)
{
    if (!filename || !filename[0])
        return false;

    ikv_node_t *root = ikv_create_object("icvar");
    if (!root)
        return false;

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        if (g_cvars[i].flags & CVAR_FLAG_NO_SAVE)
            continue;

        cvar_set_value_nested(root, &g_cvars[i]);
    }

    bool ok = ikv_write_file(filename, root);
    ikv_free(root);
    return ok;
}

bool cvar_load(const char *filename)
{
    if (!filename || !filename[0])
        return false;

    ikv_node_t *root = ikv_parse_file(filename);
    if (!root)
        return false;

    if (!ikv_root_is_named(root, "icvar"))
    {
        ikv_free(root);
        return false;
    }

    char slash_key[256];

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        if (g_cvars[i].flags & CVAR_FLAG_NO_LOAD)
            continue;

        ikv_node_t *n = cvar_find_node_nested(root, g_cvars[i].name);

        if (!n)
        {
            cvar_build_slash_key(slash_key, sizeof(slash_key), g_cvars[i].name);
            if (slash_key[0])
                n = ikv_object_get(root, slash_key);
        }

        if (!n)
            n = ikv_object_get(root, g_cvars[i].name);

        if (!n)
            continue;

        if (!cvar_node_type_matches(&g_cvars[i], n))
            continue;

        sv_cvar_key_t key = (sv_cvar_key_t)i;

        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            if (n->type == IKV_INT)
            {
                int32_t oldv = g_cvars[i].value.i;
                int32_t newv = (int32_t)ikv_as_int(n);
                if (oldv != newv && cvar_can_set_internal(key, true, true))
                {
                    g_cvars[i].value.i = newv;
                    cvar_fire_changed(key, &oldv, &g_cvars[i].value.i);
                }
            }
            break;

        case CVAR_BOOL:
        {
            bool oldv = g_cvars[i].value.b;
            bool newv = (n->type == IKV_BOOL) ? ikv_as_bool(n) : (ikv_as_int(n) != 0);
            if (oldv != newv && cvar_can_set_internal(key, true, true))
            {
                g_cvars[i].value.b = newv;
                cvar_fire_changed(key, &oldv, &g_cvars[i].value.b);
            }
        }
        break;

        case CVAR_FLOAT:
        {
            float oldv = g_cvars[i].value.f;
            float newv = 0.0f;
            if (n->type == IKV_FLOAT)
                newv = (float)ikv_as_float(n);
            else
                newv = (float)ikv_as_int(n);

            if (oldv != newv && cvar_can_set_internal(key, true, true))
            {
                g_cvars[i].value.f = newv;
                cvar_fire_changed(key, &oldv, &g_cvars[i].value.f);
            }
        }
        break;

        case CVAR_STRING:
            if (n->type == IKV_STRING)
            {
                const char *s = ikv_as_string(n);
                const char *in = s ? s : "";

                if (strncmp(g_cvars[i].value.s, in, sizeof(g_cvars[i].value.s)) != 0 && cvar_can_set_internal(key, true, true))
                {
                    char oldv[64];
                    strncpy(oldv, g_cvars[i].value.s, sizeof(oldv) - 1);
                    oldv[sizeof(oldv) - 1] = 0;

                    strncpy(g_cvars[i].value.s, in, sizeof(g_cvars[i].value.s) - 1);
                    g_cvars[i].value.s[sizeof(g_cvars[i].value.s) - 1] = 0;

                    cvar_fire_changed(key, oldv, g_cvars[i].value.s);
                }
            }
            break;

        default:
            break;
        }
    }

    ikv_free(root);
    return true;
}
