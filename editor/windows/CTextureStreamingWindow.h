#pragma once

#include <stdint.h>
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
    struct texture_stream_hist_sample_t
    {
        uint64_t ms;
        float vram_mb;
        float up_mb;
        float ev_mb;
    };

    class CTextureStreamingWindow final : public CBaseWindow
    {
    public:
        CTextureStreamingWindow() : CBaseWindow("Texture Streaming")
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
        bool m_ShowOnlyResident = true;
        bool m_ShowOnlyPending = false;

        float m_ThumbSize = 96.0f;
        bool m_GridShowNames = true;
        ihandle_t m_Selected = {};
        int m_SelectedPreviewMip = -1; // -1 => current resident top mip

        // Per-frame history ring (keeps ~60 seconds of samples; capped).
        std::vector<texture_stream_hist_sample_t> m_Hist;
        size_t m_HistCap = 60000; // enough for 1000 fps over 60s
        size_t m_HistHead = 0;
        size_t m_HistCount = 0;

        std::vector<asset_debug_slot_t> m_Slots;
        asset_manager_debug_snapshot_t m_Snapshot = {};
    };
}
