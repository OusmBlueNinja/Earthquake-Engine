#include "renderer.h"
#include "utils/logger.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

int R_init(renderer_t *r)
{
    if (!r)
        return 1;

    r->clear_color = (vec4){.r = 0.05f, .g = 0.05f, .b = 0.06f, .a = 1.0f};
    r->fb_size = (vec2i){1, 1};

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    return 0;
}

void R_shutdown(renderer_t *r)
{
    LOG_INFO("Shutting down Renderer");
    (void)r;
}

void R_set_clear_color(renderer_t *r, vec4 color)
{
    if (!r)
        return;
    r->clear_color = color;
}

void R_begin_frame(renderer_t *r, vec2i fb_size)
{
    if (!r)
        return;

    if (fb_size.x < 1)
        fb_size.x = 1;
    if (fb_size.y < 1)
        fb_size.y = 1;

    r->fb_size = fb_size;

    glViewport(0, 0, (int)r->fb_size.x, (int)r->fb_size.y);
    glClearColor(r->clear_color.r, r->clear_color.g, r->clear_color.b, r->clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void R_end_frame(renderer_t *r)
{
    (void)r;
}
