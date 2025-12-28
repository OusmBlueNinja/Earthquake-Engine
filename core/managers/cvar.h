#ifndef CVAR_H
#define CVAR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    CVAR_INT = 0,
    CVAR_BOOL,
    CVAR_FLOAT,
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

typedef enum
{
    SV_CHEATS = 0,
    SV_MAX_PLAYERS,
    SV_HOST,
    SV_PORT,
    CL_VSYNC,
    CL_BLOOM,
    CL_RENDER_DEBUG,
    CL_CPU_THREADS,
    CL_LOG_LEVEL,

    CL_R_BLOOM_THRESHOLD,
    CL_R_BLOOM_KNEE,
    CL_R_BLOOM_INTENSITY,
    CL_R_BLOOM_MIPS,

    CL_R_EXPOSURE_LEVEL,
    CL_R_EXPOSURE_AUTO,
    CL_R_OUTPUT_GAMMA,
    CL_R_MANUAL_SRGB,

    CL_R_ALPHA_TEST,
    CL_R_ALPHA_CUTOFF,

    CL_R_HEIGHT_INVERT,
    CL_R_IBL_INTENSITY,

    CL_R_SSR,
    CL_R_SSR_INTENSITY,
    CL_R_SSR_STEPS,
    CL_R_SSR_STRIDE,
    CL_R_SSR_THICKNESS,
    CL_R_SSR_MAX_DIST,
    CL_R_FORCE_LOD_LEVEL,
    CL_R_WIREFRAME,
    CL_R_PT,
    CL_R_PT_SPP,
    CL_R_PT_ENV_INTENSITY,
    CL_R_PT_HALFRES,
    CL_R_PT_BOUNCES,
    CL_R_PT_REBUILD_EPS,

    SV_CVAR_COUNT
} sv_cvar_key_t;

typedef void (*cvar_changed_fn)(sv_cvar_key_t key, const void *old_state, const void *state);

int cvar_init(void);
void cvar_shutdown(void);

bool cvar_load(const char *filename);
bool cvar_save(const char *filename);

void cvar_set_cheats_permission(bool allowed);
bool cvar_get_cheats_permission(void);

int32_t cvar_get_int_name(const char *name);
bool cvar_get_bool_name(const char *name);
float cvar_get_float_name(const char *name);
const char *cvar_get_string_name(const char *name);

bool cvar_set_int_name(const char *name, int32_t v);
bool cvar_set_bool_name(const char *name, bool v);
bool cvar_set_float_name(const char *name, float v);
bool cvar_set_string_name(const char *name, const char *v);

bool cvar_force_set_int_name(const char *name, int32_t v);
bool cvar_force_set_bool_name(const char *name, bool v);
bool cvar_force_set_float_name(const char *name, float v);
bool cvar_force_set_string_name(const char *name, const char *v);

bool cvar_set_callback_key(sv_cvar_key_t key, cvar_changed_fn fn);
bool cvar_set_callback_name(const char *name, cvar_changed_fn fn);

#endif
