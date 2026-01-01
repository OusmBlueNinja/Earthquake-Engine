#pragma once

#include <stdint.h>

#include "editor/windows/CBaseWindow.h"

extern "C"
{
#include "types/handle.h"
}

namespace editor
{
    class CEditorContext;

    typedef void (*inspector_asset_label_fn_t)(CEditorContext *, ihandle_t, char *, uint32_t);

    void EntityInspector_SetAssetLabelFn(inspector_asset_label_fn_t fn);

    class CEntityInspectorWindow final : public CBaseWindow
    {
    public:
        CEntityInspectorWindow();

        void OnTick(float dt, CEditorContext *ctx) override;

    protected:
        bool BeginImpl() override;
        void EndImpl() override;
    };
}
