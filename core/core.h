#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "layer.h"

#include "systems/event.h"

#include "types/vec2.h"
#include "types/vec2i.h"
#include "types/vector.h"
#include "types/vec3.h"

#include "managers/window_manager.h"
#include "managers/cvar.h"
#include "managers/asset_manager/asset_manager.h"

#include "renderer/renderer.h"

#include "net/server.h"

#include "utils/logger.h"
#include "utils/hsv_to_rgb.h"
#include "utils/macros.h"
#include "utils/threads.h"

#define ENGINE_V "25.0.2b"
#define ENGINE_N "Earthquake"

typedef struct ApplicationSpecification
{
    vec2i window_size;
    bool vsync;
    int argc;
    char **argv;
    bool terminal_colors;
    int am_max_inflight_jobs;

} ApplicationSpecification;

typedef enum
{
    APP_STATUS_INVALID_APP = 1,
    APP_STATUS_MISMATCHING_APPLICATION,
    APP_STATUS_NOT_INITALIZED,
    APP_STATUS_FAILED_TO_CREATE_WINDOW,
    APP_STATUS_FAILED_TO_INITALIZE_RENDERER,
    APP_STATUS_FAILED_TO_INIT_CVARS,
    APP_STATUS_FAILED_TO_INIT_SV,
} app_status_t;

typedef struct Application
{

    ApplicationSpecification *specification;

    window_manager window_manager;
    asset_manager_t asset_manager;

    renderer_t renderer;

    bool application_initalized;
    app_status_t status;
    bool running;

    vector_t layers;

} Application;

ApplicationSpecification create_specification();

Application *create_application(ApplicationSpecification *specification);
void delete_application(Application *app);

/* This starts the application and initalizes it */
void init_application(Application *app);

layer_t create_layer(const char *name);
uint32_t push_layer(layer_t layer);

void app_dispatch_event(Application *app, event_t *e);

Application *get_application();
renderer_t *get_renderer();

//! PRIVATE
void loop_application();

const char *app_status_to_string(app_status_t status);
