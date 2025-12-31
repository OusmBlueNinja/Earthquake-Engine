#pragma once
#include <stdint.h>

typedef struct frame_graph_t frame_graph_t;

typedef struct fg_handle_t
{
    uint16_t id; // 0 = invalid
} fg_handle_t;

static inline fg_handle_t fg_handle_invalid(void) { return (fg_handle_t){0}; }
static inline int fg_handle_is_valid(fg_handle_t h) { return h.id != 0u; }

typedef enum fg_resource_type_t
{
    FG_RESOURCE_VIRTUAL = 0,
    FG_RESOURCE_TEXTURE2D,
    FG_RESOURCE_TEXTURE2D_ARRAY,
    FG_RESOURCE_BUFFER
} fg_resource_type_t;

typedef enum fg_access_t
{
    FG_ACCESS_READ = 0,
    FG_ACCESS_WRITE = 1
} fg_access_t;

typedef void (*fg_execute_fn)(void *user);

frame_graph_t *fg_create(void);
void fg_destroy(frame_graph_t *fg);

void fg_begin(frame_graph_t *fg);

fg_handle_t fg_create_virtual(frame_graph_t *fg, const char *name);
fg_handle_t fg_import_texture2d(frame_graph_t *fg, const char *name, uint32_t gl_tex);
fg_handle_t fg_import_texture2d_array(frame_graph_t *fg, const char *name, uint32_t gl_tex);
fg_handle_t fg_import_buffer(frame_graph_t *fg, const char *name, uint32_t gl_buf);

uint32_t fg_imported_gl_id(const frame_graph_t *fg, fg_handle_t h);
const char *fg_resource_name(const frame_graph_t *fg, fg_handle_t h);
fg_resource_type_t fg_resource_type(const frame_graph_t *fg, fg_handle_t h);

uint16_t fg_add_pass(frame_graph_t *fg, const char *name, fg_execute_fn execute, void *user);
void fg_pass_use(frame_graph_t *fg, uint16_t pass_id, fg_handle_t res, fg_access_t access);

int fg_compile(frame_graph_t *fg);
int fg_execute(frame_graph_t *fg);

