#pragma once

extern "C"
{
#include "core/core.h"
}


namespace editor
{
    struct CEditorContext
    {
        Application *app = nullptr;
        renderer_t *renderer = nullptr;
        asset_manager_t *assets = nullptr;

        float dt = 0.0f;
        float fps = 0.0f;
    };
}
