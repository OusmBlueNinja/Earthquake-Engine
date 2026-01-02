#include "editor/windows/CTextureStreamingWindow.h"

#include <stdint.h>
#include <inttypes.h>
#include <float.h>
#include <string.h>

static float bytes_to_mb(uint64_t bytes)
{
    return (float)((double)bytes / (1024.0 * 1024.0));
}

namespace editor
{
    static void hist_push(std::vector<float> &v, size_t max_n, float x)
    {
        if (max_n == 0)
            return;
        if (v.size() < max_n)
        {
            v.push_back(x);
            return;
        }
        // ring shift for small buffers (max 300 default)
        memmove(v.data(), v.data() + 1, (v.size() - 1) * sizeof(float));
        v[v.size() - 1] = x;
    }

    void CTextureStreamingWindow::OnTick(float, CEditorContext *ctx)
    {
        if (!ctx || !ctx->assets)
        {
            ImGui::TextUnformatted("asset_manager: null");
            return;
        }

        const uint32_t slot_count = asset_manager_debug_get_slot_count(ctx->assets);
        m_Slots.resize(slot_count);
        asset_manager_debug_get_slots(ctx->assets, slot_count ? m_Slots.data() : nullptr, slot_count, &m_Snapshot);

        // History
        hist_push(m_HistVRAM, m_HistMax, bytes_to_mb(m_Snapshot.vram_resident_bytes));
        hist_push(m_HistUp, m_HistMax, bytes_to_mb(m_Snapshot.tex_stream_uploaded_bytes_last_frame));
        hist_push(m_HistEv, m_HistMax, bytes_to_mb(m_Snapshot.tex_stream_evicted_bytes_last_frame));

        ImGui::SeparatorText("Budgets");
        ImGui::Text("vram: %.1f / %.1f MB", (double)bytes_to_mb(m_Snapshot.vram_resident_bytes), (double)bytes_to_mb(m_Snapshot.vram_budget_bytes));
        ImGui::Text("upload_budget: %.2f MB/f", (double)bytes_to_mb(m_Snapshot.tex_stream_upload_budget_bytes_per_frame));
        ImGui::Text("stable_frames: %u  evict_unused_ms: %u", (unsigned)m_Snapshot.tex_stream_stable_frames, (unsigned)m_Snapshot.tex_stream_evict_unused_ms);
        ImGui::Text("last_frame: up %.2f MB (%u)  ev %.2f MB (%u)  pending %u",
                    (double)bytes_to_mb(m_Snapshot.tex_stream_uploaded_bytes_last_frame),
                    (unsigned)m_Snapshot.tex_stream_uploads_last_frame,
                    (double)bytes_to_mb(m_Snapshot.tex_stream_evicted_bytes_last_frame),
                    (unsigned)m_Snapshot.tex_stream_evictions_last_frame,
                    (unsigned)m_Snapshot.tex_stream_pending_uploads);

        ImGui::SeparatorText("History");
        if (!m_HistVRAM.empty())
            ImGui::PlotLines("VRAM (MB)", m_HistVRAM.data(), (int)m_HistVRAM.size(), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 60));
        if (!m_HistUp.empty())
            ImGui::PlotLines("Uploads (MB)", m_HistUp.data(), (int)m_HistUp.size(), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 45));
        if (!m_HistEv.empty())
            ImGui::PlotLines("Evictions (MB)", m_HistEv.data(), (int)m_HistEv.size(), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 45));

        ImGui::SeparatorText("Filters");
        m_Filter.Draw("Filter (path)", 240.0f);
        ImGui::SameLine();
        ImGui::Checkbox("Only Resident", &m_ShowOnlyResident);
        ImGui::SameLine();
        ImGui::Checkbox("Only Pending", &m_ShowOnlyPending);

        const ImGuiTableFlags table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Hideable |
            ImGuiTableFlags_ScrollY;

        if (!ImGui::BeginTable("##tex_stream_table", 10, table_flags, ImVec2(0.0f, 0.0f)))
            return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("VRAM (MB)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Mips", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Res/Tgt", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Forced", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("Prio", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Residency", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Age (s)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < slot_count; ++i)
        {
            const asset_debug_slot_t &s = m_Slots[i];
            if (s.type != ASSET_IMAGE || s.state != ASSET_STATE_READY)
                continue;

            // Only show textures that are actually using VRAM (resident mip bytes > 0).
            if (m_ShowOnlyResident && s.vram_bytes == 0)
                continue;
            if (s.vram_bytes == 0)
                continue;

            const bool pending = (s.img_mip_count != 0) && (s.img_resident_top_mip > s.img_target_top_mip);
            if (m_ShowOnlyPending && !pending)
                continue;

            if (m_Filter.IsActive() && !m_Filter.PassFilter(s.path))
                continue;

            uint64_t age_ms = 0;
            if (s.last_requested_ms && m_Snapshot.now_ms > s.last_requested_ms)
                age_ms = m_Snapshot.now_ms - s.last_requested_ms;

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", (unsigned)s.slot_index);

            ImGui::TableSetColumnIndex(1);
            if (s.vram_bytes)
                ImGui::Text("%.2f", (double)bytes_to_mb(s.vram_bytes));
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(2);
            if (s.img_mip_count)
                ImGui::Text("%u", (unsigned)s.img_mip_count);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(3);
            if (s.img_mip_count)
                ImGui::Text("%u/%u", (unsigned)s.img_resident_top_mip, (unsigned)s.img_target_top_mip);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(4);
            if (s.img_forced)
                ImGui::Text("%u", (unsigned)s.img_forced_top_mip);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%u", (unsigned)s.img_priority);

            ImGui::TableSetColumnIndex(6);
            ImGui::Text("0x%08X%08X", (unsigned)(s.img_residency_mask >> 32), (unsigned)(s.img_residency_mask & 0xFFFFFFFFu));

            ImGui::TableSetColumnIndex(7);
            if (s.last_requested_ms)
                ImGui::Text("%.1f", (double)age_ms / 1000.0);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(8);
            ImGui::TextUnformatted(s.path[0] ? s.path : "-");
        }

        ImGui::EndTable();
    }
}
