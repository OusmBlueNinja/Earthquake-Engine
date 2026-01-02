#pragma once

#include <filesystem>

extern "C"
{
#include "core/systems/ecs/world.h"
#include "managers/asset_manager/asset_manager.h"
}

namespace editor
{
    class CEditorProject;

    class CEditorSceneManager final
    {
    public:
        void Clear();

        void SetScenePath(const std::filesystem::path &p);
        const std::filesystem::path &GetScenePath() const { return m_scene_path; }
        bool HasScenePath() const { return !m_scene_path.empty(); }

        void SetAutosaveEnabled(bool on) { m_autosave_enabled = on; }
        bool GetAutosaveEnabled() const { return m_autosave_enabled; }
        void SetAutosaveIntervalSeconds(float s);
        float GetAutosaveIntervalSeconds() const { return m_autosave_interval_s; }

        bool IsDirty() const { return m_dirty; }
        void MarkDirty();
        void ClearDirty();

        bool SaveNow(const ecs_world_t *w, const asset_manager_t *am);
        bool LoadNow(ecs_world_t *w, asset_manager_t *am, const std::filesystem::path &path);

        void TickAutosave(float dt, const ecs_world_t *w, const asset_manager_t *am);

    private:
        bool SaveToPathAtomic(const ecs_world_t *w, const asset_manager_t *am, const std::filesystem::path &path);

        std::filesystem::path m_scene_path;
        bool m_dirty = false;

        bool m_autosave_enabled = true;
        float m_autosave_interval_s = 2.0f;
        float m_autosave_accum_s = 0.0f;
    };
}

