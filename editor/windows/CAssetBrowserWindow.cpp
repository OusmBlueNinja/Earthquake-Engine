#include "editor/windows/CAssetBrowserWindow.h"

#include <chrono>
#include <algorithm>
#include <string.h>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <filesystem>
#include <new>
#include <inttypes.h>

extern "C"
{
#include "asset_manager/asset_manager.h"
}

#include "imgui.h"
#include "editor/CEditorContext.h"

namespace editor
{
    

    static bool is_dir_ignored_rel(const std::filesystem::path& rel)
    {
        try
        {
            auto s = rel.generic_string();
            if (s.empty()) return false;
            if (s.find("/Cache") != std::string::npos) return true;
            if (s.find("/.git") != std::string::npos) return true;
            if (s.find("/build") != std::string::npos) return true;
            if (s.find("/bin") != std::string::npos) return true;
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    static std::filesystem::path make_abs_norm(const std::filesystem::path& p)
    {
        std::error_code ec;
        auto a = std::filesystem::absolute(p, ec);
        if (ec) return p.lexically_normal();
        return a.lexically_normal();
    }

    CAssetBrowserWindow::CAssetBrowserWindow()
        : CBaseWindow("Asset Browser")
    {
        m_cb.resolve_type_from_path = &CAssetBrowserWindow::DefaultResolveTypeFromPath;
        m_cb.resolve_handle = &CAssetBrowserWindow::DefaultResolveHandle;
        m_cb.resolve_preview_tex = &CAssetBrowserWindow::DefaultResolvePreviewTex;
        m_cb.rename_file = &CAssetBrowserWindow::DefaultRenameFile;
        m_cb.delete_file = &CAssetBrowserWindow::DefaultDeleteFile;

        m_rename_buf[0] = 0;
    }

    CAssetBrowserWindow::~CAssetBrowserWindow()
    {
        StopScanner();
    }

    bool CAssetBrowserWindow::BeginImpl()
    {
        return ImGui::Begin("Asset Browser");
    }

    void CAssetBrowserWindow::EndImpl()
    {
        ImGui::End();
    }

    void CAssetBrowserWindow::OnTick(float, CEditorContext*)
    {
        Draw();
    }

    void CAssetBrowserWindow::SetAssetManager(asset_manager_t* am)
    {
        m_am = am;
    }

    void CAssetBrowserWindow::SetProjectRoot(const std::filesystem::path& abs_project_root)
    {
        if (abs_project_root.empty())
        {
            StopScanner();
            m_project_root_abs.clear();
            m_scan_root_abs.clear();
            m_pending_dirty.store(0);
            m_items.clear();
            m_item_index.clear();
            m_folders_rel.clear();
            m_nodes.clear();
            m_root = nullptr;
            m_current_folder_rel.clear();
            m_selected_id = 0;
            CancelRename();
            m_delete_id = 0;
            m_delete_modal_open = 0;
            return;
        }

        m_project_root_abs = make_abs_norm(abs_project_root);
        if (m_scan_root_abs.empty())
        {
            auto assets = m_project_root_abs / "Assets";
            std::error_code ec;
            if (std::filesystem::exists(assets, ec) && std::filesystem::is_directory(assets, ec))
                m_scan_root_abs = assets;
            else
                m_scan_root_abs = m_project_root_abs;
        }
        EnsureScanner();
    }

    void CAssetBrowserWindow::SetScanRoot(const std::filesystem::path& abs_scan_root)
    {
        if (abs_scan_root.empty())
        {
            StopScanner();
            m_scan_root_abs.clear();
            m_pending_dirty.store(0);
            m_items.clear();
            m_item_index.clear();
            m_folders_rel.clear();
            m_nodes.clear();
            m_root = nullptr;
            m_current_folder_rel.clear();
            m_selected_id = 0;
            CancelRename();
            m_delete_id = 0;
            m_delete_modal_open = 0;
            return;
        }

        m_scan_root_abs = make_abs_norm(abs_scan_root);
        EnsureScanner();
    }

    void CAssetBrowserWindow::SetScanIntervalMs(uint32_t ms)
    {
        m_scan_interval_ms = ms ? ms : 250;
    }

    void CAssetBrowserWindow::SetTileSize(float px)
    {
        m_tile_size = (px < 48.0f) ? 48.0f : px;
    }

    void CAssetBrowserWindow::SetCallbacks(const callbacks_t& cb)
    {
        if (cb.resolve_type_from_path) m_cb.resolve_type_from_path = cb.resolve_type_from_path;
        if (cb.resolve_handle) m_cb.resolve_handle = cb.resolve_handle;
        if (cb.resolve_preview_tex) m_cb.resolve_preview_tex = cb.resolve_preview_tex;
        if (cb.rename_file) m_cb.rename_file = cb.rename_file;
        if (cb.delete_file) m_cb.delete_file = cb.delete_file;
    }

    void CAssetBrowserWindow::EnsureScanner()
    {
        if (m_scan_root_abs.empty())
            return;

        if (m_scan_run.load())
            return;

        m_scan_run.store(1);
        m_scan_thread = std::thread(&CAssetBrowserWindow::ScannerThreadMain, this);
    }

    void CAssetBrowserWindow::StopScanner()
    {
        if (!m_scan_run.load())
            return;

        m_scan_run.store(0);
        if (m_scan_thread.joinable())
            m_scan_thread.join();
    }

    uint64_t CAssetBrowserWindow::FileTimeToU64(std::filesystem::file_time_type ft)
    {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch()).count();
        return (uint64_t)ns;
    }

    std::string CAssetBrowserWindow::ToLower(std::string s)
    {
        for (auto& c : s)
        {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        }
        return s;
    }

    std::string CAssetBrowserWindow::StemString(const std::filesystem::path& p)
    {
        return p.stem().string();
    }

    uint64_t CAssetBrowserWindow::HashPath64(const std::filesystem::path& p)
    {
        try
        {
            auto s = p.generic_string();
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c : s)
            {
                h ^= (uint64_t)c;
                h *= 1099511628211ull;
            }
            return h;
        }
        catch (...)
        {
            // Best-effort: stable-ish fallback (avoids scan aborts on allocation/encoding failures).
            return 0;
        }
    }

    asset_type_t CAssetBrowserWindow::DefaultResolveTypeFromPath(const std::filesystem::path& abs_path)
    {
        auto ext = ToLower(abs_path.extension().string());
        auto fname = ToLower(abs_path.filename().string());
        if (ext == ".itex") return ASSET_IMAGE;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".hdr") return ASSET_IMAGE;
        if (ext == ".imesh" || ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") return ASSET_MODEL;
        if (ext == ".ikv" || ext == ".imat" || ext == ".material") return ASSET_MATERIAL;
        if (ext == ".scene") return ASSET_SCENE;
        if (fname.size() >= 10 && fname.rfind(".scene.yaml") == fname.size() - 10) return ASSET_SCENE;
        if (fname.size() >= 9 && fname.rfind(".scene.yml") == fname.size() - 9) return ASSET_SCENE;

        if (ext == ".iasset")
        {
            auto stem = abs_path.stem().string();
            auto us = stem.find('_');
            if (us != std::string::npos)
            {
                auto prefix = stem.substr(0, us);
                for (uint32_t i = 0; i < (uint32_t)ASSET_MAX; ++i)
                {
                    if (prefix == g_asset_type_names[i])
                        return (asset_type_t)i;
                }
            }
        }

        return ASSET_NONE;
    }

    ihandle_t CAssetBrowserWindow::DefaultResolveHandle(asset_manager_t* am, asset_type_t type, const std::filesystem::path& abs_path)
    {
        if (!am) return ihandle_invalid();
        if (type == ASSET_NONE) return ihandle_invalid();
        auto s = abs_path.string();
        return asset_manager_request(am, type, s.c_str());
    }

    uint32_t CAssetBrowserWindow::DefaultResolvePreviewTex(asset_manager_t* am, ihandle_t h, asset_type_t type, const std::filesystem::path&)
    {
        if (!am) return 0;
        if (!ihandle_is_valid(h)) return 0;
        if (type != ASSET_IMAGE) return 0;

        const asset_any_t* a = asset_manager_get_any(am, h);
        if (!a) return 0;
        if (a->type != ASSET_IMAGE) return 0;
        if (a->state != ASSET_STATE_READY) return 0;

        return (uint32_t)a->as.image.gl_handle;
    }

    bool CAssetBrowserWindow::DefaultRenameFile(asset_manager_t*, const std::filesystem::path& abs_old, const std::filesystem::path& abs_new)
    {
        std::error_code ec;
        if (std::filesystem::exists(abs_new, ec)) return false;
        std::filesystem::rename(abs_old, abs_new, ec);
        return !ec;
    }

    bool CAssetBrowserWindow::DefaultDeleteFile(asset_manager_t*, const std::filesystem::path& abs_path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(abs_path, ec)) return false;
        if (std::filesystem::is_directory(abs_path, ec)) return false;
        std::filesystem::remove(abs_path, ec);
        return !ec;
    }

    const char* CAssetBrowserWindow::TypeLabel(asset_type_t t) const
    {
        return ASSET_TYPE_TO_STRING(t);
    }

    void CAssetBrowserWindow::ScannerThreadMain()
    {
        std::unordered_map<std::filesystem::path, file_stamp_t> prev;
        uint32_t missing_scans = 0;

        while (m_scan_run.load())
        {
            pending_snapshot_t snap;
            std::unordered_map<std::filesystem::path, file_stamp_t> now;

            std::error_code ec;
            bool scan_ok = false;
            bool root_exists = false;

            try
            {
                if (!m_scan_root_abs.empty())
                {
                    root_exists = std::filesystem::exists(m_scan_root_abs, ec) && std::filesystem::is_directory(m_scan_root_abs, ec);
                    if (ec)
                    {
                        ec.clear();
                    }
                    else if (root_exists)
                    {
                        std::filesystem::recursive_directory_iterator it(m_scan_root_abs, std::filesystem::directory_options::skip_permission_denied, ec);
                        std::filesystem::recursive_directory_iterator end;
                        if (!ec)
                        {
                            scan_ok = true;

                            for (; it != end && m_scan_run.load(); it.increment(ec))
                            {
                                if (ec)
                                {
                                    ec.clear();
                                    continue;
                                }

                                auto p = it->path();
                                auto rel = std::filesystem::relative(p, m_scan_root_abs, ec);
                                if (ec)
                                {
                                    ec.clear();
                                    continue;
                                }

                                if (is_dir_ignored_rel(rel))
                                {
                                    if (it->is_directory(ec))
                                        it.disable_recursion_pending();
                                    continue;
                                }

                                if (it->is_directory(ec))
                                {
                                    snap.folders_rel.push_back(rel.lexically_normal());
                                    continue;
                                }

                                if (!it->is_regular_file(ec))
                                    continue;

                                file_stamp_t st;
                                auto ft = std::filesystem::last_write_time(p, ec);
                                if (ec)
                                {
                                    ec.clear();
                                    continue;
                                }
                                st.write_time = FileTimeToU64(ft);

                                auto fs = std::filesystem::file_size(p, ec);
                                if (ec)
                                {
                                    ec.clear();
                                    fs = 0;
                                }
                                st.file_size = (uint64_t)fs;

                                now[p] = st;

                                item_t item;
                                try
                                {
                                    item.abs_path = p.lexically_normal();
                                    item.rel_path = rel.lexically_normal();
                                    item.id = HashPath64(item.rel_path);
                                    item.ext = ToLower(item.abs_path.extension().string());
                                    item.name = StemString(item.abs_path);
                                    item.type = m_cb.resolve_type_from_path ? m_cb.resolve_type_from_path(item.abs_path) : ASSET_NONE;
                                    item.stamp = st;
                                }
                                catch (const std::bad_alloc&)
                                {
                                    LOG_ERROR("AssetBrowser: out of memory while indexing a file (size=%" PRIu64 " bytes)", (uint64_t)st.file_size);
                                    continue;
                                }
                                catch (const std::exception&)
                                {
                                    // Skip problematic entries (e.g. very long/invalid paths, transient IO issues, OOM).
                                    continue;
                                }
                                catch (...)
                                {
                                    continue;
                                }

                                snap.items.push_back(std::move(item));
                            }
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("AssetBrowser scan exception: %s", e.what());
                scan_ok = false;
            }
            catch (...)
            {
                LOG_ERROR("AssetBrowser scan exception: unknown");
                scan_ok = false;
            }

            if (!scan_ok)
            {
                if (!m_scan_root_abs.empty() && !root_exists)
                    missing_scans++;
                else
                    missing_scans = 0;
            }
            else
            {
                missing_scans = 0;
            }

            bool changed = false;
            if (scan_ok)
            {
                if (now.size() != prev.size())
                {
                    changed = true;
                }
                else
                {
                    for (auto& kv : now)
                    {
                        auto it = prev.find(kv.first);
                        if (it == prev.end())
                        {
                            changed = true;
                            break;
                        }
                        if (it->second.write_time != kv.second.write_time || it->second.file_size != kv.second.file_size)
                        {
                            changed = true;
                            break;
                        }
                    }
                }
            }
            else if (missing_scans >= 4 && !prev.empty())
            {
                // Avoid UI flicker if the directory disappears momentarily (e.g. rename/swap): only clear after
                // a few consecutive failed scans.
                changed = true;
                snap.items.clear();
                snap.folders_rel.clear();
            }

            if (changed)
            {
                snap.valid = 1;
                {
                    std::lock_guard<std::mutex> lk(m_pending_mtx);
                    m_pending = std::move(snap);
                }
                m_pending_dirty.store(1);
                prev = std::move(now);
            }

            uint32_t ms = m_scan_interval_ms;
            if (ms < 100) ms = 100;
            for (uint32_t t = 0; t < ms && m_scan_run.load(); t += 25)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    void CAssetBrowserWindow::ApplyPendingSnapshot()
    {
        if (!m_pending_dirty.load())
            return;

        pending_snapshot_t snap;
        {
            std::lock_guard<std::mutex> lk(m_pending_mtx);
            snap = std::move(m_pending);
            m_pending = pending_snapshot_t{};
        }
        m_pending_dirty.store(0);

        if (!snap.valid)
            return;

        struct item_state_t
        {
            file_stamp_t stamp{};
            ihandle_t handle{};
            uint8_t handle_valid = 0;
            uint32_t preview_tex = 0;
            uint8_t has_preview = 0;
        };

        std::unordered_map<uint64_t, item_state_t> prev_state;
        prev_state.reserve(m_items.size());
        for (const auto& it : m_items)
        {
            item_state_t st;
            st.stamp = it.stamp;
            st.handle = it.handle;
            st.handle_valid = it.handle_valid;
            st.preview_tex = it.preview_tex;
            st.has_preview = it.has_preview;
            prev_state[it.id] = st;
        }

        std::vector<item_t> next_items = std::move(snap.items);
        for (auto& it : next_items)
        {
            auto ps = prev_state.find(it.id);
            if (ps == prev_state.end())
                continue;

            const item_state_t& st = ps->second;
            if (st.stamp.write_time == it.stamp.write_time && st.stamp.file_size == it.stamp.file_size)
            {
                it.handle = st.handle;
                it.handle_valid = st.handle_valid;
                it.preview_tex = st.preview_tex;
                it.has_preview = st.has_preview;
            }
        }

        m_items = std::move(next_items);
        m_folders_rel = std::move(snap.folders_rel);

        m_item_index.clear();
        m_item_index.reserve(m_items.size());
        for (size_t i = 0; i < m_items.size(); ++i)
            m_item_index[m_items[i].id] = i;

        if (m_selected_id != 0 && m_item_index.find(m_selected_id) == m_item_index.end())
            m_selected_id = 0;

        RebuildFolderTree();
    }

    void CAssetBrowserWindow::RebuildFolderTree()
    {
        m_nodes.clear();
        m_root = nullptr;

        auto make_node = [&](const std::string& name, const std::filesystem::path& rel) -> folder_node_t*
        {
            m_nodes.emplace_back(std::make_unique<folder_node_t>());
            auto* n = m_nodes.back().get();
            n->name = name;
            n->rel_path = rel;
            return n;
        };

        m_root = make_node("Assets", std::filesystem::path(""));

        auto get_or_make_child = [&](folder_node_t* parent, const std::string& name, const std::filesystem::path& rel) -> folder_node_t*
        {
            auto it = parent->children.find(name);
            if (it != parent->children.end())
                return it->second;
            auto* c = make_node(name, rel);
            parent->children[name] = c;
            return c;
        };

        for (auto& f : m_folders_rel)
        {
            auto rel = f.lexically_normal();
            if (rel.empty()) continue;

            folder_node_t* cur = m_root;
            std::filesystem::path accum;

            for (auto& part : rel)
            {
                auto s = part.string();
                if (s.empty()) continue;
                accum /= s;
                cur = get_or_make_child(cur, s, accum.lexically_normal());
            }
        }

        for (auto& it : m_items)
        {
            auto parent_rel = it.rel_path.parent_path().lexically_normal();

            folder_node_t* cur = m_root;
            if (!parent_rel.empty())
            {
                std::filesystem::path accum;
                for (auto& part : parent_rel)
                {
                    auto s = part.string();
                    if (s.empty()) continue;
                    accum /= s;
                    cur = get_or_make_child(cur, s, accum.lexically_normal());
                }
            }
            cur->items.push_back(it.id);
        }

        if (m_current_folder_rel.empty())
            m_current_folder_rel = std::filesystem::path("");
    }

    bool CAssetBrowserWindow::EnsureHandle(item_t& it)
    {
        if (!m_cb.resolve_handle)
            return false;

        if (!it.handle_valid || !ihandle_is_valid(it.handle))
        {
            it.handle = m_cb.resolve_handle(m_am, it.type, it.abs_path);
            it.handle_valid = ihandle_is_valid(it.handle) ? 1 : 0;
            return it.handle_valid != 0;
        }

        if (m_am)
        {
            const asset_any_t* a = asset_manager_get_any(m_am, it.handle);
            if (!a || a->type != it.type)
            {
                it.handle_valid = 0;
                it.handle = ihandle_invalid();
                it.has_preview = 0;
                it.preview_tex = 0;

                it.handle = m_cb.resolve_handle(m_am, it.type, it.abs_path);
                it.handle_valid = ihandle_is_valid(it.handle) ? 1 : 0;
            }
        }

        return it.handle_valid != 0;
    }

    bool CAssetBrowserWindow::EnsurePreview(item_t& it)
    {
        if (!m_cb.resolve_preview_tex)
            return false;

        if (!EnsureHandle(it))
            return false;

        if (m_am && ihandle_is_valid(it.handle))
            asset_manager_touch(m_am, it.handle);

        if (it.preview_tex != 0)
        {
            uint32_t verify = m_cb.resolve_preview_tex(m_am, it.handle, it.type, it.abs_path);
            if (verify != 0)
            {
                it.preview_tex = verify;
                it.has_preview = 1;
                return true;
            }

            it.preview_tex = 0;
            it.has_preview = 0;
        }

        uint32_t tex = m_cb.resolve_preview_tex(m_am, it.handle, it.type, it.abs_path);
        if (tex != 0)
        {
            it.preview_tex = tex;
            it.has_preview = 1;
            return true;
        }

        return false;
    }

    void CAssetBrowserWindow::BeginRename(uint64_t id)
    {
        m_rename_id = id;
        m_rename_buf[0] = 0;

        auto it = m_item_index.find(id);
        if (it == m_item_index.end())
            return;

        auto& a = m_items[it->second];

        size_t n = a.name.size();
        if (n >= sizeof(m_rename_buf))
            n = sizeof(m_rename_buf) - 1;

        memcpy(m_rename_buf, a.name.data(), n);
        m_rename_buf[n] = 0;
    }

    void CAssetBrowserWindow::CommitRename()
    {
        if (!m_rename_id)
            return;

        auto it = m_item_index.find(m_rename_id);
        if (it == m_item_index.end())
        {
            CancelRename();
            return;
        }

        auto& a = m_items[it->second];

        std::string new_stem = m_rename_buf;
        if (new_stem.empty())
        {
            CancelRename();
            return;
        }

        auto old_abs = a.abs_path;
        auto new_abs = old_abs.parent_path() / (new_stem + old_abs.extension().string());

        bool ok = false;
        if (m_cb.rename_file)
            ok = m_cb.rename_file(m_am, old_abs, new_abs);

        if (ok)
        {
            a.abs_path = new_abs.lexically_normal();
            a.name = new_stem;
            a.handle_valid = 0;
            a.has_preview = 0;
            a.preview_tex = 0;
        }

        CancelRename();
    }

    void CAssetBrowserWindow::CancelRename()
    {
        m_rename_id = 0;
        m_rename_buf[0] = 0;
    }

    void CAssetBrowserWindow::RequestDelete(uint64_t id)
    {
        m_delete_id = id;
        m_delete_modal_open = 1;
    }

    void CAssetBrowserWindow::PerformDeleteIfConfirmed()
    {
        if (!m_delete_id)
            return;

        auto it = m_item_index.find(m_delete_id);
        if (it == m_item_index.end())
        {
            m_delete_id = 0;
            return;
        }

        auto& a = m_items[it->second];
        if (m_cb.delete_file)
            m_cb.delete_file(m_am, a.abs_path);

        m_delete_id = 0;
    }

    void CAssetBrowserWindow::Draw()
    {
        ApplyPendingSnapshot();

        if (m_scan_root_abs.empty())
        {
            ImGui::TextUnformatted("No project loaded.");
            return;
        }

        float left_w = 260.0f;

        ImGui::BeginChild("##asset_tree", ImVec2(left_w, 0), true);
        DrawFolderTreePanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##asset_grid", ImVec2(0, 0), true);
        DrawGridPanel();
        ImGui::EndChild();

        if (m_delete_modal_open)
        {
            ImGui::OpenPopup("Delete Asset?");
            m_delete_modal_open = 0;
        }

        if (ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            std::string nm;
            auto it = m_item_index.find(m_delete_id);
            if (it != m_item_index.end())
                nm = m_items[it->second].rel_path.generic_string();

            ImGui::TextUnformatted("Delete this file?");
            ImGui::Separator();
            ImGui::TextUnformatted(nm.c_str());
            ImGui::Separator();

            bool del = ImGui::Button("Delete");
            ImGui::SameLine();
            bool cancel = ImGui::Button("Cancel");

            if (del)
            {
                PerformDeleteIfConfirmed();
                ImGui::CloseCurrentPopup();
            }
            if (cancel)
            {
                m_delete_id = 0;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void CAssetBrowserWindow::DrawFolderTreePanel()
    {
        if (!m_root)
        {
            ImGui::TextUnformatted("Scanning...");
            return;
        }

        DrawFolderNode(m_root, true);
    }

    void CAssetBrowserWindow::DrawFolderNode(folder_node_t* n, bool root)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (root) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (n->rel_path == m_current_folder_rel) flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx((void*)n, flags, "%s", n->name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            m_current_folder_rel = n->rel_path;

        if (open)
        {
            std::vector<folder_node_t*> kids;
            kids.reserve(n->children.size());
            for (auto& kv : n->children) kids.push_back(kv.second);

            std::sort(kids.begin(), kids.end(), [](folder_node_t* a, folder_node_t* b)
            {
                return a->name < b->name;
            });

            for (auto* c : kids)
                DrawFolderNode(c, false);

            ImGui::TreePop();
        }
    }

    void CAssetBrowserWindow::DrawGridPanel()
    {
        const char* hdr = m_current_folder_rel.empty() ? "Assets" : m_current_folder_rel.generic_string().c_str();
        ImGui::TextUnformatted(hdr);
        ImGui::Separator();

        float pad = 10.0f;
        float cell = m_tile_size + pad;

        float w = ImGui::GetContentRegionAvail().x;
        int cols = (int)(w / cell);
        if (cols < 1) cols = 1;

        if (ImGui::BeginTable("##asset_table", cols, ImGuiTableFlags_SizingFixedFit))
        {
            std::vector<uint64_t> ids;
            ids.reserve(m_items.size());

            for (auto& it : m_items)
            {
                if (it.rel_path.parent_path().lexically_normal() == m_current_folder_rel)
                    ids.push_back(it.id);
            }

            std::sort(ids.begin(), ids.end(), [&](uint64_t a, uint64_t b)
            {
                auto ia = m_item_index.find(a);
                auto ib = m_item_index.find(b);
                if (ia == m_item_index.end() || ib == m_item_index.end()) return a < b;
                return m_items[ia->second].name < m_items[ib->second].name;
            });

            for (auto id : ids)
            {
                auto itI = m_item_index.find(id);
                if (itI == m_item_index.end())
                    continue;

                auto& a = m_items[itI->second];

                ImGui::TableNextColumn();
                ImGui::PushID((int)(id & 0x7FFFFFFF));

                ImVec2 tile(m_tile_size, m_tile_size);
                ImVec2 cursor0 = ImGui::GetCursorScreenPos();

                bool selected = (m_selected_id == id);

                ImGui::InvisibleButton("##tile_btn", ImVec2(tile.x, tile.y));
                bool hovered = ImGui::IsItemHovered();
                bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

                if (clicked)
                {
                    m_selected_id = id;
                    if (m_rename_id && m_rename_id != id)
                        CancelRename();
                }

                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    BeginRename(id);

                if (ImGui::BeginPopupContextItem("##ctx"))
                {
                    if (ImGui::MenuItem("Rename"))
                        BeginRename(id);

                    if (ImGui::MenuItem("Delete"))
                        RequestDelete(id);

                    ImGui::EndPopup();
                }

                auto* dl = ImGui::GetWindowDrawList();
                ImU32 col_bg = selected ? ImGui::GetColorU32(ImGuiCol_Header) : ImGui::GetColorU32(ImGuiCol_FrameBg);
                dl->AddRectFilled(cursor0, ImVec2(cursor0.x + tile.x, cursor0.y + tile.y), col_bg, 6.0f);

                uint32_t tex = 0;
                if (a.type != ASSET_NONE)
                    EnsurePreview(a);
                tex = a.preview_tex;

                if (tex != 0)
                {
                    ImVec2 img_p0(cursor0.x + 6.0f, cursor0.y + 6.0f);
                    ImVec2 img_p1(cursor0.x + tile.x - 6.0f, cursor0.y + tile.y - 22.0f);
                    dl->AddImage((ImTextureID)(uintptr_t)tex, img_p0, img_p1);
                }
                else
                {
                    ImVec2 p0(cursor0.x + 10.0f, cursor0.y + 10.0f);
                    ImVec2 p1(cursor0.x + tile.x - 10.0f, cursor0.y + tile.y - 24.0f);
                    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_Button), 6.0f);
                }

                if (m_rename_id == id)
                {
                    ImGui::SetCursorScreenPos(ImVec2(cursor0.x + 6.0f, cursor0.y + tile.y - 20.0f));
                    ImGui::PushItemWidth(tile.x - 12.0f);

                    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                    bool enter = ImGui::InputText("##rename", m_rename_buf, sizeof(m_rename_buf), flags);
                    bool deact = ImGui::IsItemDeactivatedAfterEdit();

                    ImGui::PopItemWidth();

                    if (enter || deact)
                        CommitRename();
                }
                else
                {
                    std::string label = a.name;
                    ImVec2 txt_sz = ImGui::CalcTextSize(label.c_str());
                    float x = cursor0.x + tile.x - 6.0f - txt_sz.x;
                    float y = cursor0.y + tile.y - 18.0f;
                    dl->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
                }

                if (hovered && a.type != ASSET_NONE)
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(a.rel_path.generic_string().c_str());
                    ImGui::TextUnformatted(TypeLabel(a.type));
                    ImGui::EndTooltip();
                }

                if (hovered && a.type != ASSET_NONE && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    asset_browser_drag_payload_t p{};
                    if (EnsureHandle(a))
                        p.handle = a.handle;
                    p.type = a.type;

                    ImGui::SetDragDropPayload("ASSET_IHANDLE", &p, sizeof(p));
                    ImGui::TextUnformatted(a.name.c_str());
                    ImGui::TextUnformatted(TypeLabel(a.type));
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
}
