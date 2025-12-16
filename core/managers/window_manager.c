#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "window_manager.h"
#include <stdio.h>

#include "utils/logger.h"

static void wm_error_callback(int error, const char *description)
{
    LOG_ERROR("GLFW error %d: %s", error, description);
}

int wm_init(window_manager *wm)
{
    if (!wm)
        return 1;

    if (wm->size.x <= 0)
        wm->size.x = WM_DEFAULT_WIDTH;
    if (wm->size.y <= 0)
        wm->size.y = WM_DEFAULT_HEIGHT;
    if (!wm->title)
        wm->title = "Earthquake Engine: Invalid Title";

    glfwSetErrorCallback(wm_error_callback);

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    wm->window = glfwCreateWindow(wm->size.x, wm->size.y, wm->title, NULL, NULL);
    if (!wm->window) {
        LOG_ERROR("Failed to create window");
        glfwTerminate();
        return 3;
    }
    glfwMakeContextCurrent(wm->window);

    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if (err != GLEW_OK)
        LOG_ERROR("GLEW init failed: %s\n", glewGetErrorString(err));


    if (!GLEW_ARB_framebuffer_object) {
        LOG_ERROR("Framebuffer objects not supported!\n");
    }

    glfwSwapInterval(0);
    LOG_OK("Created Window");
    return 0;
}

void wm_shutdown(window_manager *wm)
{
    if (!wm)
        return;

    LOG_INFO("Shutting Down Window Manager");

    if (wm->window) {
        glfwDestroyWindow(wm->window);
        wm->window = nullptr;
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
    (void) wm;
    glfwPollEvents();
}

void wm_swap_buffers(window_manager *wm)
{
    if (wm && wm->window)
        glfwSwapBuffers(wm->window);
}

void wm_set_vsync(window_manager *wm, int state)
{
    if (wm && wm->window) {
        glfwSwapInterval(state);
        wm->vsync = state;
    }
}

void wm_set_title(window_manager *wm, const char *title)
{
    if (wm && wm->window && title) {
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
    if (wm && wm->window) {
        glfwGetFramebufferSize(wm->window, &size.x, &size.y);
    }
    return size;
}
