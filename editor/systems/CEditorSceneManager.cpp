#include "editor/systems/CEditorSceneManager.h"

extern "C"
{
#include "core/systems/ecs/scene_yaml.h"
}

#include <system_error>

namespace editor
{
    void CEditorSceneManager::Clear()
    {
        m_scene_path.clear();
        m_dirty = false;
        m_autosave_accum_s = 0.0f;
    }

    void CEditorSceneManager::SetScenePath(const std::filesystem::path &p)
    {
        m_scene_path = p;
        m_autosave_accum_s = 0.0f;
    }

    void CEditorSceneManager::SetAutosaveIntervalSeconds(float s)
    {
        if (s < 0.1f)
            s = 0.1f;
        if (s > 120.0f)
            s = 120.0f;
        m_autosave_interval_s = s;
    }

    void CEditorSceneManager::MarkDirty()
    {
        m_dirty = true;
    }

    void CEditorSceneManager::ClearDirty()
    {
        m_dirty = false;
    }

    bool CEditorSceneManager::SaveToPathAtomic(const ecs_world_t *w, const asset_manager_t *am, const std::filesystem::path &path)
    {
        if (!w)
            return false;
        if (path.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        auto tmp = path;
        tmp += ".tmp";

        auto tmp_s = tmp.string();
        if (!ecs_scene_save_yaml_file(w, tmp_s.c_str(), am))
            return false;

        std::filesystem::rename(tmp, path, ec);
        if (ec)
        {
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tmp, path, ec);
        }

        if (ec)
        {
            std::filesystem::remove(tmp, ec);
            return false;
        }

        return true;
    }

    bool CEditorSceneManager::SaveNow(const ecs_world_t *w, const asset_manager_t *am)
    {
        if (!HasScenePath())
            return false;
        if (!SaveToPathAtomic(w, am, m_scene_path))
            return false;
        m_dirty = false;
        m_autosave_accum_s = 0.0f;
        return true;
    }

    bool CEditorSceneManager::LoadNow(ecs_world_t *w, asset_manager_t *am, const std::filesystem::path &path)
    {
        if (!w)
            return false;
        if (path.empty())
            return false;

        auto s = path.string();
        if (!ecs_scene_load_yaml_file(w, s.c_str(), am))
            return false;

        m_scene_path = path;
        m_dirty = false;
        m_autosave_accum_s = 0.0f;
        return true;
    }

    void CEditorSceneManager::TickAutosave(float dt, const ecs_world_t *w, const asset_manager_t *am)
    {
        if (!m_autosave_enabled)
            return;
        if (!m_dirty)
            return;
        if (!HasScenePath())
            return;
        if (!w)
            return;

        if (dt < 0.0f)
            dt = 0.0f;
        if (dt > 0.25f)
            dt = 0.25f;

        m_autosave_accum_s += dt;
        if (m_autosave_accum_s < m_autosave_interval_s)
            return;

        (void)SaveNow(w, am);
    }
}
