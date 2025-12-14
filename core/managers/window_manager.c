#include <GLFW/glfw3.h>
#include "window_manager.h"
#include <stdio.h>

static void wm_error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int wm_init(window_manager *wm)
{
    if (!wm)
        return 1;

    if (wm->size.x <= 0)
        wm->size.x = 800;
    if (wm->size.y <= 0)
        wm->size.y = 600;
    if (!wm->title)
        wm->title = "GLFW Window";

    glfwSetErrorCallback(wm_error_callback);

    if (!glfwInit())
    {
        printf("Failed to initialize GLFW\n");
        return 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    wm->window = glfwCreateWindow(wm->size.x, wm->size.y, wm->title, NULL, NULL);
    if (!wm->window)
    {
        printf("Failed to create GLFW window\n");
        glfwTerminate();
        return 3;
    }

    glfwMakeContextCurrent(wm->window);
    glfwSwapInterval(0);
    printf("Created Window\n");
    return 0;
}

void wm_shutdown(window_manager *wm)
{
    if (!wm)
        return;

    if (wm->window)
    {
        glfwDestroyWindow(wm->window);
        wm->window = NULL;
    }
    glfwTerminate();
}

bool wm_should_close(const window_manager *wm)
{
    if (!wm || !wm->window)
        return true;
    return glfwWindowShouldClose(wm->window);
}

void wm_poll(window_manager *wm)
{
    (void)wm;
    glfwPollEvents();
}

void wm_swap_buffers(window_manager *wm)
{
    if (wm && wm->window)
        glfwSwapBuffers(wm->window);
}

void wm_set_vsync(window_manager *wm, int state)
{
    if (wm && wm->window)

    {
        glfwSwapInterval(state);
        wm->vsync = state;
    }
}

void wm_set_title(window_manager *wm, const char *title)
{
    if (wm && wm->window && title)
    {
        wm->title = title;
        glfwSetWindowTitle(wm->window, title);
    }
}

double wm_get_time(void)
{
    return glfwGetTime();
}

vec2i wm_get_framebuffer_size(window_manager *wm)
{
    vec2i size;
    if (wm && wm->window)
    {
        glfwGetFramebufferSize(wm->window, &size.x, &size.y);
    }
}