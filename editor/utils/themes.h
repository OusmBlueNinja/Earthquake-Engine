#pragma once
#include <imgui.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class CBaseEditorTheme
{
public:
    virtual ~CBaseEditorTheme() = default;
    virtual std::string_view GetId() const = 0;
    virtual std::string_view GetDisplayName() const = 0;
    virtual void Apply() const = 0;
};

class CEditorThemeManager
{
public:
    CEditorThemeManager() = default;

    void Register(std::unique_ptr<CBaseEditorTheme> theme);
    void RegisterBuiltins();

    bool ApplyById(std::string_view id);
    bool ApplyByIndex(size_t index);

    const CBaseEditorTheme* GetCurrent() const;
    size_t GetCount() const;

    std::vector<std::string_view> GetIds() const;
    std::vector<std::string_view> GetDisplayNames() const;

private:
    const CBaseEditorTheme* FindById(std::string_view id) const;

private:
    std::vector<std::unique_ptr<CBaseEditorTheme>> m_themes;
    size_t m_currentIndex = (size_t)-1;
};
