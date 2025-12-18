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
        char s[64];
    } value;
    union
    {
        int32_t i;
        bool b;
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

static cvar_entry_t g_cvars[SV_CVAR_COUNT] = {
    [SV_MAX_PLAYERS] = {.name = "sv_max_players", .type = CVAR_INT, .def.i = 20, .flags = CVAR_FLAG_CHEATS | CVAR_FLAG_NO_LOAD},
    [SV_HOST] = {.name = "sv_host", .type = CVAR_STRING, .def.s = "0.0.0.0", .flags = CVAR_FLAG_READONLY | CVAR_FLAG_NO_LOAD},
    [SV_PORT] = {.name = "sv_port", .type = CVAR_INT, .def.i = 0, .flags = CVAR_FLAG_READONLY | CVAR_FLAG_NO_LOAD},
    [CL_VSYNC] = {.name = "cl_vsync", .type = CVAR_BOOL, .def.b = true, .flags = CVAR_FLAG_NONE},
};

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
    {
        g_cvars[i].hash = fnv1a(g_cvars[i].name);
    }

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        for (int j = i + 1; j < SV_CVAR_COUNT; ++j)
        {
            if (g_cvars[i].hash == g_cvars[j].hash)
            {
                LOG_ERROR("CVar hash collision: '%s' and '%s' both hash to 0x%08X",
                          g_cvars[i].name ? g_cvars[i].name : "<null>",
                          g_cvars[j].name ? g_cvars[j].name : "<null>",
                          (unsigned int)g_cvars[i].hash);
                return 1;
            }
        }
    }

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            g_cvars[i].value.i = g_cvars[i].def.i;
            break;
        case CVAR_BOOL:
            g_cvars[i].value.b = g_cvars[i].def.b;
            break;
        case CVAR_STRING:
            strncpy(g_cvars[i].value.s, g_cvars[i].def.s ? g_cvars[i].def.s : "", sizeof(g_cvars[i].value.s) - 1);
            g_cvars[i].value.s[sizeof(g_cvars[i].value.s) - 1] = 0;
            break;
        default:
            break;
        }
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

const char *cvar_get_string_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_STRING ? g_cvars[k].value.s : "";
}

bool cvar_set_int_name(const char *name, int32_t v)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_INT || (g_cvars[k].flags & CVAR_FLAG_READONLY))
        return false;

    int32_t oldv = g_cvars[k].value.i;
    if (oldv == v)
        return true;

    g_cvars[k].value.i = v;
    cvar_fire_changed(k, &oldv, &g_cvars[k].value.i);
    return true;
}

bool cvar_set_bool_name(const char *name, bool v)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_BOOL || (g_cvars[k].flags & CVAR_FLAG_READONLY))
        return false;

    bool oldv = g_cvars[k].value.b;
    if (oldv == v)
        return true;

    g_cvars[k].value.b = v;
    cvar_fire_changed(k, &oldv, &g_cvars[k].value.b);
    return true;
}

bool cvar_set_string_name(const char *name, const char *v)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_STRING || (g_cvars[k].flags & CVAR_FLAG_READONLY))
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

bool cvar_set_callback_key(sv_cvar_key_t key, cvar_changed_fn fn)
{
    if (key >= SV_CVAR_COUNT)
        return false;
    g_cvars[key].on_changed = fn;
    return true;
}

bool cvar_set_callback_name(const char *name, cvar_changed_fn fn)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT)
        return false;
    g_cvars[k].on_changed = fn;
    return true;
}

bool cvar_save(const char *filename)
{
    if (!filename || !filename[0])
        return false;

    ikv_node_t *root = ikv_create_object(NULL);
    if (!root)
        return false;

    ikv_node_t *cvars = ikv_object_add_object(root, "cvars");
    if (!cvars)
    {
        ikv_free(root);
        return false;
    }

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        if (g_cvars[i].flags & CVAR_FLAG_NO_SAVE)
            continue;

        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            ikv_object_set_int(cvars, g_cvars[i].name, (int64_t)g_cvars[i].value.i);
            break;
        case CVAR_BOOL:
            ikv_object_set_bool(cvars, g_cvars[i].name, g_cvars[i].value.b);
            break;
        case CVAR_STRING:
            ikv_object_set_string(cvars, g_cvars[i].name, g_cvars[i].value.s);
            break;
        default:
            break;
        }
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

    ikv_node_t *cvars = ikv_object_get(root, "cvars");
    if (!cvars || cvars->type != IKV_OBJECT)
    {
        ikv_free(root);
        return false;
    }

    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        if (g_cvars[i].flags & CVAR_FLAG_NO_LOAD)
            continue;

        ikv_node_t *n = ikv_object_get(cvars, g_cvars[i].name);
        if (!n)
            continue;

        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            if (n->type == IKV_INT)
            {
                int32_t oldv = g_cvars[i].value.i;
                int32_t newv = (int32_t)ikv_as_int(n);
                if (oldv != newv)
                {
                    g_cvars[i].value.i = newv;
                    cvar_fire_changed((sv_cvar_key_t)i, &oldv, &g_cvars[i].value.i);
                }
            }
            break;

        case CVAR_BOOL:
            if (n->type == IKV_BOOL)
            {
                bool oldv = g_cvars[i].value.b;
                bool newv = ikv_as_bool(n);
                if (oldv != newv)
                {
                    g_cvars[i].value.b = newv;
                    cvar_fire_changed((sv_cvar_key_t)i, &oldv, &g_cvars[i].value.b);
                }
            }
            else if (n->type == IKV_INT)
            {
                bool oldv = g_cvars[i].value.b;
                bool newv = ikv_as_int(n) != 0;
                if (oldv != newv)
                {
                    g_cvars[i].value.b = newv;
                    cvar_fire_changed((sv_cvar_key_t)i, &oldv, &g_cvars[i].value.b);
                }
            }
            break;

        case CVAR_STRING:
            if (n->type == IKV_STRING)
            {
                const char *s = ikv_as_string(n);
                const char *in = s ? s : "";

                if (strncmp(g_cvars[i].value.s, in, sizeof(g_cvars[i].value.s)) != 0)
                {
                    char oldv[64];
                    strncpy(oldv, g_cvars[i].value.s, sizeof(oldv) - 1);
                    oldv[sizeof(oldv) - 1] = 0;

                    strncpy(g_cvars[i].value.s, in, sizeof(g_cvars[i].value.s) - 1);
                    g_cvars[i].value.s[sizeof(g_cvars[i].value.s) - 1] = 0;

                    cvar_fire_changed((sv_cvar_key_t)i, oldv, g_cvars[i].value.s);
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
