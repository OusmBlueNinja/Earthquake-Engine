#include "CEditorProjectManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace editor
{
    static std::filesystem::path make_abs_norm(const std::filesystem::path &p)
    {
        std::error_code ec;
        auto a = std::filesystem::absolute(p, ec);
        if (ec)
            return p.lexically_normal();
        return a.lexically_normal();
    }

    static bool write_kv(std::ofstream &f, const std::string &k, const std::string &v)
    {
        if (!f.good())
            return false;
        f << k << "=" << v << "\n";
        return f.good();
    }

    CEditorProjectManager::CEditorProjectManager()
    {
        auto cfg = GetUserConfigDir("GigabiteEditor");
        std::error_code ec;
        std::filesystem::create_directories(cfg, ec);
        m_recent_file = cfg / "recent_projects.txt";
        LoadRecentProjects();
    }

    bool CEditorProjectManager::CreateProject(const std::filesystem::path &root_dir, const std::string &name)
    {
        if (name.empty())
            return false;

        CEditorProject p;
        p.version = 1;
        p.name = name;
        p.root_dir = make_abs_norm(root_dir);
        p.project_file = DefaultProjectFilePath(p.root_dir, p.name);

        p.assets_dir = p.root_dir / "Assets";
        p.cache_dir = p.root_dir / "Cache";
        p.scenes_dir = p.root_dir / "Scenes";
        p.startup_scene = p.scenes_dir / "startup.scene";

        std::error_code ec;
        std::filesystem::create_directories(p.root_dir, ec);
        if (ec)
            return false;

        if (!EnsureProjectDirs(p))
            return false;

        if (!WriteProjectFile(p))
            return false;

        m_project = p;
        m_has_project = true;

        TouchRecent(m_project.project_file);
        SaveRecentProjects();

        return true;
    }

    bool CEditorProjectManager::OpenProject(const std::filesystem::path &project_file)
    {
        CEditorProject p;
        if (!ReadProjectFile(p, project_file))
            return false;

        if (!EnsureProjectDirs(p))
            return false;

        m_project = p;
        m_has_project = true;

        TouchRecent(m_project.project_file);
        SaveRecentProjects();

        return true;
    }

    bool CEditorProjectManager::SaveProject()
    {
        if (!m_has_project)
            return false;

        if (!EnsureProjectDirs(m_project))
            return false;

        if (!WriteProjectFile(m_project))
            return false;

        TouchRecent(m_project.project_file);
        SaveRecentProjects();

        return true;
    }

    void CEditorProjectManager::CloseProject()
    {
        m_has_project = false;
        m_project = CEditorProject{};
    }

    bool CEditorProjectManager::HasOpenProject() const
    {
        return m_has_project;
    }

    const CEditorProject *CEditorProjectManager::GetProject() const
    {
        return m_has_project ? &m_project : nullptr;
    }

    const std::vector<CEditorProjectManager::RecentProject> &CEditorProjectManager::GetRecentProjects() const
    {
        return m_recent;
    }

    bool CEditorProjectManager::EnsureProjectDirs(const CEditorProject &p)
    {
        std::error_code ec;
        std::filesystem::create_directories(p.root_dir, ec);
        if (ec)
            return false;
        std::filesystem::create_directories(p.assets_dir, ec);
        if (ec)
            return false;
        std::filesystem::create_directories(p.cache_dir, ec);
        if (ec)
            return false;
        std::filesystem::create_directories(p.scenes_dir, ec);
        if (ec)
            return false;
        return true;
    }

    std::filesystem::path CEditorProjectManager::DefaultProjectFilePath(const std::filesystem::path &root_dir, const std::string &name)
    {
        auto safe = name;
        for (auto &c : safe)
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }
        return make_abs_norm(root_dir) / (safe + ".gproj");
    }

    std::filesystem::path CEditorProjectManager::GetUserConfigDir(const std::string &app_name)
    {
#if defined(_WIN32)
        const char *appdata = std::getenv("APPDATA");
        if (appdata && appdata[0])
            return std::filesystem::path(appdata) / app_name;
        const char *userprofile = std::getenv("USERPROFILE");
        if (userprofile && userprofile[0])
            return std::filesystem::path(userprofile) / "AppData" / "Roaming" / app_name;
        return std::filesystem::current_path() / app_name;
#elif defined(__APPLE__)
        const char *home = std::getenv("HOME");
        if (home && home[0])
            return std::filesystem::path(home) / "Library" / "Application Support" / app_name;
        return std::filesystem::current_path() / app_name;
#else
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0])
            return std::filesystem::path(xdg) / app_name;
        const char *home = std::getenv("HOME");
        if (home && home[0])
            return std::filesystem::path(home) / ".config" / app_name;
        return std::filesystem::current_path() / app_name;
#endif
    }

    std::string CEditorProjectManager::PathToUtf8(const std::filesystem::path &p)
    {
#if defined(_WIN32)
        return p.u8string();
#else
        return p.string();
#endif
    }

    bool CEditorProjectManager::WriteProjectFile(const CEditorProject &p)
    {
        std::error_code ec;
        std::filesystem::create_directories(p.project_file.parent_path(), ec);
        if (ec)
            return false;

        std::ofstream f(p.project_file, std::ios::binary);
        if (!f.is_open())
            return false;

        if (!write_kv(f, "version", std::to_string(p.version)))
            return false;
        if (!write_kv(f, "name", p.name))
            return false;

        if (!write_kv(f, "root_dir", PathToUtf8(p.root_dir)))
            return false;
        if (!write_kv(f, "assets_dir", PathToUtf8(p.assets_dir)))
            return false;
        if (!write_kv(f, "cache_dir", PathToUtf8(p.cache_dir)))
            return false;
        if (!write_kv(f, "scenes_dir", PathToUtf8(p.scenes_dir)))
            return false;
        if (!write_kv(f, "startup_scene", PathToUtf8(p.startup_scene)))
            return false;

        return true;
    }

    std::string CEditorProjectManager::Trim(const std::string &s)
    {
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
            ++b;
        size_t e = s.size();
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
            --e;
        return s.substr(b, e - b);
    }

    bool CEditorProjectManager::ParseKeyValueLine(const std::string &line, std::string &k, std::string &v)
    {
        auto s = Trim(line);
        if (s.empty())
            return false;
        if (s[0] == '#')
            return false;
        if (s.size() >= 2 && s[0] == '/' && s[1] == '/')
            return false;

        auto pos = s.find('=');
        if (pos == std::string::npos)
            return false;

        k = Trim(s.substr(0, pos));
        v = Trim(s.substr(pos + 1));
        if (k.empty())
            return false;
        return true;
    }

    bool CEditorProjectManager::ReadProjectFile(CEditorProject &out, const std::filesystem::path &project_file)
    {
        auto pf = make_abs_norm(project_file);

        std::ifstream f(pf, std::ios::binary);
        if (!f.is_open())
            return false;

        CEditorProject p;
        p.project_file = pf;

        std::string line;
        while (std::getline(f, line))
        {
            std::string k, v;
            if (!ParseKeyValueLine(line, k, v))
                continue;

            if (k == "version")
                p.version = (uint32_t)std::stoul(v);
            else if (k == "name")
                p.name = v;
            else if (k == "root_dir")
                p.root_dir = v;
            else if (k == "assets_dir")
                p.assets_dir = v;
            else if (k == "cache_dir")
                p.cache_dir = v;
            else if (k == "scenes_dir")
                p.scenes_dir = v;
            else if (k == "startup_scene")
                p.startup_scene = v;
        }

        if (p.name.empty())
            return false;

        if (p.root_dir.empty())
            p.root_dir = pf.parent_path();

        p.root_dir = make_abs_norm(p.root_dir);

        if (p.assets_dir.empty())
            p.assets_dir = p.root_dir / "Assets";
        if (p.cache_dir.empty())
            p.cache_dir = p.root_dir / "Cache";
        if (p.scenes_dir.empty())
            p.scenes_dir = p.root_dir / "Scenes";
        if (p.startup_scene.empty())
            p.startup_scene = p.scenes_dir / "startup.scene";

        p.assets_dir = make_abs_norm(p.assets_dir);
        p.cache_dir = make_abs_norm(p.cache_dir);
        p.scenes_dir = make_abs_norm(p.scenes_dir);
        p.startup_scene = make_abs_norm(p.startup_scene);

        out = p;
        return true;
    }

    void CEditorProjectManager::LoadRecentProjects()
    {
        m_recent.clear();

        std::ifstream f(m_recent_file, std::ios::binary);
        if (!f.is_open())
            return;

        std::string line;
        while (std::getline(f, line))
        {
            auto s = Trim(line);
            if (s.empty())
                continue;
            if (s[0] == '#')
                continue;

            RecentProject rp;
            rp.project_file = make_abs_norm(std::filesystem::path(s));
            m_recent.push_back(rp);
        }

        std::vector<RecentProject> unique;
        unique.reserve(m_recent.size());

        for (auto &r : m_recent)
        {
            bool found = false;
            for (auto &u : unique)
            {
                if (u.project_file == r.project_file)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                unique.push_back(r);
        }

        m_recent = std::move(unique);
        PruneRecent();
    }

    void CEditorProjectManager::SaveRecentProjects()
    {
        std::error_code ec;
        std::filesystem::create_directories(m_recent_file.parent_path(), ec);

        std::ofstream f(m_recent_file, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return;

        for (auto &r : m_recent)
            f << PathToUtf8(r.project_file) << "\n";
    }

    void CEditorProjectManager::TouchRecent(const std::filesystem::path &project_file)
    {
        auto pf = make_abs_norm(project_file);

        m_recent.erase(
            std::remove_if(m_recent.begin(), m_recent.end(),
                           [&](const RecentProject &r)
                           { return r.project_file == pf; }),
            m_recent.end());

        RecentProject rp;
        rp.project_file = pf;
        m_recent.insert(m_recent.begin(), rp);

        PruneRecent();
    }

    void CEditorProjectManager::PruneRecent()
    {
        if (m_recent.size() > m_max_recent)
            m_recent.resize(m_max_recent);
    }
}
