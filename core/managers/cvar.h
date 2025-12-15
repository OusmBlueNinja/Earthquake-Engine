#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SV_MAX_PLAYERS = 0,
    SV_HOST,
    SV_PORT,
    SV_CVAR_COUNT
} sv_cvar_key_t;

typedef enum
{
    CVAR_INT = 0,
    CVAR_BOOL = 1,
    CVAR_STRING = 2
} cvar_type_t;

int cvar_init(void);
void cvar_shutdown(void);

cvar_type_t cvar_type(sv_cvar_key_t key);
const char *cvar_name(sv_cvar_key_t key);

bool cvar_set_int(sv_cvar_key_t key, int32_t v);
bool cvar_get_int(sv_cvar_key_t key, int32_t *out_v);

bool cvar_set_bool(sv_cvar_key_t key, bool v);
bool cvar_get_bool(sv_cvar_key_t key, bool *out_v);

bool cvar_set_string(sv_cvar_key_t key, const char *s);
bool cvar_get_string(sv_cvar_key_t key, char *out, size_t out_len);

void cvar_reset(sv_cvar_key_t key);
void cvar_reset_all(void);
