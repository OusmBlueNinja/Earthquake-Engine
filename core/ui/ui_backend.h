#pragma once
#include <stdint.h>
#include "ui_types.h"
#include "ui_commands.h"

struct ui_ctx_t;

typedef int (*ui_text_width_fn)(void *user, uint32_t font_id, const char *text, int len);
typedef float (*ui_text_height_fn)(void *user, uint32_t font_id);

typedef struct ui_base_backend_t
{
    void *user;

    ui_text_width_fn text_width;
    ui_text_height_fn text_height;

    void (*begin)(struct ui_base_backend_t *b, int fb_w, int fb_h);
    void (*render)(struct ui_base_backend_t *b, const ui_cmd_t *cmds, uint32_t count);
    void (*end)(struct ui_base_backend_t *b);
} ui_base_backend_t;
