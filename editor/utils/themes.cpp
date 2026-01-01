#include "themes.h"

static void ApplyEditorLayoutDefaults(ImGuiStyle &s)
{
    s.WindowPadding = ImVec2(10, 10);
    s.FramePadding = ImVec2(8, 6);
    s.CellPadding = ImVec2(8, 6);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.TouchExtraPadding = ImVec2(0, 0);
    s.IndentSpacing = 18.0f;
    s.ScrollbarSize = 14.0f;
    s.GrabMinSize = 12.0f;

    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.TabBorderSize = 0.0f;

    s.WindowRounding = 7.0f;
    s.ChildRounding = 7.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 7.0f;
    s.ScrollbarRounding = 10.0f;
    s.GrabRounding = 7.0f;
    s.TabRounding = 6.0f;

    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.0f);
}

class CTheme_GraphiteEditorDark final : public CBaseEditorTheme
{
public:
    std::string_view GetId() const override { return "graphite_dark"; }
    std::string_view GetDisplayName() const override { return "Graphite (Editor Dark)"; }

    void Apply() const override
    {
        ImGuiStyle &s = ImGui::GetStyle();
        ApplyEditorLayoutDefaults(s);

        ImVec4 *c = s.Colors;

        c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.58f, 0.58f, 1.00f);

        c[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        c[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        c[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.14f, 0.14f, 0.98f);

        c[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        c[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

        c[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);

        c[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);

        c[ImGuiCol_ScrollbarBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

        ImVec4 accent = ImVec4(0.25f, 0.56f, 0.92f, 1.00f);

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 0.50f, 0.86f, 1.00f);

        c[ImGuiCol_Button] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);

        c[ImGuiCol_Header] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);

        c[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
        c[ImGuiCol_SeparatorActive] = accent;

        c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.20f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(accent.x, accent.y, accent.z, 0.70f);

        c[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        c[ImGuiCol_TabSelectedOverline] = accent;
        c[ImGuiCol_TabDimmed] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.85f);


        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.30f);

        c[ImGuiCol_NavCursor] = ImVec4(accent.x, accent.y, accent.z, 0.80f);
        c[ImGuiCol_NavWindowingHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);

        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
    }
};

class CTheme_SlateEditorDark final : public CBaseEditorTheme
{
public:
    std::string_view GetId() const override { return "slate_dark"; }
    std::string_view GetDisplayName() const override { return "Slate (Editor Dark)"; }

    void Apply() const override
    {
        ImGuiStyle &s = ImGui::GetStyle();
        ApplyEditorLayoutDefaults(s);

        ImVec4 *c = s.Colors;

        c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.93f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);

        c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
        c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
        c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.98f);

        c[ImGuiCol_Border] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.22f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);

        c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);

        c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);

        c[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.34f, 0.34f, 0.40f, 1.00f);

        ImVec4 accent = ImVec4(0.00f, 0.73f, 0.56f, 1.00f);

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.64f, 0.49f, 1.00f);

        c[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.22f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);

        c[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.18f);
        c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.30f);

        c[ImGuiCol_Separator] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
        c[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        c[ImGuiCol_SeparatorActive] = ImVec4(accent.x, accent.y, accent.z, 0.80f);

        c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.18f);
        c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
        c[ImGuiCol_ResizeGripActive] = ImVec4(accent.x, accent.y, accent.z, 0.65f);

        c[ImGuiCol_Tab] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
        c[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
        c[ImGuiCol_TabSelectedOverline] = accent;
        c[ImGuiCol_TabDimmed] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.85f);


        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.25f);

        c[ImGuiCol_NavCursor] = ImVec4(accent.x, accent.y, accent.z, 0.85f);
        c[ImGuiCol_NavWindowingHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);

        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
    }
};

void CEditorThemeManager::Register(std::unique_ptr<CBaseEditorTheme> theme)
{
    if (!theme)
        return;
    m_themes.emplace_back(std::move(theme));
    if (m_currentIndex == (size_t)-1)
        m_currentIndex = 0;
}

void CEditorThemeManager::RegisterBuiltins()
{
    Register(std::make_unique<CTheme_GraphiteEditorDark>());
    Register(std::make_unique<CTheme_SlateEditorDark>());
}

const CBaseEditorTheme *CEditorThemeManager::FindById(std::string_view id) const
{
    for (const auto &t : m_themes)
        if (t && t->GetId() == id)
            return t.get();
    return nullptr;
}

bool CEditorThemeManager::ApplyById(std::string_view id)
{
    for (size_t i = 0; i < m_themes.size(); ++i)
    {
        if (m_themes[i] && m_themes[i]->GetId() == id)
            return ApplyByIndex(i);
    }
    return false;
}

bool CEditorThemeManager::ApplyByIndex(size_t index)
{
    if (index >= m_themes.size())
        return false;
    if (!m_themes[index])
        return false;
    m_themes[index]->Apply();
    m_currentIndex = index;
    return true;
}

const CBaseEditorTheme *CEditorThemeManager::GetCurrent() const
{
    if (m_currentIndex == (size_t)-1 || m_currentIndex >= m_themes.size())
        return nullptr;
    return m_themes[m_currentIndex].get();
}

size_t CEditorThemeManager::GetCount() const
{
    return m_themes.size();
}

std::vector<std::string_view> CEditorThemeManager::GetIds() const
{
    std::vector<std::string_view> out;
    out.reserve(m_themes.size());
    for (const auto &t : m_themes)
        out.emplace_back(t ? t->GetId() : std::string_view{});
    return out;
}

std::vector<std::string_view> CEditorThemeManager::GetDisplayNames() const
{
    std::vector<std::string_view> out;
    out.reserve(m_themes.size());
    for (const auto &t : m_themes)
        out.emplace_back(t ? t->GetDisplayName() : std::string_view{});
    return out;
}
