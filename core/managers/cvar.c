#include "cvar.h"
#include <string.h>

typedef struct
{
    const char *name;
    cvar_type_t type;
    int32_t def_i;
    bool def_b;
    const char *def_s;
    int32_t i;
    bool b;
    char s[64];
} cvar_entry_t;

static cvar_entry_t g_cvars[SV_CVAR_COUNT] = {
    [SV_MAX_PLAYERS] = {.name = "sv_max_players", .type = CVAR_INT, .def_i = 20},
    [SV_HOST] = {.name = "sv_host", .type = CVAR_STRING, .def_s = "0.0.0.0"},
    [SV_PORT] = {.name = "sv_port", .type = CVAR_INT, .def_i = 20},
};

static bool g_cvars_inited = false;

static bool key_ok(sv_cvar_key_t key)
{
    return (key >= 0) && (key < SV_CVAR_COUNT);
}

int cvar_init(void)
{
    if (SV_CVAR_COUNT == 0)
    {
        return 1;
    }

    for (int i = 0; i < (int)SV_CVAR_COUNT; ++i)
    {
        g_cvars[i].i = g_cvars[i].def_i;
        g_cvars[i].b = g_cvars[i].def_b;
        if (g_cvars[i].def_s)
        {
            strncpy(g_cvars[i].s, g_cvars[i].def_s, sizeof(g_cvars[i].s) - 1);
            g_cvars[i].s[sizeof(g_cvars[i].s) - 1] = 0;
        }
        else
        {
            g_cvars[i].s[0] = 0;
        }
    }

    g_cvars_inited = true;
    return 0;
}

void cvar_shutdown(void)
{
    if (!g_cvars_inited)
        return;

    for (int i = 0; i < (int)SV_CVAR_COUNT; ++i)
    {
        g_cvars[i].i = g_cvars[i].def_i;
        g_cvars[i].b = g_cvars[i].def_b;
        g_cvars[i].s[0] = 0;
    }

    g_cvars_inited = false;
}

cvar_type_t cvar_type(sv_cvar_key_t key)
{
    if (!key_ok(key))
        return CVAR_INT;
    return g_cvars[key].type;
}

const char *cvar_name(sv_cvar_key_t key)
{
    if (!key_ok(key))
        return "";
    return g_cvars[key].name ? g_cvars[key].name : "";
}

bool cvar_set_int(sv_cvar_key_t key, int32_t v)
{
    if (!key_ok(key))
        return false;
    if (g_cvars[key].type != CVAR_INT)
        return false;
    g_cvars[key].i = v;
    return true;
}

bool cvar_get_int(sv_cvar_key_t key, int32_t *out_v)
{
    if (!key_ok(key) || !out_v)
        return false;
    if (g_cvars[key].type != CVAR_INT)
        return false;
    *out_v = g_cvars[key].i;
    return true;
}

bool cvar_set_bool(sv_cvar_key_t key, bool v)
{
    if (!key_ok(key))
        return false;
    if (g_cvars[key].type != CVAR_BOOL)
        return false;
    g_cvars[key].b = v;
    return true;
}

bool cvar_get_bool(sv_cvar_key_t key, bool *out_v)
{
    if (!key_ok(key) || !out_v)
        return false;
    if (g_cvars[key].type != CVAR_BOOL)
        return false;
    *out_v = g_cvars[key].b;
    return true;
}

bool cvar_set_string(sv_cvar_key_t key, const char *s)
{
    if (!key_ok(key) || !s)
        return false;
    if (g_cvars[key].type != CVAR_STRING)
        return false;
    strncpy(g_cvars[key].s, s, sizeof(g_cvars[key].s) - 1);
    g_cvars[key].s[sizeof(g_cvars[key].s) - 1] = 0;
    return true;
}

bool cvar_get_string(sv_cvar_key_t key, char *out, size_t out_len)
{
    if (!key_ok(key) || !out || out_len == 0)
        return false;
    if (g_cvars[key].type != CVAR_STRING)
        return false;
    size_t n = strlen(g_cvars[key].s);
    if (n + 1 > out_len)
        return false;
    memcpy(out, g_cvars[key].s, n + 1);
    return true;
}

void cvar_reset(sv_cvar_key_t key)
{
    if (!key_ok(key))
        return;
    g_cvars[key].i = g_cvars[key].def_i;
    g_cvars[key].b = g_cvars[key].def_b;
    if (g_cvars[key].def_s)
    {
        strncpy(g_cvars[key].s, g_cvars[key].def_s, sizeof(g_cvars[key].s) - 1);
        g_cvars[key].s[sizeof(g_cvars[key].s) - 1] = 0;
    }
    else
    {
        g_cvars[key].s[0] = 0;
    }
}

void cvar_reset_all(void)
{
    cvar_init();
}
