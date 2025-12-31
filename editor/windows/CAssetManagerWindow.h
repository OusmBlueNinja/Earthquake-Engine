#pragma once

#include <vector>

#include "imgui.h"
#include "editor/CEditorContext.h"
#include "editor/windows/CBaseWindow.h"

extern "C"
{
#include "managers/asset_manager/asset_manager.h"
}

namespace editor
{
    class CAssetManagerWindow final : public CBaseWindow
    {
    public:
        CAssetManagerWindow() : CBaseWindow("Asset Manager")
        {
            m_Open = false;
        }

    protected:
        bool BeginImpl() override
        {
            return ImGui::Begin(m_Name, &m_Open);
        }

        void EndImpl() override
        {
            ImGui::End();
        }

        void OnTick(float, CEditorContext *ctx) override;

    private:
        ImGuiTextFilter m_Filter;
        bool m_ShowEmpty = false;
        bool m_ShowFailed = true;

        std::vector<asset_debug_slot_t> m_Slots;
        asset_manager_debug_snapshot_t m_Snapshot = {};
    };
}

