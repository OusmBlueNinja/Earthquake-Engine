#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "window_manager.h"
#include "utils/logger.h"
#include "cvar.h"
#include "core.h"
#include "event.h"

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

static void wm_dispatch(event_t *e)
{
    Application *app = get_application();
    if (!app || !e)
        return;
    app_dispatch_event(app, e);
}

static void wm_glfw_framebuffer_size(GLFWwindow *window, int width, int height)
{
    (void)window;
    event_t e = {0};
    e.type = EV_WINDOW_RESIZE;
    e.as.window_resize.w = width;
    e.as.window_resize.h = height;
    wm_dispatch(&e);
}

static void wm_glfw_window_close(GLFWwindow *window)
{
    (void)window;
    event_t e = {0};
    e.type = EV_WINDOW_CLOSE;
    wm_dispatch(&e);
}

static void wm_glfw_cursor_pos(GLFWwindow *window, double x, double y)
{
    (void)window;
    event_t e = {0};
    e.type = EV_MOUSE_MOVE;
    e.as.mouse_move.x = x;
    e.as.mouse_move.y = y;
    wm_dispatch(&e);
}

static void wm_glfw_mouse_button(GLFWwindow *window, int button, int action, int mods)
{
    (void)window;

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    event_t e = {0};
    e.type = (action == GLFW_PRESS) ? EV_MOUSE_BUTTON_DOWN : EV_MOUSE_BUTTON_UP;
    e.as.mouse_button.button = button;
    e.as.mouse_button.mods = mods;
    e.as.mouse_button.x = x;
    e.as.mouse_button.y = y;
    wm_dispatch(&e);
}

static void wm_glfw_scroll(GLFWwindow *window, double dx, double dy)
{
    (void)window;
    event_t e = {0};
    e.type = EV_MOUSE_SCROLL;
    e.as.mouse_scroll.dx = dx;
    e.as.mouse_scroll.dy = dy;
    wm_dispatch(&e);
}

static void wm_glfw_key(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)window;
    event_t e = {0};
    e.type = (action == GLFW_PRESS || action == GLFW_REPEAT) ? EV_KEY_DOWN : EV_KEY_UP;
    e.as.key.key = key;
    e.as.key.scancode = scancode;
    e.as.key.mods = mods;
    e.as.key.repeat = (action == GLFW_REPEAT);
    wm_dispatch(&e);
}

static void wm_glfw_char(GLFWwindow *window, unsigned int codepoint)
{
    (void)window;
    event_t e = {0};
    e.type = EV_CHAR;
    e.as.ch.codepoint = codepoint;
    wm_dispatch(&e);
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

    glfwSetFramebufferSizeCallback(wm->window, wm_glfw_framebuffer_size);
    glfwSetWindowCloseCallback(wm->window, wm_glfw_window_close);
    glfwSetCursorPosCallback(wm->window, wm_glfw_cursor_pos);
    glfwSetMouseButtonCallback(wm->window, wm_glfw_mouse_button);
    glfwSetScrollCallback(wm->window, wm_glfw_scroll);
    glfwSetKeyCallback(wm->window, wm_glfw_key);
    glfwSetCharCallback(wm->window, wm_glfw_char);

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
