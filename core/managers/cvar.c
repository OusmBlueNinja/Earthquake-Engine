#include "cvar.h"
#include <string.h>
#include <stddef.h>

typedef struct
{
    const char *name;
    uint32_t hash;
    cvar_type_t type;
    uint32_t flags;
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

static cvar_entry_t g_cvars[SV_CVAR_COUNT] = {
    [SV_MAX_PLAYERS] = {.name = "sv_max_players", .type = CVAR_INT, .def.i = 20, .flags = CVAR_FLAG_NONE},
    [SV_HOST] = {.name = "sv_host", .type = CVAR_STRING, .def.s = "0.0.0.0", .flags = CVAR_FLAG_NO_SAVE},
    [SV_PORT] = {.name = "sv_port", .type = CVAR_INT, .def.i = 20, .flags = CVAR_FLAG_NONE},
    [CL_VSYNC] = {.name = "cl_vsync", .type = CVAR_BOOL, .def.b = true, .flags = CVAR_FLAG_READONLY},
};

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

static sv_cvar_key_t find_key(const char *name)
{
    uint32_t h = fnv1a(name);
    for (int i = 0; i < SV_CVAR_COUNT; ++i)
        if (g_cvars[i].hash == h)
            return (sv_cvar_key_t)i;
    return SV_CVAR_COUNT;
}

int cvar_init(void)
{
    for (int i = 0; i < SV_CVAR_COUNT; ++i)
    {
        g_cvars[i].hash = fnv1a(g_cvars[i].name);
        switch (g_cvars[i].type)
        {
        case CVAR_INT:
            g_cvars[i].value.i = g_cvars[i].def.i;
            break;
        case CVAR_BOOL:
            g_cvars[i].value.b = g_cvars[i].def.b;
            break;
        case CVAR_STRING:
            strncpy(g_cvars[i].value.s, g_cvars[i].def.s, sizeof(g_cvars[i].value.s) - 1);
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

bool cvar_set_int_name(const char *name, int32_t v)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_INT || (g_cvars[k].flags & CVAR_FLAG_READONLY))
        return false;
    g_cvars[k].value.i = v;
    return true;
}

bool cvar_get_bool_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_BOOL ? g_cvars[k].value.b : false;
}

bool cvar_set_bool_name(const char *name, bool v)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_BOOL || (g_cvars[k].flags & CVAR_FLAG_READONLY))
        return false;
    g_cvars[k].value.b = v;
    return true;
}

const char *cvar_get_string_name(const char *name)
{
    sv_cvar_key_t k = find_key(name);
    return k < SV_CVAR_COUNT && g_cvars[k].type == CVAR_STRING ? g_cvars[k].value.s : "";
}

bool cvar_set_string_name(const char *name, const char *v)
{
    sv_cvar_key_t k = find_key(name);
    if (k >= SV_CVAR_COUNT || g_cvars[k].type != CVAR_STRING || (g_cvars[k].flags & CVAR_FLAG_READONLY))
        return false;
    strncpy(g_cvars[k].value.s, v, sizeof(g_cvars[k].value.s) - 1);
    g_cvars[k].value.s[sizeof(g_cvars[k].value.s) - 1] = 0;
    return true;
}
