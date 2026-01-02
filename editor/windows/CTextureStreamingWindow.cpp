#include "editor/windows/CTextureStreamingWindow.h"

#include <GL/glew.h>

#include <stdint.h>
#include <inttypes.h>
#include <float.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>

static float bytes_to_mb(uint64_t bytes)
{
    return (float)((double)bytes / (1024.0 * 1024.0));
}

namespace editor
{
    static const char *path_basename(const char *path)
    {
        if (!path || !path[0])
            return "-";
        const char *slash = strrchr(path, '/');
        const char *bslash = strrchr(path, '\\');
        const char *p = slash;
        if (!p || (bslash && bslash > p))
            p = bslash;
        return p ? (p + 1) : path;
    }

    static void hist_push_ring(std::vector<texture_stream_hist_sample_t> &v,
                               size_t &head,
                               size_t &count,
                               size_t cap,
                               texture_stream_hist_sample_t x)
    {
        if (cap == 0)
            return;
        if (v.size() != cap)
            v.resize(cap);

        if (count < cap)
        {
            const size_t idx = (head + count) % cap;
            v[idx] = x;
            ++count;
        }
        else
        {
            v[head] = x;
            head = (head + 1) % cap;
        }
    }

    static const texture_stream_hist_sample_t *hist_at(const std::vector<texture_stream_hist_sample_t> &v, size_t head, size_t cap, size_t i)
    {
        const size_t idx = (head + i) % cap;
        return &v[idx];
    }

    struct scoped_texture_mip_preview_t final
    {
        GLuint tex = 0;
        GLint old_bound = 0;
        GLint old_base = 0;
        GLint old_max = 0;
        GLfloat old_min_lod = 0.0f;
        GLfloat old_max_lod = 0.0f;

        explicit scoped_texture_mip_preview_t(GLuint texture, GLint mip)
        {
            tex = texture;
            if (!tex)
                return;

            glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_bound);
            glBindTexture(GL_TEXTURE_2D, tex);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &old_base);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &old_max);
            glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, &old_min_lod);
            glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, &old_max_lod);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, mip);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, (float)mip);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)mip);
        }

        ~scoped_texture_mip_preview_t()
        {
            if (!tex)
                return;
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, old_base);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, old_max);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, old_min_lod);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, old_max_lod);
            glBindTexture(GL_TEXTURE_2D, (GLuint)old_bound);
        }
    };

    static bool tex_slot_passes_filters(const asset_debug_slot_t &s, const ImGuiTextFilter &filter, bool only_resident, bool only_pending)
    {
        if (s.type != ASSET_IMAGE || s.state != ASSET_STATE_READY)
            return false;
        if (s.vram_bytes == 0)
            return false; // window shows "on GPU / using VRAM" only
        if (only_resident && s.vram_bytes == 0)
            return false;

        const bool pending = (s.img_mip_count != 0) && (s.img_resident_top_mip > s.img_target_top_mip);
        if (only_pending && !pending)
            return false;

        if (filter.IsActive() && !filter.PassFilter(s.path))
            return false;
        return true;
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

        // History: one sample per frame, keep last 60 seconds.
        {
            const texture_stream_hist_sample_t s = {
                m_Snapshot.now_ms,
                bytes_to_mb(m_Snapshot.vram_resident_bytes),
                bytes_to_mb(m_Snapshot.tex_stream_uploaded_bytes_last_frame),
                bytes_to_mb(m_Snapshot.tex_stream_evicted_bytes_last_frame),
            };
            hist_push_ring(m_Hist, m_HistHead, m_HistCount, m_HistCap, s);

            const uint64_t cutoff = (m_Snapshot.now_ms > 60000u) ? (m_Snapshot.now_ms - 60000u) : 0u;
            while (m_HistCount)
            {
                const texture_stream_hist_sample_t *oldest = hist_at(m_Hist, m_HistHead, m_HistCap, 0);
                if (oldest->ms >= cutoff)
                    break;
                m_HistHead = (m_HistHead + 1) % m_HistCap;
                --m_HistCount;
            }
        }

        ImGui::SeparatorText("Texture Streaming");
        ImGui::Text("VRAM: %.1f / %.1f MB   Upload budget: %.2f MB/f   Pending: %u",
                    (double)bytes_to_mb(m_Snapshot.vram_resident_bytes),
                    (double)bytes_to_mb(m_Snapshot.vram_budget_bytes),
                    (double)bytes_to_mb(m_Snapshot.tex_stream_upload_budget_bytes_per_frame),
                    (unsigned)m_Snapshot.tex_stream_pending_uploads);
        ImGui::Text("Last frame: up %.2f MB (%u)  ev %.2f MB (%u)   stable %u f   evict_unused %u ms",
                    (double)bytes_to_mb(m_Snapshot.tex_stream_uploaded_bytes_last_frame),
                    (unsigned)m_Snapshot.tex_stream_uploads_last_frame,
                    (double)bytes_to_mb(m_Snapshot.tex_stream_evicted_bytes_last_frame),
                    (unsigned)m_Snapshot.tex_stream_evictions_last_frame,
                    (unsigned)m_Snapshot.tex_stream_stable_frames,
                    (unsigned)m_Snapshot.tex_stream_evict_unused_ms);

        // VRAM caveat / sparse stats (Task Manager only drops when sparse commit/decommit is supported+used).
        {
            uint32_t shown = 0;
            uint32_t sparse = 0;
            for (uint32_t i = 0; i < slot_count; ++i)
            {
                const asset_debug_slot_t &s = m_Slots[i];
                if (s.type != ASSET_IMAGE || s.state != ASSET_STATE_READY)
                    continue;
                if (s.vram_bytes == 0)
                    continue;
                const asset_image_t *img = asset_manager_get_image(ctx->assets, s.handle);
                if (!img)
                    continue;
                ++shown;
                if (img->stream_sparse)
                    ++sparse;
            }
            if (shown)
            {
                ImGui::Text("Sparse textures: %u / %u", (unsigned)sparse, (unsigned)shown);
                if (sparse == 0)
                    ImGui::TextUnformatted("Note: without sparse textures, OS VRAM may not drop even after mip eviction.");
            }
        }

        ImGui::SeparatorText("Filters");
        m_Filter.Draw("Filter (path)", 240.0f);
        ImGui::SameLine();
        ImGui::Checkbox("Only Resident", &m_ShowOnlyResident);
        ImGui::SameLine();
        ImGui::Checkbox("Only Pending", &m_ShowOnlyPending);

        if (ImGui::BeginTabBar("##tex_stream_tabs", ImGuiTabBarFlags_None))
        {
            if (ImGui::BeginTabItem("Grid"))
            {
                ImGui::SliderFloat("Thumbnail", &m_ThumbSize, 48.0f, 192.0f, "%.0f px");
                ImGui::SameLine();
                ImGui::Checkbox("Names", &m_GridShowNames);

                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float cell_w = m_ThumbSize + spacing;
                int cols = (int)(avail.x / (cell_w > 1.0f ? cell_w : 1.0f));
                cols = std::max(1, cols);

                std::vector<uint32_t> indices;
                indices.reserve(slot_count);
                for (uint32_t i = 0; i < slot_count; ++i)
                {
                    const asset_debug_slot_t &s = m_Slots[i];
                    if (!tex_slot_passes_filters(s, m_Filter, m_ShowOnlyResident, m_ShowOnlyPending))
                        continue;
                    indices.push_back(i);
                }

                ImGui::Text("%u textures", (unsigned)indices.size());
                ImGui::Separator();

                const float avail_y = ImGui::GetContentRegionAvail().y;
                const float grid_h = std::max(180.0f, avail_y - 280.0f);
                ImGui::BeginChild("##tex_stream_grid", ImVec2(0, grid_h), false, ImGuiWindowFlags_HorizontalScrollbar);
                if (ImGui::BeginTable("##tex_stream_grid_table", cols, ImGuiTableFlags_SizingFixedFit))
                {
                    int col = 0;
                    for (uint32_t idx : indices)
                    {
                        const asset_debug_slot_t &s = m_Slots[idx];
                        const asset_image_t *img = asset_manager_get_image(ctx->assets, s.handle);
                        if (!img || img->gl_handle == 0)
                            continue;

                        const uint32_t mip_count = s.img_mip_count ? s.img_mip_count : img->mip_count;
                        const int mip_to_preview =
                            (mip_count == 0) ? 0 :
                            (m_SelectedPreviewMip >= 0 && ihandle_eq(m_Selected, s.handle)) ? std::min(m_SelectedPreviewMip, (int)mip_count - 1) :
                            (int)s.img_resident_top_mip;

                        if (col == 0)
                            ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(col);

                        ImGui::PushID((int)s.slot_index);
                        const scoped_texture_mip_preview_t preview(img->gl_handle, mip_to_preview);
                        const ImTextureID tid = (ImTextureID)(intptr_t)img->gl_handle;
                        const bool clicked = ImGui::ImageButton("##tex", tid, ImVec2(m_ThumbSize, m_ThumbSize), ImVec2(0, 0), ImVec2(1, 1));
                        if (clicked)
                        {
                            m_Selected = s.handle;
                            m_SelectedPreviewMip = -1;
                        }

                        const bool is_selected = ihandle_is_valid(m_Selected) && ihandle_eq(m_Selected, s.handle);
                        const bool is_pending = (s.img_mip_count != 0) && (s.img_resident_top_mip > s.img_target_top_mip);
                        {
                            const ImVec2 rmin = ImGui::GetItemRectMin();
                            const ImVec2 rmax = ImGui::GetItemRectMax();
                            ImDrawList *dl = ImGui::GetWindowDrawList();
                            const ImU32 border_col = is_selected ? IM_COL32(255, 210, 64, 255) : IM_COL32(0, 0, 0, 110);
                            dl->AddRect(rmin, rmax, border_col, 0.0f, 0, is_selected ? 2.0f : 1.0f);

                            char overlay[64];
                            snprintf(overlay, sizeof(overlay), "mip %d  %u/%u", mip_to_preview, (unsigned)s.img_resident_top_mip, (unsigned)s.img_target_top_mip);
                            const ImU32 bg_col = is_pending ? IM_COL32(180, 40, 40, 140) : IM_COL32(0, 0, 0, 120);
                            const ImVec2 pad(4.0f, 3.0f);
                            const ImVec2 text_size = ImGui::CalcTextSize(overlay);
                            dl->AddRectFilled(ImVec2(rmin.x, rmax.y - text_size.y - pad.y * 2.0f),
                                              ImVec2(rmin.x + text_size.x + pad.x * 2.0f, rmax.y),
                                              bg_col);
                            dl->AddText(ImVec2(rmin.x + pad.x, rmax.y - text_size.y - pad.y), IM_COL32(255, 255, 255, 255), overlay);
                        }
                        if (ImGui::IsItemHovered() && s.path[0])
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(s.path);
                            ImGui::Text("mip %d   res/tgt %u/%u   vram %.2f MB",
                                        mip_to_preview,
                                        (unsigned)s.img_resident_top_mip,
                                        (unsigned)s.img_target_top_mip,
                                        (double)bytes_to_mb(s.vram_bytes));
                            ImGui::EndTooltip();
                        }

                        if (m_GridShowNames)
                        {
                            ImGui::TextUnformatted(path_basename(s.path));
                            ImGui::Text("mip %d  %.2f MB", mip_to_preview, (double)bytes_to_mb(s.vram_bytes));
                            ImGui::Text("res/tgt %u/%u", (unsigned)s.img_resident_top_mip, (unsigned)s.img_target_top_mip);
                        }
                        ImGui::PopID();

                        col = (col + 1) % cols;
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild();

                if (ihandle_is_valid(m_Selected))
                {
                    const char *sel_path = nullptr;
                    for (const asset_debug_slot_t &s : m_Slots)
                    {
                        if (s.type == ASSET_IMAGE && ihandle_eq(s.handle, m_Selected))
                        {
                            sel_path = s.path;
                            break;
                        }
                    }

                    const asset_image_t *img = asset_manager_get_image(ctx->assets, m_Selected);
                    if (img && img->gl_handle)
                    {
                        ImGui::SeparatorText("Selected");
                        if (sel_path && sel_path[0])
                            ImGui::TextUnformatted(sel_path);
                        const uint32_t mip_count = img->mip_count ? img->mip_count : 1u;
                        const int cur_mip = (int)img->stream_current_top_mip;
                        if (m_SelectedPreviewMip < 0)
                            m_SelectedPreviewMip = -1;

                        ImGui::Text("gl %u   %ux%u   mips %u", (unsigned)img->gl_handle, (unsigned)img->width, (unsigned)img->height, (unsigned)img->mip_count);
                        ImGui::Text("resident_top %u   target_top %u   safety_min %u   priority %u",
                                    (unsigned)img->stream_current_top_mip,
                                    (unsigned)img->stream_target_top_mip,
                                    (unsigned)img->stream_min_safety_mip,
                                    (unsigned)img->stream_priority);
                        ImGui::Text("vram_bytes %.2f MB   residency 0x%08X%08X",
                                    (double)bytes_to_mb(img->vram_bytes),
                                    (unsigned)(img->stream_residency_mask >> 32),
                                    (unsigned)(img->stream_residency_mask & 0xFFFFFFFFu));

                        int preview_mip = (m_SelectedPreviewMip < 0) ? cur_mip : m_SelectedPreviewMip;
                        preview_mip = std::clamp(preview_mip, 0, (int)mip_count - 1);
                        if (ImGui::SliderInt("Preview mip", &preview_mip, 0, (int)mip_count - 1))
                            m_SelectedPreviewMip = preview_mip;
                        ImGui::SameLine();
                        if (ImGui::Button("Current"))
                            m_SelectedPreviewMip = -1;

                        const scoped_texture_mip_preview_t preview(img->gl_handle, preview_mip);
                        const ImTextureID tid = (ImTextureID)(intptr_t)img->gl_handle;
                        const float preview_w = std::min(512.0f, ImGui::GetContentRegionAvail().x);
                        const float preview_h = std::min(256.0f, ImGui::GetContentRegionAvail().y);
                        ImGui::Image(tid, ImVec2(preview_w, std::max(64.0f, preview_h)));
                    }
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Table"))
            {
                const ImGuiTableFlags table_flags =
                    ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_Reorderable |
                    ImGuiTableFlags_Hideable |
                    ImGuiTableFlags_ScrollY;

                if (!ImGui::BeginTable("##tex_stream_table", 10, table_flags, ImVec2(0.0f, 0.0f)))
                {
                    ImGui::EndTabItem();
                }
                else
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                    ImGui::TableSetupColumn("VRAM (MB)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    ImGui::TableSetupColumn("Sparse", ImGuiTableColumnFlags_WidthFixed, 54.0f);
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
                        if (!tex_slot_passes_filters(s, m_Filter, m_ShowOnlyResident, m_ShowOnlyPending))
                            continue;

                        uint64_t age_ms = 0;
                        if (s.last_requested_ms && m_Snapshot.now_ms > s.last_requested_ms)
                            age_ms = m_Snapshot.now_ms - s.last_requested_ms;

                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%u", (unsigned)s.slot_index);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f", (double)bytes_to_mb(s.vram_bytes));

                        ImGui::TableSetColumnIndex(2);
                        {
                            const asset_image_t *img = asset_manager_get_image(ctx->assets, s.handle);
                            ImGui::TextUnformatted((img && img->stream_sparse) ? "yes" : "no");
                        }

                        ImGui::TableSetColumnIndex(3);
                        if (s.img_mip_count)
                            ImGui::Text("%u", (unsigned)s.img_mip_count);
                        else
                            ImGui::TextUnformatted("-");

                        ImGui::TableSetColumnIndex(4);
                        if (s.img_mip_count)
                            ImGui::Text("%u/%u", (unsigned)s.img_resident_top_mip, (unsigned)s.img_target_top_mip);
                        else
                            ImGui::TextUnformatted("-");

                        ImGui::TableSetColumnIndex(5);
                        if (s.img_forced)
                            ImGui::Text("%u", (unsigned)s.img_forced_top_mip);
                        else
                            ImGui::TextUnformatted("-");

                        ImGui::TableSetColumnIndex(6);
                        ImGui::Text("%u", (unsigned)s.img_priority);

                        ImGui::TableSetColumnIndex(7);
                        ImGui::Text("0x%08X%08X", (unsigned)(s.img_residency_mask >> 32), (unsigned)(s.img_residency_mask & 0xFFFFFFFFu));

                        ImGui::TableSetColumnIndex(8);
                        if (s.last_requested_ms)
                            ImGui::Text("%.1f", (double)age_ms / 1000.0);
                        else
                            ImGui::TextUnformatted("-");

                        ImGui::TableSetColumnIndex(9);
                        ImGui::TextUnformatted(s.path[0] ? s.path : "-");
                    }

                    ImGui::EndTable();
                    ImGui::EndTabItem();
                }
            }

            if (ImGui::BeginTabItem("History (60s)"))
            {
                std::vector<float> vram;
                std::vector<float> up;
                std::vector<float> ev;
                vram.reserve(m_HistCount);
                up.reserve(m_HistCount);
                ev.reserve(m_HistCount);
                for (size_t i = 0; i < m_HistCount; ++i)
                {
                    const texture_stream_hist_sample_t *s = hist_at(m_Hist, m_HistHead, m_HistCap, i);
                    vram.push_back(s->vram_mb);
                    up.push_back(s->up_mb);
                    ev.push_back(s->ev_mb);
                }

                const float vram_budget_mb = bytes_to_mb(m_Snapshot.vram_budget_bytes);
                if (!vram.empty())
                    ImGui::PlotLines("VRAM (MB)", vram.data(), (int)vram.size(), 0, nullptr, 0.0f, std::max(1.0f, vram_budget_mb), ImVec2(0, 70));
                if (!up.empty())
                    ImGui::PlotHistogram("Uploads (MB/f)", up.data(), (int)up.size(), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 55));
                if (!ev.empty())
                    ImGui::PlotHistogram("Evictions (MB/f)", ev.data(), (int)ev.size(), 0, nullptr, 0.0f, FLT_MAX, ImVec2(0, 55));
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
}
