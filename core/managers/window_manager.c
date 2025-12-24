#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "window_manager.h"
#include "utils/logger.h"
#include "cvar.h"
#include "core.h"

static void wm_error_callback(int error, const char *description)
{
    LOG_ERROR("GLFW error %d: %s", error, description);
}

static void wm_on_vsync_change(sv_cvar_key_t key, const void *old_state, const void *state)
{
    (void)key;
    bool oldb = *(const bool *)old_state;
    bool newb = *(const bool *)state;
    LOG_DEBUG("cl_vsync: %d -> %d", oldb, newb);
    wm_set_vsync(&get_application()->window_manager, newb);
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

    wm->fbo = 0;
    wm->fbo_size = (vec2i){0, 0};

    glfwSetErrorCallback(wm_error_callback);

    if (!glfwInit())
    {
        LOG_ERROR("Failed to initialize GLFW");
        return 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    wm->window = glfwCreateWindow(wm->size.x, wm->size.y, wm->title, NULL, NULL);
    if (!wm->window)
    {
        LOG_ERROR("Failed to create window");
        glfwTerminate();
        return 3;
    }

    glfwMakeContextCurrent(wm->window);

    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if (err != GLEW_OK)
        LOG_ERROR("GLEW init failed: %s", glewGetErrorString(err));

    wm_set_vsync(wm, cvar_get_bool_name("cl_vsync"));
    cvar_set_callback_name("cl_vsync", wm_on_vsync_change);

    vec2i fb = wm_get_framebuffer_size(wm);
    glViewport(0, 0, fb.x, fb.y);

    LOG_DEBUG("GL: %s", (const char *)glGetString(GL_VERSION));
    LOG_DEBUG("GLSL: %s", (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));

    LOG_OK("Created Window");
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
    if (!wm || !wm->window)
        return;

    if (state < 0 || state >= WM_VSYNC_MAX)
        state = WM_VSYNC_ON;

    glfwSwapInterval(state);
    wm->vsync = state;
}

void wm_set_title(window_manager *wm, const char *title)
{
    if (!wm || !wm->window || !title)
        return;

    wm->title = title;
    glfwSetWindowTitle(wm->window, title);
}

double wm_get_time(void)
{
    return glfwGetTime();
}

vec2i wm_get_framebuffer_size(window_manager *wm)
{
    vec2i size = {0, 0};
    if (wm && wm->window)
        glfwGetFramebufferSize(wm->window, &size.x, &size.y);
    return size;
}

void wm_bind_framebuffer(window_manager *wm, unsigned int framebuffer, vec2i fb_size)
{
    if (!wm)
        return;

    wm->fbo = framebuffer;
    wm->fbo_size = fb_size;
}

void wm_begin_frame(window_manager *wm)
{
    if (!wm || !wm->window)
        return;

    vec2i win_fb = wm_get_framebuffer_size(wm);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_fb.x, win_fb.y);
}

void wm_end_frame(window_manager *wm)
{
    if (!wm || !wm->window)
        return;

    vec2i win_fb = wm_get_framebuffer_size(wm);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, win_fb.x, win_fb.y);

    glDisable(GL_SCISSOR_TEST);

    if (wm->fbo != 0)
    {
        int src_w = wm->fbo_size.x;
        int src_h = wm->fbo_size.y;

        if (src_w <= 0 || src_h <= 0)
        {
            src_w = win_fb.x;
            src_h = win_fb.y;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, wm->fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        glBlitFramebuffer(
            0, 0, src_w, src_h,
            0, 0, win_fb.x, win_fb.y,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    glfwSwapBuffers(wm->window);

    wm->fbo = 0;
    wm->fbo_size = (vec2i){0, 0};
}
