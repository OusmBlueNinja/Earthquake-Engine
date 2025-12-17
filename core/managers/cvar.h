#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    SV_MAX_PLAYERS = 0,
    SV_HOST,
    SV_PORT,
    CL_VSYNC,
    SV_CVAR_COUNT
} sv_cvar_key_t;

typedef enum
{
    CVAR_INT = 0,
    CVAR_BOOL,
    CVAR_STRING
} cvar_type_t;

typedef enum cvar_flag_t
{
    CVAR_FLAG_NONE = 0,
    CVAR_FLAG_READONLY = 1 << 0,
    CVAR_FLAG_NO_SAVE = 1 << 1,
    CVAR_FLAG_NO_LOAD = 1 << 2,
    CVAR_FLAG_CHEATS = 1 << 3
} cvar_flag_t;

int cvar_init(void);
void cvar_shutdown(void);

int32_t cvar_get_int_name(const char *name);
bool cvar_set_int_name(const char *name, int32_t v);

bool cvar_get_bool_name(const char *name);
bool cvar_set_bool_name(const char *name, bool v);

const char *cvar_get_string_name(const char *name);
bool cvar_set_string_name(const char *name, const char *v);

bool cvar_save(const char *filename);
bool cvar_load(const char *filename);
