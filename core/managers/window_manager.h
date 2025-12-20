#pragma once
#include <stdbool.h>
#include "types/vec2i.h"

#define WM_DEFAULT_WIDTH 1280
#define WM_DEFAULT_HEIGHT 720

typedef struct GLFWwindow GLFWwindow;

typedef struct window_manager
{
    GLFWwindow *window;
    vec2i size;
    const char *title;
    int vsync;

    unsigned int fbo;
    vec2i fbo_size;
} window_manager;

enum
{
    WM_VSYNC_OFF = 0,
    WM_VSYNC_ON = 1,
    WM_VSYNC_MAX
};

int wm_init(window_manager *wm);
void wm_shutdown(window_manager *wm);

bool wm_should_close(const window_manager *wm);
void wm_poll(window_manager *wm);
void wm_swap_buffers(window_manager *wm);

void wm_set_vsync(window_manager *wm, int state);
void wm_set_title(window_manager *wm, const char *title);

double wm_get_time(void);
vec2i wm_get_framebuffer_size(window_manager *wm);

void wm_bind_framebuffer(window_manager *wm, unsigned int framebuffer, vec2i fb_size);
void wm_begin_frame(window_manager *wm);
void wm_end_frame(window_manager *wm);
