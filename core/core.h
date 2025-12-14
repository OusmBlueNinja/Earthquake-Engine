#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "types/vec2.h"
#include "types/vec2i.h"

#include "managers/window_manager.h"
#include "renderer/renderer.h"

#define ENGINE_V "25.0.1"
#define ENGINE_N "Earthquake"

typedef struct ApplicationSpecification
{
    vec2i window_size;
    bool vsync;
    int argc;
    char **argv;

} ApplicationSpecification;

typedef struct Core
{

} Core;

enum
{
    APP_STATUS_INVALID_APP = 1,
    APP_STATUS_MISMATCHING_APPLICATION,
    APP_STATUS_NOT_INITALIZED,
    APP_STATUS_FAILED_TO_CREATE_WINDOW,
    APP_STATUS_FAILED_TO_INITALIZE_RENDERER,
};
typedef struct Application
{

    ApplicationSpecification *specification;

    Core *core;

    window_manager window_manager;

    renderer_t renderer;

    bool application_initalized;
    int status;
    bool running;

} Application;

Application *create_application(ApplicationSpecification *specification);
void delete_application(Application *app);
void start_application(Application *app);

//! PRIVATE
void loop_application();
