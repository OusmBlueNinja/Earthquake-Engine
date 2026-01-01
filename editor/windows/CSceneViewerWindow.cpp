#include "editor/windows/CSceneViewerWindow.h"

extern "C"
{
#include "core/systems/ecs/ecs.h"
#include "core/systems/ecs/components/c_tag.h"
#include "core/systems/ecs/components/c_transform.h"
#include "core/systems/ecs/entity.h"
}

#include <stdio.h>
#include <string.h>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "imgui.h"
#include "editor/CEditorContext.h"
#include "IconsFontAwesome6.h"

namespace editor
{
    static void scene_viewer_make_default_name(char *dst, size_t dst_size, uint32_t idx)
    {
        if (!dst || dst_size == 0)
            return;
        snprintf(dst, dst_size, "Entity %u", idx);
        dst[dst_size - 1] = 0;
    }

    static const char *scene_viewer_tag_name(const c_tag_t *tag)
    {
        if (!tag)
            return "<null>";
        if (tag->name[0])
            return tag->name;
        return "<unnamed>";
    }

    static void scene_viewer_set_name(ecs_world_t *w, ecs_entity_t e, const char *name)
    {
        c_tag_t *tag = ecs_get(w, e, c_tag_t);
        if (!tag)
            return;

        memset(tag->name, 0, sizeof(tag->name));
        if (name && name[0])
            memcpy(tag->name, name, strlen(name) < (sizeof(tag->name) - 1) ? strlen(name) : (sizeof(tag->name) - 1));
    }

    static ecs_entity_t scene_viewer_create_entity(ecs_world_t *w)
    {
        ecs_entity_t e = ecs_entity_create(w);
        ecs_add(w, e, c_transform_t);
        return e;
    }

    static int scene_viewer_is_valid(ecs_world_t *w, ecs_entity_t e)
    {
        if (!w)
            return 0;
        if (e == 0)
            return 0;
        return ecs_entity_is_alive(w, e) ? 1 : 0;
    }

    static ecs_entity_t scene_viewer_parent(ecs_world_t *w, ecs_entity_t e)
    {
        if (!scene_viewer_is_valid(w, e))
            return 0;
        return ecs_entity_get_parent(w, e);
    }

    static void scene_viewer_build_children_map(
        ecs_world_t *w,
        std::unordered_map<uint64_t, std::vector<ecs_entity_t>> &children,
        std::vector<ecs_entity_t> &roots)
    {
        children.clear();
        roots.clear();

        uint32_t n = ecs_count(w, c_tag_t);
        c_tag_t *tags = ecs_dense(w, c_tag_t);
        if (!tags || n == 0)
            return;

        ecs_entity_t root = ecs_world_root(w);

        for (uint32_t i = 0; i < n; ++i)
        {
            ecs_entity_t e = tags[i].base.entity;
            if (!ecs_entity_is_alive(w, e))
                continue;

            if (e == root)
                continue;

            ecs_entity_t p = ecs_entity_get_parent(w, e);
            if (!ecs_entity_is_alive(w, p))
                p = root;

            children[(uint64_t)p].push_back(e);
        }

        auto &r = children[(uint64_t)root];
        roots = r;

        auto sort_by_name = [&](ecs_entity_t a, ecs_entity_t b) -> bool
        {
            c_tag_t *ta = ecs_get(w, a, c_tag_t);
            c_tag_t *tb = ecs_get(w, b, c_tag_t);
            const char *na = scene_viewer_tag_name(ta);
            const char *nb = scene_viewer_tag_name(tb);
            return strcmp(na, nb) < 0;
        };

        for (auto &kv : children)
            std::sort(kv.second.begin(), kv.second.end(), sort_by_name);

        std::sort(roots.begin(), roots.end(), sort_by_name);
    }

    static bool scene_viewer_would_cycle(ecs_world_t *w, ecs_entity_t child, ecs_entity_t parent)
    {
        if (child == parent)
            return true;

        ecs_entity_t cur = parent;
        while (cur != 0 && ecs_entity_is_alive(w, cur))
        {
            if (cur == child)
                return true;
            if (ecs_entity_is_root(w, cur))
                break;
            cur = ecs_entity_get_parent(w, cur);
        }

        return false;
    }

    static ImVec4 ui_mul(const ImVec4 &c, float m)
    {
        return ImVec4(c.x * m, c.y * m, c.z * m, c.w);
    }

    static bool ui_icon_click_no_bg_tint(
        const char *id,
        const char *icon,
        ImVec2 size,
        const ImVec4 &base_col,
        const ImVec4 &hover_col,
        const ImVec4 &active_col)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(id, size);

        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();
        bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        ImVec4 col = base_col;
        if (held)
            col = active_col;
        else if (hovered)
            col = hover_col;

        ImVec2 ts = ImGui::CalcTextSize(icon);
        float tx = p.x + (size.x - ts.x) * 0.5f;
        float ty = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(col), icon);

        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        return pressed;
    }

    static void ui_draw_tree_guides(int depth, const std::vector<uint8_t> &vert, ImVec2 item_min, ImVec2 item_max)
    {
        if (depth <= 0)
            return;

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Border);
        float indent = ImGui::GetStyle().IndentSpacing;

        float y0 = item_min.y;
        float y1 = item_max.y;
        float ym = (y0 + y1) * 0.5f;

        float x_cur = item_min.x - ImGui::GetTreeNodeToLabelSpacing() * 0.5f;

        for (int i = 0; i < depth - 1; ++i)
        {
            if (i < (int)vert.size() && vert[i])
            {
                float x = x_cur - (float)(depth - 1 - i) * indent;
                dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), col, 1.0f);
            }
        }

        dl->AddLine(ImVec2(x_cur, y0), ImVec2(x_cur, y1), col, 1.0f);
        dl->AddLine(ImVec2(x_cur, ym), ImVec2(item_min.x - 2.0f, ym), col, 1.0f);
    }

    static void scene_viewer_entity_context_menu(
        ecs_world_t *w,
        CEditorContext *ctx,
        ecs_entity_t e,
        ecs_entity_t root)
    {
        if (ImGui::MenuItem("Select"))
            ctx->selected_entity = e;

        if (ImGui::MenuItem("Duplicate"))
        {
            ecs_entity_t ne = scene_viewer_create_entity(w);
            c_tag_t *tag = ecs_get(w, e, c_tag_t);
            c_tag_t *ntag = ecs_get(w, ne, c_tag_t);
            if (ntag && tag)
            {
                memcpy(ntag->name, tag->name, sizeof(ntag->name));
                ntag->layer = tag->layer;
                ntag->visible = tag->visible;
            }
            if (ecs_has(w, e, c_transform_t))
            {
                c_transform_t *src = ecs_get(w, e, c_transform_t);
                c_transform_t *dst = ecs_add(w, ne, c_transform_t);
                if (src && dst)
                    *dst = *src;
            }
            ecs_entity_set_parent(w, ne, scene_viewer_parent(w, e) ? scene_viewer_parent(w, e) : root);
            ctx->selected_entity = ne;
        }

        if (ImGui::MenuItem("Delete"))
        {
            ecs_entity_destroy(w, e);
            if (ctx->selected_entity == e)
                ctx->selected_entity = 0;
            return;
        }
    }

    static void scene_viewer_draw_entity_node(
        ecs_world_t *w,
        CEditorContext *ctx,
        ecs_entity_t e,
        std::unordered_map<uint64_t, std::vector<ecs_entity_t>> &children,
        ecs_entity_t root,
        int depth,
        const std::vector<uint8_t> &vert,
        bool is_last,
        bool parent_effective_visible)
    {
        c_tag_t *tag = ecs_get(w, e, c_tag_t);
        const char *name = scene_viewer_tag_name(tag);

        auto it = children.find((uint64_t)e);
        bool has_children = (it != children.end() && !it->second.empty());

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        if (!has_children)
            flags |= ImGuiTreeNodeFlags_Leaf;

        if (ctx && ctx->selected_entity == e)
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID((int)e);

        bool self_vis = tag ? (tag->visible != 0) : true;
        bool effective_vis = parent_effective_visible && self_vis;
        bool dim = !effective_vis;

        ImVec4 text_col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImVec4 dis_col = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

        if (dim)
            ImGui::PushStyleColor(ImGuiCol_Text, dis_col);

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);

        char label[256];
        snprintf(label, sizeof(label), "%s##%llu", name, (unsigned long long)e);

        bool open = ImGui::TreeNodeEx(label, flags);
        ImGui::SetNextItemAllowOverlap();

        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();

        ui_draw_tree_guides(depth, vert, rmin, rmax);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            ctx->selected_entity = e;

        if (ImGui::BeginPopupContextItem("EntityContext", ImGuiPopupFlags_MouseButtonRight))
        {
            scene_viewer_entity_context_menu(w, ctx, e, root);
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ecs_entity_t payload = e;
            ImGui::SetDragDropPayload("ECS_ENTITY", &payload, sizeof(payload));
            ImGui::TextUnformatted(name);
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("ECS_ENTITY"))
            {
                if (pl->DataSize == sizeof(ecs_entity_t))
                {
                    ecs_entity_t dropped = *(const ecs_entity_t *)pl->Data;

                    if (scene_viewer_is_valid(w, dropped) && dropped != e && dropped != root)
                    {
                        if (!scene_viewer_would_cycle(w, dropped, e))
                            ecs_entity_set_parent(w, dropped, e);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (dim)
            ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(1);

        float h = ImGui::GetFrameHeight();
        float ih = h - 2.0f;
        if (ih < 10.0f)
            ih = 10.0f;

        ImVec2 icon_sz(ih, ih);

        ImVec4 base = dim ? dis_col : text_col;
        ImVec4 hov = ui_mul(base, 1.18f);
        ImVec4 act = ui_mul(base, 0.92f);

        const char *eye = self_vis ? ICON_FA_EYE : ICON_FA_EYE_SLASH;

        if (ui_icon_click_no_bg_tint("##vis", eye, icon_sz, base, hov, act))
        {
            if (tag)
                tag->visible = self_vis ? 0u : 1u;
        }

        if (ImGui::IsItemHovered())
        {
            if (!parent_effective_visible)
                ImGui::SetTooltip("Parent is hidden");
            else
                ImGui::SetTooltip("Visibility");
        }

        if (open)
        {
            if (has_children)
            {
                size_t count = it->second.size();
                for (size_t i = 0; i < count; ++i)
                {
                    ecs_entity_t c = it->second[i];
                    if (!ecs_entity_is_alive(w, c))
                        continue;

                    bool child_last = (i + 1 == count);

                    std::vector<uint8_t> next_vert = vert;
                    next_vert.push_back(is_last ? 0u : 1u);

                    scene_viewer_draw_entity_node(
                        w,
                        ctx,
                        c,
                        children,
                        root,
                        depth + 1,
                        next_vert,
                        child_last,
                        effective_vis);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    bool CSceneViewerWindow::BeginImpl()
    {
        return ImGui::Begin(GetName(), &m_Open);
    }

    void CSceneViewerWindow::EndImpl()
    {
        ImGui::End();
    }

    void CSceneViewerWindow::OnTick(float, CEditorContext *ctx)
    {
        if (!ctx || !ctx->app)
            return;

        ecs_world_t *w = &ctx->app->scene;
        ecs_entity_t root = ecs_world_root(w);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 0.0f));

        ImGui::TextUnformatted("Scene");
        ImGui::Separator();

        ImGui::BeginChild("SceneTree", ImVec2(0.0f, 0.0f), false);

        bool wants_bg_menu =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            !ImGui::IsAnyItemHovered() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

        if (wants_bg_menu)
            ImGui::OpenPopup("SceneContext");

        if (ImGui::BeginPopup("SceneContext"))
        {
            if (ImGui::MenuItem("New Entity"))
            {
                ecs_entity_t e = scene_viewer_create_entity(w);
                ctx->selected_entity = e;
            }

            if (ImGui::MenuItem("Clear Selection", nullptr, false, ctx->selected_entity != 0))
                ctx->selected_entity = 0;

            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("ECS_ENTITY"))
            {
                if (pl->DataSize == sizeof(ecs_entity_t))
                {
                    ecs_entity_t dropped = *(const ecs_entity_t *)pl->Data;
                    if (scene_viewer_is_valid(w, dropped) && dropped != root)
                        ecs_entity_set_parent(w, dropped, root);
                }
            }
            ImGui::EndDragDropTarget();
        }

        std::unordered_map<uint64_t, std::vector<ecs_entity_t>> children;
        std::vector<ecs_entity_t> roots;
        scene_viewer_build_children_map(w, children, roots);

        ImGuiTableFlags tf = ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##SceneTreeTable", 2, tf))
        {
            float icon_col = ImGui::GetFrameHeight() + 6.0f;
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Vis", ImGuiTableColumnFlags_WidthFixed, icon_col);

            size_t count = roots.size();
            for (size_t i = 0; i < count; ++i)
            {
                ecs_entity_t e = roots[i];
                if (!ecs_entity_is_alive(w, e))
                    continue;

                bool last = (i + 1 == count);
                std::vector<uint8_t> vert;
                scene_viewer_draw_entity_node(w, ctx, e, children, root, 0, vert, last, true);
            }

            if (roots.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Empty Scene");
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();

        ImGui::PopStyleVar(7);
    }
}
