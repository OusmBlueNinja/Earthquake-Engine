#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

namespace editor
{
    struct CEditorProject
    {
        uint32_t version = 1;
        std::string name;

        std::filesystem::path project_file;
        std::filesystem::path root_dir;

        std::filesystem::path assets_dir;
        std::filesystem::path cache_dir;
        std::filesystem::path scenes_dir;

        std::filesystem::path startup_scene;
    };

    class CEditorProjectManager
    {
    public:
        struct RecentProject
        {
            std::filesystem::path project_file;
        };

        CEditorProjectManager();

        bool CreateProject(const std::filesystem::path& root_dir, const std::string& name);
        bool OpenProject(const std::filesystem::path& project_file);
        bool SaveProject();
        void CloseProject();

        bool HasOpenProject() const;
        const CEditorProject* GetProject() const;

        const std::vector<RecentProject>& GetRecentProjects() const;

    private:
        bool EnsureProjectDirs(const CEditorProject& p);
        bool WriteProjectFile(const CEditorProject& p);
        bool ReadProjectFile(CEditorProject& out, const std::filesystem::path& project_file);

        void LoadRecentProjects();
        void SaveRecentProjects();
        void TouchRecent(const std::filesystem::path& project_file);
        void PruneRecent();

        static std::filesystem::path DefaultProjectFilePath(const std::filesystem::path& root_dir, const std::string& name);
        static std::filesystem::path GetUserConfigDir(const std::string& app_name);

        static std::string Trim(const std::string& s);
        static bool ParseKeyValueLine(const std::string& line, std::string& k, std::string& v);
        static std::string PathToUtf8(const std::filesystem::path& p);

    private:
        CEditorProject m_project;
        bool m_has_project = false;

        std::vector<RecentProject> m_recent;
        size_t m_max_recent = 12;

        std::filesystem::path m_recent_file;
    };
}
