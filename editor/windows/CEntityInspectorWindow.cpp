#include "editor/windows/CEntityInspectorWindow.h"

extern "C"
{
#include "core/systems/ecs/ecs.h"
#include "core/systems/ecs/components/c_tag.h"
#include "core/systems/ecs/components/c_transform.h"
#include "core/systems/ecs/components/c_mesh_renderer.h"
#include "core/systems/ecs/components/c_light.h"
#include "handle.h"
}

#include <string.h>
#include <stdio.h>
#include <float.h>
#include <stdlib.h>

#include "imgui.h"
#include "editor/CEditorContext.h"
#include "IconsFontAwesome6.h"

namespace editor
{
    static struct
    {
        uint32_t type_tag;
        uint32_t size;
        uint8_t bytes[1024];
        int valid;
    } g_comp_clip;

    struct asset_browser_drag_payload_t
    {
        ihandle_t handle;
        uint32_t type;
    };

    static inspector_asset_label_fn_t g_inspector_asset_label_fn = nullptr;

    void EntityInspector_SetAssetLabelFn(inspector_asset_label_fn_t fn)
    {
        g_inspector_asset_label_fn = fn;
    }

    static bool inspector_handle_valid(ihandle_t h)
    {
        return ((uint32_t)h.value | (uint32_t)h.type | (uint32_t)h.meta) != 0;
    }

    static void inspector_make_asset_label(CEditorContext *ctx, ihandle_t h, char *out, uint32_t out_sz)
    {
        if (!out || out_sz == 0)
            return;

        out[0] = 0;

        if (g_inspector_asset_label_fn)
        {
            g_inspector_asset_label_fn(ctx, h, out, out_sz);
            if (out[0])
                return;
        }

        if (!inspector_handle_valid(h))
        {
            snprintf(out, out_sz, "None");
            out[out_sz - 1] = 0;
            return;
        }

        handle_hex_triplet_filesafe(out, h);
    }

    static uint32_t hash_str(const char *s)
    {
        uint32_t h = 2166136261u;
        if (!s)
            return h;
        while (*s)
        {
            h ^= (uint8_t)*s++;
            h *= 16777619u;
        }
        return h;
    }

    static bool parse_hex_u32(const char *s, uint32_t *out)
    {
        if (!s || !out)
            return false;

        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
            ++s;

        if (!*s)
            return false;

        char *endp = nullptr;
        unsigned long v = strtoul(s, &endp, 16);
        if (endp == s)
            return false;

        while (*endp == ' ' || *endp == '\t' || *endp == '\n' || *endp == '\r')
            ++endp;

        if (*endp != 0)
            return false;

        *out = (uint32_t)v;
        return true;
    }

    static bool parse_hex_u16(const char *s, uint16_t *out)
    {
        uint32_t v = 0;
        if (!parse_hex_u32(s, &v))
            return false;
        if (v > 0xFFFFu)
            return false;
        *out = (uint16_t)v;
        return true;
    }

    static void clipboard_set(const char *type_name, const void *data, uint32_t size)
    {
        g_comp_clip.valid = 0;
        g_comp_clip.type_tag = 0;
        g_comp_clip.size = 0;

        if (!type_name || !type_name[0] || !data || !size)
            return;
        if (size > (uint32_t)sizeof(g_comp_clip.bytes))
            return;

        memcpy(g_comp_clip.bytes, data, size);
        g_comp_clip.type_tag = hash_str(type_name);
        g_comp_clip.size = size;
        g_comp_clip.valid = 1;
    }

    static int clipboard_can_paste(const char *type_name, uint32_t size)
    {
        if (!g_comp_clip.valid)
            return 0;
        if (g_comp_clip.type_tag != hash_str(type_name))
            return 0;
        if (g_comp_clip.size != size)
            return 0;
        return 1;
    }

    CEntityInspectorWindow::CEntityInspectorWindow()
        : CBaseWindow("Entity Inspector")
    {
    }

    bool CEntityInspectorWindow::BeginImpl()
    {
        const float min_w = 380.0f;

        ImGui::SetNextWindowSizeConstraints(ImVec2(min_w, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        bool ok = ImGui::Begin(GetName(), &m_Open);

        if (ok)
        {
            ImVec2 sz = ImGui::GetWindowSize();
            if (sz.x < min_w)
                ImGui::SetWindowSize(ImVec2(min_w, sz.y), ImGuiCond_Always);
        }

        return ok;
    }

    void CEntityInspectorWindow::EndImpl()
    {
        ImGui::End();
    }

    static void inspector_layers_popup(uint16_t *layers_mask);

    static void ui_thin_separator()
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float y = p.y + 2.0f;
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Separator);
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), col, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    static bool ui_icon_button_centered_no_bg(const char *id, const char *icon, ImVec2 size)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(id, size);

        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();
        bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        if (hovered || held)
        {
            ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered);
            dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, 0.0f);
        }

        ImVec2 ts = ImGui::CalcTextSize(icon);
        float tx = p.x + (size.x - ts.x) * 0.5f;
        float ty = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_Text), icon);

        return pressed;
    }

    static bool ui_text_clickable_no_bg(const char *id, const char *txt)
    {
        ImGui::PushID(id);

        float h = ImGui::GetFrameHeight();
        ImVec2 ts = ImGui::CalcTextSize(txt);
        float w = ts.x + 2.0f;

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##t", ImVec2(w, h));

        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(p.x, ty), ImGui::GetColorU32(ImGuiCol_Text), txt);

        ImGui::PopID();
        return pressed;
    }

    static bool inspector_component_header_minimal(
        const char *storage_key,
        const char *icon,
        const char *name,
        bool default_open,
        bool *out_open_menu)
    {
        if (out_open_menu)
            *out_open_menu = false;

        ImGuiID sid = ImGui::GetID(storage_key);
        ImGuiStorage *st = ImGui::GetStateStorage();
        bool open = st->GetBool(sid, default_open);

        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();
        const ImGuiStyle &style = ImGui::GetStyle();

        ImVec2 content_cursor = ImGui::GetCursorScreenPos();

        float row_h = ImGui::GetFrameHeight() + 8.0f;
        float y0 = content_cursor.y;

        bool has_vscroll = ImGui::GetScrollMaxY() > 0.0f;
        float x0 = win_pos.x;
        float x1 = win_pos.x + win_size.x - (has_vscroll ? style.ScrollbarSize : 0.0f);

        float pad_x = 10.0f;
        float dots_w = ImGui::GetFrameHeight();
        float dots_pad_r = 6.0f;
        float dots_pad_l = 6.0f;

        ImVec2 dots_min = ImVec2(x1 - dots_w - dots_pad_r, y0 + (row_h - dots_w) * 0.5f);
        ImVec2 dots_max = ImVec2(dots_min.x + dots_w, dots_min.y + dots_w);

        ImGui::PushID(storage_key);

        float toggle_w = (dots_min.x - x0) - dots_pad_l;
        if (toggle_w < 1.0f)
            toggle_w = 1.0f;

        ImGui::SetCursorScreenPos(ImVec2(x0, y0));
        ImGui::InvisibleButton("##hdr_toggle", ImVec2(toggle_w, row_h));
        bool toggle_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImU32 line = ImGui::GetColorU32(ImGuiCol_Border);
        ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);

        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y0), line, 1.0f);

        float ty = y0 + (row_h - ImGui::GetFontSize()) * 0.5f;
        float x = x0 + pad_x;

        const char *arrow = open ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT;
        dl->AddText(ImVec2(x, ty), text_col, arrow);
        x += ImGui::CalcTextSize(arrow).x + 8.0f;

        if (icon && icon[0])
        {
            dl->AddText(ImVec2(x, ty), text_col, icon);
            x += ImGui::CalcTextSize(icon).x + 8.0f;
        }

        if (name && name[0])
            dl->AddText(ImVec2(x, ty), text_col, name);

        ImGui::SetCursorScreenPos(dots_min);
        bool dots_pressed = ui_icon_button_centered_no_bg("##dots", ICON_FA_ELLIPSIS, ImVec2(dots_w, dots_w));

        if (dots_pressed && out_open_menu)
            *out_open_menu = true;

        if (toggle_clicked)
        {
            ImVec2 mp = ImGui::GetMousePos();
            bool in_dots = (mp.x >= dots_min.x && mp.x <= dots_max.x && mp.y >= dots_min.y && mp.y <= dots_max.y);
            if (!in_dots)
                open = !open;
        }

        st->SetBool(sid, open);

        ImGui::PopID();

        ImGui::SetCursorScreenPos(ImVec2(content_cursor.x, y0 + row_h + 6.0f));
        return open;
    }

    static void inspector_layers_popup(uint16_t *layers_mask)
    {
        if (!layers_mask)
            return;

        if (!ImGui::BeginPopup("LayersPopup"))
            return;

        ImGui::TextUnformatted("Layers");
        ImGui::Separator();

        uint16_t mask = *layers_mask;

        const float avail = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float cell_w = (avail - spacing * 4.0f) / 5.0f;
        const ImVec2 cell(cell_w, 0.0f);

        for (int r = 0; r < 2; ++r)
        {
            for (int c = 0; c < 5; ++c)
            {
                int id = r * 5 + c;
                uint16_t bit = (uint16_t)(1u << id);
                bool on = (mask & bit) != 0;

                char label[32];
                snprintf(label, sizeof(label), "%d", id);

                if (on)
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

                if (ImGui::Selectable(label, on, 0, cell))
                {
                    if (on)
                        mask = (uint16_t)(mask & ~bit);
                    else
                        mask = (uint16_t)(mask | bit);
                }

                if (on)
                    ImGui::PopStyleVar();

                if (c != 4)
                    ImGui::SameLine();
            }
        }

        ImGui::Separator();

        if (ImGui::Button("All"))
            mask = 0x03FFu;

        ImGui::SameLine();

        if (ImGui::Button("None"))
            mask = 0;

        *layers_mask = mask;

        ImGui::EndPopup();
    }

    static void inspector_draw_tag_inline(ecs_world_t *w, ecs_entity_t e)
    {
        c_tag_t *tag = ecs_get(w, e, c_tag_t);
        if (!tag)
            return;

        ImGui::PushID("TagInline");

        float h = ImGui::GetFrameHeight();
        float icon_w = h;

        char name_buf[C_TAG_NAME_MAX];
        memset(name_buf, 0, sizeof(name_buf));
        memcpy(name_buf, tag->name, sizeof(tag->name));

        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float name_w = ImGui::GetContentRegionAvail().x - (icon_w * 2.0f) - (spacing * 2.0f);
        if (name_w < 60.0f)
            name_w = 60.0f;

        ImGui::SetNextItemWidth(name_w);
        if (ImGui::InputText("##Name", name_buf, sizeof(name_buf)))
        {
            memset(tag->name, 0, sizeof(tag->name));
            memcpy(tag->name, name_buf, sizeof(tag->name) - 1);
        }

        ImGui::SameLine();

        bool visible = tag->visible != 0;
        ImVec2 icon_sz(icon_w, h);

        if (ui_icon_button_centered_no_bg("##vis", visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, icon_sz))
            tag->visible = visible ? 0u : 1u;

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Visibility");

        ImGui::SameLine();

        uint16_t layers_mask = (uint16_t)(tag->layer & 0x03FFu);

        if (ui_icon_button_centered_no_bg("##layers", ICON_FA_LAYER_GROUP, icon_sz))
            ImGui::OpenPopup("LayersPopup");

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Layers");

        inspector_layers_popup(&layers_mask);

        tag->layer = (uint32_t)layers_mask;

        ImGui::PopID();

        (void)w;
        (void)e;
    }

    static bool inspector_vec3_plain(const char *id, vec3 *v, float speed, float reset_value)
    {
        if (!v)
            return false;

        bool changed = false;

        ImGui::PushID(id);

        float avail = ImGui::GetContentRegionAvail().x;

        float right_pad = 14.0f;
        float group_spacing = ImGui::GetStyle().ItemSpacing.x;
        float label_gap = 3.0f;

        float h = ImGui::GetFrameHeight();
        float letter_w = ImGui::CalcTextSize("X").x + 2.0f;

        float max_per = (avail - right_pad - group_spacing * 2.0f - letter_w * 3.0f - label_gap * 3.0f) / 3.0f;
        if (max_per < 18.0f)
            max_per = 18.0f;

        float desired = ImGui::GetFontSize() * 4.2f;
        float per = desired;
        if (per > max_per)
            per = max_per;

        float group_w =
            (letter_w + label_gap + per) * 3.0f +
            group_spacing * 2.0f;

        float start_x = ImGui::GetCursorPosX();
        float target_x = start_x + avail - right_pad - group_w;
        if (target_x > start_x)
            ImGui::SetCursorPosX(target_x);

        auto axis = [&](const char *lbl, float *val, const char *drag_id) -> bool
        {
            bool c = false;

            if (ui_text_clickable_no_bg(lbl, lbl))
            {
                *val = reset_value;
                c = true;
            }

            ImGui::SameLine(0.0f, label_gap);
            ImGui::SetNextItemWidth(per);

            float tmp = *val;
            if (ImGui::DragFloat(drag_id, &tmp, speed, 0.0f, 0.0f, "%.3f"))
            {
                *val = tmp;
                c = true;
            }

            return c;
        };

        changed |= axis("X", &v->x, "##x");
        ImGui::SameLine(0.0f, group_spacing);
        changed |= axis("Y", &v->y, "##y");
        ImGui::SameLine(0.0f, group_spacing);
        changed |= axis("Z", &v->z, "##z");

        ImGui::PopID();
        return changed;
    }

    static bool ui_asset_slot_row(const char *id, const char *icon, const char *label, float height)
    {
        ImGui::PushID(id);

        float avail = ImGui::GetContentRegionAvail().x;
        if (avail < 1.0f)
            avail = 1.0f;

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 sz(avail, height);

        ImGui::InvisibleButton("##slot", sz);

        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
        ImU32 br = ImGui::GetColorU32(ImGuiCol_Border);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), bg, 0.0f);
        dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), br, 0.0f);

        float pad = 6.0f;
        float icon_w = height;

        if (icon && icon[0])
        {
            ImVec2 its = ImGui::CalcTextSize(icon);
            float ix = p0.x + pad + (icon_w - its.x) * 0.5f;
            float iy = p0.y + (height - ImGui::GetFontSize()) * 0.5f;
            dl->AddText(ImVec2(ix, iy), ImGui::GetColorU32(ImGuiCol_Text), icon);
        }

        float text_x0 = p0.x + pad + icon_w + 6.0f;
        float ty = p0.y + (height - ImGui::GetFontSize()) * 0.5f;

        const char *txt = (label && label[0]) ? label : "";
        dl->AddText(ImVec2(text_x0, ty), ImGui::GetColorU32(ImGuiCol_Text), txt);

        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::PopID();
        return hovered;
    }

    class CBaseInspectorComponent
    {
    public:
        virtual ~CBaseInspectorComponent() {}

        virtual const char *StorageKey() const = 0;
        virtual const char *PopupID() const = 0;
        virtual const char *Icon() const = 0;
        virtual const char *Name() const = 0;

        virtual const char *ClipTypeName() const = 0;
        virtual uint32_t ClipSize() const = 0;

        virtual bool DefaultOpen() const { return true; }

        virtual bool Has(ecs_world_t *w, ecs_entity_t e) const = 0;
        virtual void *Get(ecs_world_t *w, ecs_entity_t e) const = 0;

        virtual void Add(ecs_world_t *w, ecs_entity_t e) const = 0;
        virtual void Remove(ecs_world_t *w, ecs_entity_t e) const = 0;

        virtual void PasteFixup(ecs_entity_t, void *) const {}

        virtual void DrawBody(CEditorContext *ctx, ecs_world_t *w, ecs_entity_t e, void *comp) const = 0;

        void Draw(CEditorContext *ctx, ecs_world_t *w, ecs_entity_t e) const
        {
            void *comp = Get(w, e);
            if (!comp)
                return;

            bool open_menu = false;
            bool open = inspector_component_header_minimal(StorageKey(), Icon(), Name(), DefaultOpen(), &open_menu);

            if (open_menu)
                ImGui::OpenPopup(PopupID());

            DrawMenu(w, e, comp);

            if (!open)
                return;

            ImGui::Dummy(ImVec2(0.0f, 1.0f));
            DrawBody(ctx, w, e, comp);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
        }

    private:
        void DrawMenu(ecs_world_t *w, ecs_entity_t e, void *comp) const
        {
            if (!ImGui::BeginPopup(PopupID()))
                return;

            if (ImGui::MenuItem(ICON_FA_COPY " Copy", nullptr, false, comp != nullptr))
                clipboard_set(ClipTypeName(), comp, ClipSize());

            bool can_paste = (comp != nullptr) && (clipboard_can_paste(ClipTypeName(), ClipSize()) != 0);
            if (ImGui::MenuItem(ICON_FA_PASTE " Paste", nullptr, false, can_paste))
            {
                memcpy(comp, g_comp_clip.bytes, ClipSize());
                PasteFixup(e, comp);
            }

            ImGui::Separator();

            if (ImGui::MenuItem(ICON_FA_TRASH " Remove", nullptr, false, comp != nullptr))
                Remove(w, e);

            ImGui::EndPopup();
        }
    };

    class CInspectorTransformComponent final : public CBaseInspectorComponent
    {
    public:
        const char *StorageKey() const override { return "##Transform"; }
        const char *PopupID() const override { return "TransformMenu"; }
        const char *Icon() const override { return ICON_FA_ARROWS_TO_DOT; }
        const char *Name() const override { return "Transform"; }
        const char *ClipTypeName() const override { return "c_transform_t"; }
        uint32_t ClipSize() const override { return (uint32_t)sizeof(c_transform_t); }
        bool Has(ecs_world_t *w, ecs_entity_t e) const override { return ecs_has(w, e, c_transform_t) != 0; }
        void *Get(ecs_world_t *w, ecs_entity_t e) const override { return (void *)ecs_get(w, e, c_transform_t); }
        void Add(ecs_world_t *w, ecs_entity_t e) const override { ecs_add(w, e, c_transform_t); }
        void Remove(ecs_world_t *w, ecs_entity_t e) const override { ecs_remove(w, e, c_transform_t); }

        void PasteFixup(ecs_entity_t e, void *comp) const override
        {
            c_transform_t *tr = (c_transform_t *)comp;
            tr->base.entity = e;
        }

        void DrawBody(CEditorContext *, ecs_world_t *, ecs_entity_t, void *comp) const override
        {
            c_transform_t *tr = (c_transform_t *)comp;

            if (ImGui::BeginTable("##TransformTable", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
            {
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Val", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Position");
                ImGui::TableSetColumnIndex(1);
                inspector_vec3_plain("pos", &tr->position, 0.05f, 0.0f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Rotation");
                ImGui::TableSetColumnIndex(1);
                inspector_vec3_plain("rot", &tr->rotation, 0.25f, 0.0f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Scale");
                ImGui::TableSetColumnIndex(1);
                inspector_vec3_plain("scl", &tr->scale, 0.05f, 1.0f);

                ImGui::EndTable();
            }
        }
    };

    class CInspectorMeshRendererComponent final : public CBaseInspectorComponent
    {
    public:
        const char *StorageKey() const override { return "##MeshRenderer"; }
        const char *PopupID() const override { return "MeshRendererMenu"; }
        const char *Icon() const override { return ICON_FA_CUBE; }
        const char *Name() const override { return "Mesh Renderer"; }
        const char *ClipTypeName() const override { return "c_mesh_renderer_t"; }
        uint32_t ClipSize() const override { return (uint32_t)sizeof(c_mesh_renderer_t); }
        bool Has(ecs_world_t *w, ecs_entity_t e) const override { return ecs_has(w, e, c_mesh_renderer_t) != 0; }
        void *Get(ecs_world_t *w, ecs_entity_t e) const override { return (void *)ecs_get(w, e, c_mesh_renderer_t); }
        void Add(ecs_world_t *w, ecs_entity_t e) const override { ecs_add(w, e, c_mesh_renderer_t); }
        void Remove(ecs_world_t *w, ecs_entity_t e) const override { ecs_remove(w, e, c_mesh_renderer_t); }

        void PasteFixup(ecs_entity_t e, void *comp) const override
        {
            c_mesh_renderer_t *mr = (c_mesh_renderer_t *)comp;
            mr->base.entity = e;
        }

        void DrawBody(CEditorContext *ctx, ecs_world_t *, ecs_entity_t e, void *comp) const override
        {
            c_mesh_renderer_t *mr = (c_mesh_renderer_t *)comp;

            char label[256];
            inspector_make_asset_label(ctx, mr->model, label, (uint32_t)sizeof(label));

            float h = ImGui::GetFrameHeight();

            ui_asset_slot_row("MeshRendererAsset", ICON_FA_CUBE, label, h);

            if (ImGui::IsItemHovered())
            {
                char hs[64];
                handle_hex_triplet_filesafe(hs, mr->model);
                ImGui::SetTooltip("%s\n%s", label, hs);
            }

            if (ImGui::BeginDragDropSource())
            {
                asset_browser_drag_payload_t p{};
                p.handle = mr->model;
                p.type = (uint32_t)mr->model.type;
                ImGui::SetDragDropPayload("ASSET_IHANDLE", &p, sizeof(p));
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("ASSET_IHANDLE"))
                {
                    if (pl->Data && pl->DataSize >= (int)sizeof(ihandle_t))
                    {
                        ihandle_t hnd{};
                        memcpy(&hnd, pl->Data, sizeof(ihandle_t));
                        mr->model = hnd;
                        mr->base.entity = e;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
    };

    class CInspectorLightComponent final : public CBaseInspectorComponent
    {
    public:
        const char *StorageKey() const override { return "##Light"; }
        const char *PopupID() const override { return "LightMenu"; }
        const char *Icon() const override { return ICON_FA_LIGHTBULB; }
        const char *Name() const override { return "Light"; }
        const char *ClipTypeName() const override { return "c_light_t"; }
        uint32_t ClipSize() const override { return (uint32_t)sizeof(c_light_t); }
        bool Has(ecs_world_t *w, ecs_entity_t e) const override { return ecs_has(w, e, c_light_t) != 0; }
        void *Get(ecs_world_t *w, ecs_entity_t e) const override { return (void *)ecs_get(w, e, c_light_t); }
        void Add(ecs_world_t *w, ecs_entity_t e) const override { ecs_add(w, e, c_light_t); }
        void Remove(ecs_world_t *w, ecs_entity_t e) const override { ecs_remove(w, e, c_light_t); }

        void PasteFixup(ecs_entity_t e, void *comp) const override
        {
            c_light_t *l = (c_light_t *)comp;
            l->base.entity = e;
        }

        void DrawBody(CEditorContext *, ecs_world_t *, ecs_entity_t e, void *comp) const override
        {
            c_light_t *l = (c_light_t *)comp;

            if (l->intensity < 0.0f) l->intensity = 0.0f;
            if (l->radius < 0.0f) l->radius = 0.0f;
            if (l->range < 0.0f) l->range = 0.0f;

            if (ImGui::BeginTable("##LightTable", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
            {
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Val", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Type");
                ImGui::TableSetColumnIndex(1);

                const char *items[] = { "Directional", "Point", "Spot" };
                int t = (int)l->type;
                if (t < 0) t = 0;
                if (t > 2) t = 2;
                if (ImGui::Combo("##type", &t, items, 3))
                    l->type = (light_type_t)t;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Color");
                ImGui::TableSetColumnIndex(1);

                float col[3] = { l->color.x, l->color.y, l->color.z };
                if (ImGui::ColorEdit3("##color", col, ImGuiColorEditFlags_NoInputs))
                {
                    l->color.x = col[0];
                    l->color.y = col[1];
                    l->color.z = col[2];
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Intensity");
                ImGui::TableSetColumnIndex(1);

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::DragFloat("##intensity", &l->intensity, 0.05f, 0.0f, 0.0f, "%.3f");

                if (l->type != LIGHT_DIRECTIONAL)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Radius");
                    ImGui::TableSetColumnIndex(1);

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::DragFloat("##radius", &l->radius, 0.05f, 0.0f, 0.0f, "%.3f");

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Range");
                    ImGui::TableSetColumnIndex(1);

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::DragFloat("##range", &l->range, 0.05f, 0.0f, 0.0f, "%.3f");
                }

                ImGui::EndTable();
            }

            l->base.entity = e;
        }
    };

    static CInspectorTransformComponent g_inspector_transform;
    static CInspectorMeshRendererComponent g_inspector_mesh_renderer;
    static CInspectorLightComponent g_inspector_light;

    static void inspector_add_components_popup(CBaseInspectorComponent *const *list, int list_count, ecs_world_t *w, ecs_entity_t e)
    {
        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;

        ImGui::TextUnformatted("Add Component");
        ImGui::Separator();

        for (int i = 0; i < list_count; ++i)
        {
            CBaseInspectorComponent *c = list[i];
            if (!c)
                continue;

            bool has = c->Has(w, e);
            bool enabled = !has;

            char label[256];
            if (c->Icon() && c->Icon()[0])
                snprintf(label, sizeof(label), "%s %s", c->Icon(), c->Name() ? c->Name() : "");
            else
                snprintf(label, sizeof(label), "%s", c->Name() ? c->Name() : "");

            if (ImGui::MenuItem(label, nullptr, false, enabled))
                c->Add(w, e);
        }

        ImGui::EndPopup();
    }

    void CEntityInspectorWindow::OnTick(float, CEditorContext *ctx)
    {
        if (!ctx || !ctx->app)
            return;

        ecs_world_t *w = &ctx->app->scene;

        if (ctx->selected_entity == 0 || !ecs_entity_is_alive(w, ctx->selected_entity))
            return;

        ecs_entity_t e = ctx->selected_entity;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));

        inspector_draw_tag_inline(w, e);

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        g_inspector_transform.Draw(ctx, w, e);

        ui_thin_separator();

        g_inspector_mesh_renderer.Draw(ctx, w, e);

        ui_thin_separator();

        g_inspector_light.Draw(ctx, w, e);

        ui_thin_separator();

        float bw = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Add Component", ImVec2(bw, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");

        CBaseInspectorComponent *add_list[] =
            {
                &g_inspector_transform,
                &g_inspector_mesh_renderer,
                &g_inspector_light
            };

        inspector_add_components_popup(add_list, (int)(sizeof(add_list) / sizeof(add_list[0])), w, e);

        if (ImGui::BeginPopupContextVoid("AddComponentsRC", ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem(ICON_FA_PLUS " Add Component"))
                ImGui::OpenPopup("AddComponentPopup");
            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(7);
    }
}
