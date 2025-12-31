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
    CL_MSAA,
    CL_MSAA_SAMPLES,
    CL_RENDER_DEBUG,
    CL_CPU_THREADS,
    CL_LOG_LEVEL,

    // Renderer user-facing toggles (should be saved)
    CL_R_SHADOWS,
    CL_AUTO_EXPOSURE,
    CL_AUTO_EXPOSURE_HZ,
    CL_R_RESTORE_GL_STATE,

    // Renderer dev/debug (not intended to be saved)
    CL_R_FORCE_LOD_LEVEL,
    CL_R_WIREFRAME,

    // Asset streaming / VRAM budget
    CL_AM_STREAMING,
    CL_AM_VRAM_BUDGET_MB,
    CL_AM_STREAM_UNUSED_FRAMES,
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
