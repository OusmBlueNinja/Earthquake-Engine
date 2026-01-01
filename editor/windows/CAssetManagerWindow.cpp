#include "editor/windows/CAssetManagerWindow.h"

#include <inttypes.h>
#include <stdint.h>

static const char *asset_state_to_string(asset_state_t st)
{
    switch (st)
    {
    case ASSET_STATE_EMPTY:
        return "Empty";
    case ASSET_STATE_LOADING:
        return "Loading";
    case ASSET_STATE_READY:
        return "Ready";
    case ASSET_STATE_FAILED:
        return "Failed";
    default:
        return "Unknown";
    }
}

static const char *asset_slot_status(const asset_debug_slot_t &s)
{
    if (s.state == ASSET_STATE_READY)
        return "Loaded";
    if (s.inflight)
        return "Queued/Loading";
    if (s.state == ASSET_STATE_FAILED)
        return "Failed";
    if (s.state == ASSET_STATE_EMPTY)
        return "Empty";
    return "Unknown";
}

static float bytes_to_mb(uint64_t bytes)
{
    return (float)((double)bytes / (1024.0 * 1024.0));
}

namespace editor
{
    void CAssetManagerWindow::OnTick(float, CEditorContext *ctx)
    {
        if (!ctx || !ctx->assets)
        {
            ImGui::TextUnformatted("asset_manager: null");
            return;
        }

        const uint32_t slot_count = asset_manager_debug_get_slot_count(ctx->assets);
        m_Slots.resize(slot_count);
        asset_manager_debug_get_slots(ctx->assets, slot_count ? m_Slots.data() : nullptr, slot_count, &m_Snapshot);

        ImGui::SeparatorText("Streaming");
        ImGui::Text("enabled: %u", m_Snapshot.streaming_enabled);
        ImGui::Text("stream_unused_ms: %u", m_Snapshot.stream_unused_ms);
        ImGui::Text("stream_unused_frames: %u", m_Snapshot.stream_unused_frames);
        ImGui::Text("vram: %.1f / %.1f MB", (double)bytes_to_mb(m_Snapshot.vram_resident_bytes), (double)bytes_to_mb(m_Snapshot.vram_budget_bytes));

        ImGui::SeparatorText("Queues");
        ImGui::Text("jobs_pending: %u", m_Snapshot.jobs_pending);
        ImGui::Text("done_pending: %u", m_Snapshot.done_pending);

        ImGui::SeparatorText("Assets");
        ImGui::Text("slots: %u", m_Snapshot.slot_count);
        ImGui::Text("frame_index: %" PRIu64, (uint64_t)m_Snapshot.frame_index);
        ImGui::Text("now_ms: %" PRIu64, (uint64_t)m_Snapshot.now_ms);
        ImGui::Text("unload_scan_index: %u", m_Snapshot.unload_scan_index);

        m_Filter.Draw("Filter (path/type/status)", 240.0f);
        ImGui::SameLine();
        ImGui::Checkbox("Show Empty", &m_ShowEmpty);
        ImGui::SameLine();
        ImGui::Checkbox("Show Failed", &m_ShowFailed);

        const ImGuiTableFlags table_flags =
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Hideable |
            ImGuiTableFlags_ScrollY;

        if (!ImGui::BeginTable("##asset_table", 12, table_flags, ImVec2(0.0f, 0.0f)))
            return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Persistent", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Inflight", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("VRAM (MB)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Age (s)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Remaining (s)", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < slot_count; ++i)
        {
            const asset_debug_slot_t &s = m_Slots[i];

            if (!m_ShowEmpty && s.state == ASSET_STATE_EMPTY)
                continue;
            if (!m_ShowFailed && s.state == ASSET_STATE_FAILED)
                continue;

            const char *type_str = ASSET_TYPE_TO_STRING(s.type);
            const char *status_str = asset_slot_status(s);
            const char *state_str = asset_state_to_string(s.state);

            bool pass = true;
            if (m_Filter.IsActive())
            {
                pass = pass && (m_Filter.PassFilter(s.path) || m_Filter.PassFilter(type_str) || m_Filter.PassFilter(status_str) || m_Filter.PassFilter(state_str));
            }
            if (!pass)
                continue;

            char hbuf[64] = {};
            char pbuf[64] = {};
            handle_hex_triplet(hbuf, s.handle);
            handle_hex_triplet(pbuf, s.persistent);

            uint64_t age_ms = 0;
            if (s.last_requested_ms && m_Snapshot.now_ms > s.last_requested_ms)
                age_ms = m_Snapshot.now_ms - s.last_requested_ms;

            uint64_t remain_ms = 0;
            if (m_Snapshot.streaming_enabled && m_Snapshot.stream_unused_ms)
            {
                if (age_ms < (uint64_t)m_Snapshot.stream_unused_ms)
                    remain_ms = (uint64_t)m_Snapshot.stream_unused_ms - age_ms;
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", s.slot_index);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(hbuf);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(pbuf);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(type_str);

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(status_str);

            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(state_str);

            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%u", (unsigned)s.inflight);

            ImGui::TableSetColumnIndex(7);
            if (s.flags & ASSET_FLAG_NO_UNLOAD)
                ImGui::TextUnformatted("NO_UNLOAD");
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(8);
            if (s.vram_bytes)
                ImGui::Text("%.2f", (double)bytes_to_mb(s.vram_bytes));
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(9);
            if (s.last_requested_ms)
                ImGui::Text("%.1f", (double)age_ms / 1000.0);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(10);
            if (s.last_requested_ms && m_Snapshot.streaming_enabled && m_Snapshot.stream_unused_ms)
                ImGui::Text("%.1f", (double)remain_ms / 1000.0);
            else
                ImGui::TextUnformatted("-");

            ImGui::TableSetColumnIndex(11);
            ImGui::TextUnformatted(s.path[0] ? s.path : "-");
        }

        ImGui::EndTable();
    }
}

