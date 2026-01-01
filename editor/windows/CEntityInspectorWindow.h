#pragma once

#include "editor/windows/CBaseWindow.h"

namespace editor
{
    class CEditorContext;

    class CEntityInspectorWindow final : public CBaseWindow
    {
    public:
        CEntityInspectorWindow() : CBaseWindow("Entity Inspector") {}

    protected:
        bool BeginImpl() override;
        void EndImpl() override;
        void OnTick(float dt, CEditorContext *ctx) override;
    };
}
