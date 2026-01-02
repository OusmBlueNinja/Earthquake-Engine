#pragma once

extern "C"
{
#include "core/core.h"
#include "core/systems/ecs/ecs_types.h"
}

namespace editor
{
    class CEditorSceneManager;

    struct CEditorContext
    {
        Application *app = nullptr;
        renderer_t *renderer = nullptr;
        asset_manager_t *assets = nullptr;
        CEditorSceneManager *scene = nullptr;

        ecs_entity_t selected_entity = 0;

        float dt = 0.0f;
        float fps = 0.0f;
    };
}
