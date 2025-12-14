#include "core.h"
#include "utils/hsv_to_rgb.h"

Application g_application;

Application *create_application(ApplicationSpecification *specification)
{

    if (!specification)
        return NULL;

    printf("Creating Application\n");

    g_application.application_initalized = true;

    g_application.specification = specification;

    g_application.status = 0;
    g_application.running = false;

    g_application.window_manager.size = specification->window_size;
    g_application.window_manager.vsync = specification->vsync;

    g_application.window_manager.title = ENGINE_N " | v" ENGINE_V;

    return &g_application;
}

void delete_application(Application *app)
{
    if (!app)
    {
        g_application.status = APP_STATUS_INVALID_APP;
        return;
    }

    g_application.application_initalized = false;

    wm_shutdown(&g_application.window_manager);

    printf("Shutdown Application\n");

    printf("Application Shutdown with status: '%d'", g_application.status);
}

void start_application(Application *app)
{
    if (!app)
    {
        g_application.status = APP_STATUS_INVALID_APP;
        return;
    }

    if (app != &g_application)
    {
        g_application.status = APP_STATUS_MISMATCHING_APPLICATION;
        return;
    }

    if (!g_application.application_initalized)
    {
        g_application.status = APP_STATUS_NOT_INITALIZED;
        return;
    }

    printf("Starting Application\n");

    if (wm_init(&g_application.window_manager))
    {
        g_application.status = APP_STATUS_FAILED_TO_CREATE_WINDOW;
        return;
    }

    if (R_init(&g_application.renderer) != 0)
    {
        g_application.status = APP_STATUS_FAILED_TO_INITALIZE_RENDERER;
        return;
    }

    R_set_clear_color(&g_application.renderer, (vec4){
                                                   255.0f / 255.0f,
                                                   80.0f / 255.0f,
                                                   200.0f / 255.0f,
                                                   255.0f / 255.0f,
                                               });

    g_application.running = true;
    loop_application();
}

void loop_application(void)
{
    double last_frame = wm_get_time();
    double accum = 0.0;

    while (!wm_should_close(&g_application.window_manager))
    {
        double now = wm_get_time();
        double dt = now - last_frame;
        last_frame = now;

        accum += dt;
        if (accum >= 1.0)
        {
            printf("dt: %.6f\n", dt);
            accum = 0.0;
        }

        float t = (float)now;
        float hue = fmodf(t * 0.10f, 1.0f); // 0.20 Hz => 5s per cycle
        vec4 color = hsv_to_rgb(hue, 1.0f, 1.0f, 1.0f);
        R_set_clear_color(&g_application.renderer, color);

        wm_poll(&g_application.window_manager);

        R_begin_frame(&g_application.renderer, g_application.window_manager.size);
        R_end_frame(&g_application.renderer);

        wm_swap_buffers(&g_application.window_manager);
    }
}
