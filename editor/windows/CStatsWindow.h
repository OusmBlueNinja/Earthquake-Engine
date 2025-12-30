#pragma once

#include "imgui.h"
#include "editor/CEditorContext.h"
#include "editor/windows/CBaseWindow.h"

namespace editor
{
    class CStatsWindow final : public CBaseWindow
    {
    public:
        CStatsWindow() : CBaseWindow("Stats") {}

    protected:
        bool BeginImpl() override
        {
            return ImGui::Begin(m_Name, &m_Open);
        }

        void EndImpl() override
        {
            ImGui::End();
        }

        void OnTick(float, CEditorContext* ctx) override;
    };
}
