#pragma once
#include <stdio.h>
#include "types/vec2.h"
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
} window_manager;

int wm_init(window_manager *wm);

void wm_shutdown(window_manager *wm);

bool wm_should_close(const window_manager *wm);

void wm_poll(window_manager *wm);

void wm_swap_buffers(window_manager *wm);

void wm_set_vsync(window_manager *wm, int state);

void wm_set_title(window_manager *wm, const char *title);

double wm_get_time(void);

vec2i wm_get_framebuffer_size(window_manager *wm);
