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
        // Windark style by DestroyerDarkNess from ImThemes
        // With some minor changes
        ImGuiStyle &style = ImGui::GetStyle();

        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.6000000238418579f;
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.WindowRounding = 8.399999618530273f;
        style.WindowBorderSize = 1.0f;
        style.WindowMinSize = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Right;
        style.ChildRounding = 3.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupRounding = 3.0f;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = ImVec2(4.0f, 3.0f);
        style.FrameRounding = 3.0f;
        style.FrameBorderSize = 1.0f;
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
        style.CellPadding = ImVec2(4.0f, 2.0f);
        style.IndentSpacing = 21.0f;
        style.ColumnsMinSpacing = 6.0f;
        style.ScrollbarSize = 5.599999904632568f;
        style.ScrollbarRounding = 18.0f;
        style.GrabMinSize = 10.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.TabBorderSize = 0.0f;

        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

        style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f,
                                                     1.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.125490203499794f, 0.125490203499794f, 0.125490203499794f, 1.0f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.125490203499794f, 0.125490203499794f, 0.125490203499794f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f, 1.0f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f, 1.0f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2156862765550613f, 0.2156862765550613f,
                                                       0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f,
                                                      1.0f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.125490203499794f, 0.125490203499794f, 0.125490203499794f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f,
                                                      1.0f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.125490203499794f, 0.125490203499794f, 0.125490203499794f,
                                                         1.0f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.125490203499794f, 0.125490203499794f, 0.125490203499794f,
                                                    1.0f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f,
                                                      1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3019607961177826f, 0.3019607961177826f,
                                                             0.3019607961177826f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.3490196168422699f, 0.3490196168422699f,
                                                            0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 0.4705882370471954f, 0.843137264251709f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.0f, 0.4705882370471954f, 0.843137264251709f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.3294117748737335f, 0.6000000238418579f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f,
                                                      1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f,
                                                     1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f,
                                                      1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.3019607961177826f, 0.3019607961177826f, 0.3019607961177826f,
                                                     1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f,
                                                  1.0f);
        style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f,
                                                         1.0f);
        style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.3019607961177826f, 0.3019607961177826f,
                                                        0.3019607961177826f, 1.0f);
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f,
                                                   1.0f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.250980406999588f, 0.250980406999588f,
                                                          0.250980406999588f, 1.0f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.3019607961177826f, 0.3019607961177826f,
                                                         0.3019607961177826f, 1.0f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f, 1.0f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2156862765550613f, 0.2156862765550613f, 0.2156862765550613f,
                                                   1.0f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.250980406999588f, 0.250980406999588f, 0.250980406999588f, 1.0f);
        style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.168627455830574f, 0.168627455830574f, 0.168627455830574f,
                                                     1.0f);
        style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.2156862765550613f, 0.2156862765550613f,
                                                           0.2156862765550613f, 1.0f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.0f, 0.4705882370471954f, 0.843137264251709f, 1.0f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.0f, 0.3294117748737335f, 0.6000000238418579f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.0f, 0.4705882370471954f, 0.843137264251709f, 1.0f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.0f, 0.3294117748737335f, 0.6000000238418579f, 1.0f);
        style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f,
                                                      1.0f);
        style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f,
                                                          0.3490196168422699f, 1.0f);
        style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f,
                                                         0.2470588237047195f, 1.0f);
        style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_TableRowBgAlt] = style.Colors[ImGuiCol_TableRowBg];
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.4705882370471954f, 0.843137264251709f, 1.0f);
        style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.20f, 0.45f, 0.85f, 0.70f);

        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f,
                                                     1.0f);
        style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
        style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f,
                                                          0.800000011920929f, 0.2000000029802322f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f,
                                                         0.3499999940395355f);
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
