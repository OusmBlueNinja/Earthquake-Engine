#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "types/vec2.h"
#include "types/vec2i.h"

#include "managers/window_manager.h"
#include "managers/cvar.h"

#include "renderer/renderer.h"

#include "net/server.h"

#include "utils/logger.h"

#define ENGINE_V "25.0.1b"
#define ENGINE_N "Earthquake"

typedef struct ApplicationSpecification
{
    vec2i window_size;
    bool vsync;
    int argc;
    char **argv;
    bool terminal_colors;

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

    renderer_t renderer;

    bool application_initalized;
    app_status_t status;
    bool running;

} Application;

ApplicationSpecification create_specification();

Application *create_application(ApplicationSpecification *specification);
void delete_application(Application *app);

/* This starts the application and initalizes it */
void init_application(Application *app);

//! PRIVATE
void loop_application();
