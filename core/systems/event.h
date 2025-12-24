#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum event_type_t
{
    EV_NONE = 0,

    EV_WINDOW_RESIZE,
    EV_WINDOW_CLOSE,

    EV_KEY_DOWN,
    EV_KEY_UP,
    EV_CHAR,

    EV_MOUSE_MOVE,
    EV_MOUSE_BUTTON_DOWN,
    EV_MOUSE_BUTTON_UP,
    EV_MOUSE_SCROLL
} event_type_t;

typedef struct event_window_resize_t
{
    int w, h;
} event_window_resize_t;
typedef struct event_key_t
{
    int key, scancode, mods, repeat;
} event_key_t;
typedef struct event_char_t
{
    uint32_t codepoint;
} event_char_t;
typedef struct event_mouse_move_t
{
    double x, y;
} event_mouse_move_t;
typedef struct event_mouse_button_t
{
    int button, mods;
    double x, y;
} event_mouse_button_t;
typedef struct event_mouse_scroll_t
{
    double dx, dy;
} event_mouse_scroll_t;

typedef struct event_t
{
    event_type_t type;
    bool handled;

    union
    {
        event_window_resize_t window_resize;
        event_key_t key;
        event_char_t ch;
        event_mouse_move_t mouse_move;
        event_mouse_button_t mouse_button;
        event_mouse_scroll_t mouse_scroll;
    } as;
} event_t;
