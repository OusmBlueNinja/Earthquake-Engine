/* core/ui/ui_events.h */
#pragma once
#include <stdint.h>
#include "ui_types.h"

struct ui_ctx_t;

typedef enum ui_event_type_t
{
    UI_EV_NONE = 0,

    UI_EV_KEY_DOWN,
    UI_EV_KEY_UP,
    UI_EV_CHAR,

    UI_EV_MOUSE_MOVE,
    UI_EV_MOUSE_BUTTON_DOWN,
    UI_EV_MOUSE_BUTTON_UP,
    UI_EV_MOUSE_SCROLL
} ui_event_type_t;

typedef struct ui_event_t
{
    ui_event_type_t type;

    uint32_t key;
    uint8_t repeat;
    uint8_t mods;

    uint32_t codepoint;

    ui_vec2_t mouse_pos;
    uint8_t button;

    ui_vec2_t scroll;
} ui_event_t;

