#pragma once

#include <stdint.h>
#include <stddef.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

#include "editor/windows/CBaseWindow.h"

extern "C"
{
#include "handle.h"
#include "asset_manager/asset_types.h"
#include "asset_manager/asset_manager.h"
}

namespace editor
{
    struct CEditorContext;

    struct asset_browser_drag_payload_t
    {
        ihandle_t handle;
        asset_type_t type;
    };

    class CAssetBrowserWindow final : public CBaseWindow
    {
    public:
        struct callbacks_t
        {
            asset_type_t (*resolve_type_from_path)(const std::filesystem::path &abs_path) = nullptr;
            ihandle_t (*resolve_handle)(asset_manager_t *am, asset_type_t type, const std::filesystem::path &abs_path) = nullptr;
            uint32_t (*resolve_preview_tex)(asset_manager_t *am, ihandle_t h, asset_type_t type, const std::filesystem::path &abs_path) = nullptr;

            bool (*rename_file)(asset_manager_t *am, const std::filesystem::path &abs_old, const std::filesystem::path &abs_new) = nullptr;
            bool (*delete_file)(asset_manager_t *am, const std::filesystem::path &abs_path) = nullptr;
        };

        CAssetBrowserWindow();
        ~CAssetBrowserWindow();

        void SetAssetManager(asset_manager_t *am);
        void SetProjectRoot(const std::filesystem::path &abs_project_root);
        void SetScanRoot(const std::filesystem::path &abs_scan_root);

        void SetScanIntervalMs(uint32_t ms);
        void SetTileSize(float px);
        void SetCallbacks(const callbacks_t &cb);

    private:
        bool BeginImpl() override;
        void EndImpl() override;
        void OnTick(float dt, CEditorContext *ctx) override;

    private:
        struct file_stamp_t
        {
            uint64_t write_time = 0;
            uint64_t file_size = 0;
        };

        struct item_t
        {
            uint64_t id = 0;
            std::filesystem::path abs_path;
            std::filesystem::path rel_path;
            std::string name;
            std::string ext;
            asset_type_t type = ASSET_NONE;
            ihandle_t handle{};
            uint8_t handle_valid = 0;
            uint32_t preview_tex = 0;
            uint8_t has_preview = 0;
            file_stamp_t stamp{};
        };

        struct folder_node_t
        {
            std::string name;
            std::filesystem::path rel_path;
            std::unordered_map<std::string, folder_node_t *> children;
            std::vector<uint64_t> items;
        };

        struct pending_snapshot_t
        {
            std::vector<item_t> items;
            std::vector<std::filesystem::path> folders_rel;
            uint8_t valid = 0;
        };

        void Draw();
        void DrawFolderTreePanel();
        void DrawGridPanel();
        void DrawFolderNode(folder_node_t *n, bool root);

        static uint32_t DefaultResolvePreviewTex(asset_manager_t *am, ihandle_t h, asset_type_t type, const std::filesystem::path &abs_path);

        void EnsureScanner();
        void StopScanner();
        void ScannerThreadMain();

        void ApplyPendingSnapshot();
        void RebuildFolderTree();

        bool EnsureHandle(item_t &it);
        bool EnsurePreview(item_t &it);

        void BeginRename(uint64_t id);
        void CommitRename();
        void CancelRename();

        void RequestDelete(uint64_t id);
        void PerformDeleteIfConfirmed();

        static uint64_t HashPath64(const std::filesystem::path &p);
        static uint64_t FileTimeToU64(std::filesystem::file_time_type ft);

        static std::string ToLower(std::string s);
        static std::string StemString(const std::filesystem::path &p);

        static asset_type_t DefaultResolveTypeFromPath(const std::filesystem::path &abs_path);
        static ihandle_t DefaultResolveHandle(asset_manager_t *am, asset_type_t type, const std::filesystem::path &abs_path);
        static bool DefaultRenameFile(asset_manager_t *am, const std::filesystem::path &abs_old, const std::filesystem::path &abs_new);
        static bool DefaultDeleteFile(asset_manager_t *am, const std::filesystem::path &abs_path);

        const char *TypeLabel(asset_type_t t) const;

    private:
        asset_manager_t *m_am = nullptr;

        std::filesystem::path m_project_root_abs;
        std::filesystem::path m_scan_root_abs;

        uint32_t m_scan_interval_ms = 500;
        float m_tile_size = 96.0f;

        callbacks_t m_cb{};

        std::thread m_scan_thread;
        std::atomic<uint8_t> m_scan_run{0};

        std::mutex m_pending_mtx;
        pending_snapshot_t m_pending;
        std::atomic<uint8_t> m_pending_dirty{0};

        std::vector<item_t> m_items;
        std::unordered_map<uint64_t, size_t> m_item_index;

        std::vector<std::filesystem::path> m_folders_rel;

        std::vector<std::unique_ptr<folder_node_t>> m_nodes;
        folder_node_t *m_root = nullptr;

        std::filesystem::path m_current_folder_rel;

        uint64_t m_rename_id = 0;
        char m_rename_buf[256];

        uint64_t m_delete_id = 0;
        uint8_t m_delete_modal_open = 0;

        uint64_t m_selected_id = 0;
    };
}
