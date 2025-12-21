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

    g_application.layers = create_vector(layer_t);

    return &g_application;
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

    if (cvar_init())
    {
        g_application.status = APP_STATUS_FAILED_TO_INIT_CVARS;
        return;
    }

    if (wm_init(&g_application.window_manager))
    {
        g_application.status = APP_STATUS_FAILED_TO_CREATE_WINDOW;
        return;
    }

    if (R_init(&g_application.renderer, &g_application.asset_manager))
    {
        g_application.status = APP_STATUS_FAILED_TO_INITALIZE_RENDERER;
        return;
    }

    if (sv_init())
    {
        g_application.status = APP_STATUS_FAILED_TO_INIT_SV;
        return;
    }

    if (!cvar_load("./config.cfg"))
    {
        LOG_WARN("Faild to load Config.");
    }

    asset_manager_desc_t desc = {0};
    desc.worker_count = threads_get_cpu_logical_count();
    desc.max_inflight_jobs = g_application.specification->am_max_inflight_jobs;
    desc.handle_type = iHANDLE_TYPE_ASSET;

    asset_manager_init(&g_application.asset_manager, &desc);

    {
        // Init Layers
        layer_t *layer;
        VECTOR_FOR_EACH(g_application.layers, layer_t, layer)
        {
            if (layer->init)
            {
                LOG_INFO("Initializing Layer: '%s'", layer->name);
                layer->init(layer);
            }
        }
    }

    g_application.running = true;
    loop_application();
}

void delete_application(Application *app)
{
    if (!app)
    {
        g_application.status = APP_STATUS_INVALID_APP;
        return;
    }
    {
        // shutdown Layers
        layer_t *layer;
        VECTOR_FOR_EACH(g_application.layers, layer_t, layer)
        {
            if (layer->shutdown)
            {
                LOG_INFO("Shutting Down Layer: '%s'", layer->name);

                layer->shutdown(layer);
            }
        }
    }

    if (!cvar_save("./config.cfg"))
    {
        LOG_WARN("Faild to save Config.");
    }

    cvar_shutdown(); // has to shutdown first, calls cvar_changed_fn

    g_application.application_initalized = false;

    asset_manager_shutdown(&g_application.asset_manager);

    R_shutdown(&g_application.renderer);

    wm_shutdown(&g_application.window_manager);

    sv_shutdown();

    LOG_INFO("Shutdown Application");
    if (!g_application.status)
        LOG_OK("status: %d", g_application.status);
    else
        LOG_ERROR("error: '%s' (%d)", app_status_to_string(g_application.status), g_application.status);
}


void loop_application(void)
{
    double last_frame = wm_get_time();
    sv_start();

    while (!wm_should_close(&g_application.window_manager))
    {
        const double now = wm_get_time();
        const double dt = now - last_frame;
        last_frame = now;

        wm_poll(&g_application.window_manager);

        layer_t *layer;
        VECTOR_FOR_EACH(g_application.layers, layer_t, layer)
        {
            if (layer->update)
                layer->update(layer, (float)dt);
        }

        asset_manager_pump(&g_application.asset_manager);

        R_resize(&g_application.renderer, wm_get_framebuffer_size(&g_application.window_manager));

        R_begin_frame(&g_application.renderer);
        {
            VECTOR_FOR_EACH(g_application.layers, layer_t, layer)
            {
                if (layer->draw)
                    layer->draw(layer);
            }
        }
        R_end_frame(&g_application.renderer);

        wm_bind_framebuffer(&g_application.window_manager, R_get_final_fbo(&g_application.renderer), g_application.renderer.fb_size);
        wm_begin_frame(&g_application.window_manager);
        wm_end_frame(&g_application.window_manager);
    }

    sv_stop();
}

uint32_t push_layer(layer_t layer)
{
    layer.id = g_application.layers.size;

    layer.app = &g_application;
    vector_push_back(&g_application.layers, &layer);
    return layer.id;
}

Application *get_application()
{
    return &g_application;
}

const char *app_status_to_string(app_status_t status)
{
    switch (status)
    {
    case APP_STATUS_INVALID_APP:
        return "APP_STATUS_INVALID_APP";
    case APP_STATUS_MISMATCHING_APPLICATION:
        return "APP_STATUS_MISMATCHING_APPLICATION";
    case APP_STATUS_NOT_INITALIZED:
        return "APP_STATUS_NOT_INITALIZED";
    case APP_STATUS_FAILED_TO_CREATE_WINDOW:
        return "APP_STATUS_FAILED_TO_CREATE_WINDOW";
    case APP_STATUS_FAILED_TO_INITALIZE_RENDERER:
        return "APP_STATUS_FAILED_TO_INITALIZE_RENDERER";
    case APP_STATUS_FAILED_TO_INIT_CVARS:
        return "APP_STATUS_FAILED_TO_INIT_CVARS";
    case APP_STATUS_FAILED_TO_INIT_SV:
        return "APP_STATUS_FAILED_TO_INIT_SV";
    default:
        return "UNKNOWN_APP_STATUS";
    }
}
