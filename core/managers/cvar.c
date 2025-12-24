#include "cvar.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
    [CL_RENDER_DEBUG] = {.name = "cl_render_debug", .type = CVAR_INT, .def.i = 0, .flags = CVAR_FLAG_NONE},
    [CL_CPU_THREADS] = {.name = "cl_cpu_threads", .type = CVAR_INT, .def.i = 1, .flags = CVAR_FLAG_READONLY | CVAR_FLAG_NO_LOAD},
    [CL_LOG_LEVEL] = {.name = "cl_log_level", .type = CVAR_INT, .def.i = LOG_LEVEL_INFO, .flags = CVAR_FLAG_NONE},

    [CL_R_BLOOM_THRESHOLD] = {.name = "cl_r_bloom_threshold", .type = CVAR_FLOAT, .def.f = 1.0f, .flags = CVAR_FLAG_NONE},
    [CL_R_BLOOM_KNEE] = {.name = "cl_r_bloom_knee", .type = CVAR_FLOAT, .def.f = 0.5f, .flags = CVAR_FLAG_NONE},
    [CL_R_BLOOM_INTENSITY] = {.name = "cl_r_bloom_intensity", .type = CVAR_FLOAT, .def.f = 0.10f, .flags = CVAR_FLAG_NONE},
    [CL_R_BLOOM_MIPS] = {.name = "cl_r_bloom_mips", .type = CVAR_INT, .def.i = 6, .flags = CVAR_FLAG_NONE},

    [CL_R_EXPOSURE] = {.name = "cl_r_exposure", .type = CVAR_FLOAT, .def.f = 1.0f, .flags = CVAR_FLAG_NONE},
    [CL_R_OUTPUT_GAMMA] = {.name = "cl_r_output_gamma", .type = CVAR_FLOAT, .def.f = 2.2f, .flags = CVAR_FLAG_NONE},
    [CL_R_MANUAL_SRGB] = {.name = "cl_r_manual_srgb", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},

    [CL_R_ALPHA_TEST] = {.name = "cl_r_alpha_test", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},
    [CL_R_ALPHA_CUTOFF] = {.name = "cl_r_alpha_cutoff", .type = CVAR_FLOAT, .def.f = 0.5f, .flags = CVAR_FLAG_NONE},

    [CL_R_HEIGHT_INVERT] = {.name = "cl_r_height_invert", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},
    [CL_R_IBL_INTENSITY] = {.name = "cl_r_ibl_intensity", .type = CVAR_FLOAT, .def.f = 0.9f, .flags = CVAR_FLAG_NONE},

    [CL_R_SSR] = {.name = "cl_r_ssr", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},
    [CL_R_SSR_INTENSITY] = {.name = "cl_r_ssr_intensity", .type = CVAR_FLOAT, .def.f = 1.0f, .flags = CVAR_FLAG_NONE},
    [CL_R_SSR_STEPS] = {.name = "cl_r_ssr_steps", .type = CVAR_INT, .def.i = 64, .flags = CVAR_FLAG_NONE},
    [CL_R_SSR_STRIDE] = {.name = "cl_r_ssr_stride", .type = CVAR_FLOAT, .def.f = 0.15f, .flags = CVAR_FLAG_NONE},
    [CL_R_SSR_THICKNESS] = {.name = "cl_r_ssr_thickness", .type = CVAR_FLOAT, .def.f = 0.2f, .flags = CVAR_FLAG_NONE},
    [CL_R_SSR_MAX_DIST] = {.name = "cl_r_ssr_max_dist", .type = CVAR_FLOAT, .def.f = 50.0f, .flags = CVAR_FLAG_NONE},
    [CL_R_FORCE_LOD_LEVEL] = {.name = "cl_r_force_lod_level", .type = CVAR_INT, .def.i = -1, .flags = CVAR_FLAG_NONE},

    [CL_R_WIREFRAME] = {.name = "cl_r_wireframe", .type = CVAR_BOOL, .def.b = false, .flags = CVAR_FLAG_NONE},

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

static bool cvar_can_set(sv_cvar_key_t k)
{
    return cvar_can_set_internal(k, false, false);
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

        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            ikv_object_set_int(root, g_cvars[i].name, (int64_t)g_cvars[i].value.i);
            break;
        case CVAR_BOOL:
            ikv_object_set_bool(root, g_cvars[i].name, g_cvars[i].value.b);
            break;
        case CVAR_FLOAT:
            ikv_object_set_float(root, g_cvars[i].name, (double)g_cvars[i].value.f);
            break;
        case CVAR_STRING:
            ikv_object_set_string(root, g_cvars[i].name, g_cvars[i].value.s);
            break;
        default:
            break;
        }
    }

    bool ok = ikv_write_file(filename, root);
    ikv_free(root);
    return ok;
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

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        if (g_cvars[i].flags & CVAR_FLAG_NO_LOAD)
            continue;

        ikv_node_t *n = ikv_object_get(root, g_cvars[i].name);
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
