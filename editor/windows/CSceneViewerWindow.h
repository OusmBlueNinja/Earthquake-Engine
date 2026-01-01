#pragma once

#include "editor/windows/CBaseWindow.h"

namespace editor
{
    class CEditorContext;

    class CSceneViewerWindow final : public CBaseWindow
    {
    public:
        CSceneViewerWindow() : CBaseWindow("Scene Viewer") {}

    protected:
        bool BeginImpl() override;
        void EndImpl() override;
        void OnTick(float dt, CEditorContext *ctx) override;
    };
}
