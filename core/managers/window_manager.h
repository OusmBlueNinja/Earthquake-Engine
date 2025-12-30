#pragma once
#include <stdbool.h>
#include "types/vec2i.h"

#define WM_DEFAULT_WIDTH 1280
#define WM_DEFAULT_HEIGHT 720

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWcursor GLFWcursor;

enum
{
    WM_VSYNC_OFF = 0,
    WM_VSYNC_ON = 1,
    WM_VSYNC_MAX
};

enum
{
    WM_CURSOR_NORMAL = 0,
    WM_CURSOR_HIDDEN = 1,
    WM_CURSOR_DISABLED = 2,
    WM_CURSOR_MAX
};

typedef enum wm_cursor_shape_t
{
    WM_CURSOR_ARROW = 0,
    WM_CURSOR_IBEAM,
    WM_CURSOR_CROSSHAIR,
    WM_CURSOR_HAND,
    WM_CURSOR_HRESIZE,
    WM_CURSOR_VRESIZE,
    WM_CURSOR_DRESIZE_NESW,
    WM_CURSOR_DRESIZE_NWSE,
    WM_CURSOR_SHAPE_MAX
} wm_cursor_shape_t;

typedef struct window_manager
{
    GLFWwindow *window;
    vec2i size;
    const char *title;
    int vsync;

    unsigned int fbo;
    vec2i fbo_size;
    GLFWcursor *cursors[WM_CURSOR_SHAPE_MAX];
} window_manager;

int wm_init(window_manager *wm);
void wm_shutdown(window_manager *wm);

bool wm_should_close(const window_manager *wm);
void wm_poll(window_manager *wm);
void wm_swap_buffers(window_manager *wm);

void wm_set_vsync(window_manager *wm, int state);
void wm_set_title(window_manager *wm, const char *title);
void wm_set_cursor_state(window_manager *wm, int state);
void wm_set_cursor_shape(window_manager *wm, wm_cursor_shape_t shape);

double wm_get_time(void);
vec2i wm_get_framebuffer_size(window_manager *wm);

void wm_bind_framebuffer(window_manager *wm, unsigned int framebuffer, vec2i fb_size);
void wm_begin_frame(window_manager *wm);
void wm_end_frame(window_manager *wm);

GLFWwindow *wm_get_glfw_window(window_manager *wm);
