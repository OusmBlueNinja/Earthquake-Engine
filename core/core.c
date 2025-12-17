#include "core.h"

Application g_application;

ApplicationSpecification create_specification()
{
    ApplicationSpecification spec;
    spec.window_size = (vec2i){1270, 720};
    spec.vsync = WM_VSYNC_ON;
    spec.argc = 0;
    spec.argv = NULL;
    spec.terminal_colors = true;

    return spec;
}

Application *create_application(ApplicationSpecification *specification)
{
    if (!specification)
        return NULL;

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

    R_shutdown(&g_application.renderer);

    wm_shutdown(&g_application.window_manager);

    sv_shutdown();

    cvar_shutdown();

    LOG_INFO("Shutdown Application");
    if (!g_application.status)
        LOG_OK("status: %d", g_application.status);
    else
        LOG_ERROR("error: %d", g_application.status);
}

void init_application(Application *app)
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

    log_enable_colors(g_application.specification->terminal_colors);

    LOG_INFO("Starting Application");

    if (wm_init(&g_application.window_manager))
    {
        g_application.status = APP_STATUS_FAILED_TO_CREATE_WINDOW;
        return;
    }

    if (R_init(&g_application.renderer))
    {
        g_application.status = APP_STATUS_FAILED_TO_INITALIZE_RENDERER;
        return;
    }

    if (cvar_init())
    {
        g_application.status = APP_STATUS_FAILED_TO_INIT_CVARS;
        return;
    }

    if (sv_init())
    {
        g_application.status = APP_STATUS_FAILED_TO_INIT_SV;
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
    sv_start();

    while (!wm_should_close(&g_application.window_manager))
    {
        const double now = wm_get_time();
        const double dt = now - last_frame;
        last_frame = now;

        accum += dt;
        if (accum >= 1.0)
        {
            LOG_DEBUG("dt: %.6f fps: %.1f", dt, 1.0f / dt);
            accum = 0.0;
        }

        const float t = (float)now;
        const float hue = fmodf(t * 0.05f, 1.0f);
        const vec4 color = hsv_to_rgb(hue, 1.0f, 1.0f, 1.0f);
        R_set_clear_color(&g_application.renderer, color);

        wm_poll(&g_application.window_manager);
        R_resize(&g_application.renderer, g_application.window_manager.size);
        R_begin_frame(&g_application.renderer);
        R_end_frame(&g_application.renderer);

        wm_begin_frame(&g_application.window_manager);
        wm_bind_framebuffer(&g_application.window_manager, R_get_color_texture(&g_application.renderer));
        wm_end_frame(&g_application.window_manager);
    }
    sv_stop();
}
