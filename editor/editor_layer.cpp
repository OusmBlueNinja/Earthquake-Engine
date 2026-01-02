extern "C"
{
#include "editor_layer.h"
#include "core/core.h"
#include "managers/window_manager.h"
}

#include <stdlib.h>
#include <stdint.h>

#include <vector>
#include <memory>
#include <filesystem>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "editor/CEditorContext.h"
#include "editor/systems/CEditorProjectManager.h"
#include "editor/windows/CBaseWindow.h"
#include "editor/windows/CViewPortWindow.h"
#include "editor/windows/CStatsWindow.h"
#include "editor/windows/CAssetManagerWindow.h"
#include "editor/windows/CTextureStreamingWindow.h"
#include "editor/windows/CSceneViewerWindow.h"
#include "editor/windows/CEntityInspectorWindow.h"
#include "editor/windows/CAssetBrowserWindow.h"

#include "editor/utils/CAssetManager.h"
#include "editor/utils/themes.h"

typedef struct editor_layer_data_t
{
    int inited;
    int first_frame;

    bool show_demo;

    vec2i fb;
    CEditorThemeManager themes;

    std::vector<std::unique_ptr<editor::CBaseWindow>> windows;
    editor::CViewPortWindow *viewport;
    editor::CAssetBrowserWindow *asset_browser;

    editor::CEditorContext ctx;
    editor::CEditorProjectManager project;

    bool show_project_create;
    bool show_project_open;

    char open_project_path[512];
    char create_project_root[512];
    char create_project_name[128];

} editor_layer_data_t;

static inline const char *editor_light_icon_path(light_type_t t)
{
    switch (t)
    {
    case LIGHT_POINT:
        return "res/editor/icons/point.png";
    case LIGHT_SPOT:
        return "res/editor/icons/spot.png";
    case LIGHT_DIRECTIONAL:
        return "res/editor/icons/sun.png";
    default:
        return "res/editor/icons/point.png";
    }
}

static editor_layer_data_t *layer_data(layer_t *layer)
{
    return (editor_layer_data_t *)layer->data;
}

static void editor_imgui_style()
{
    ImGui::StyleColorsDark();

    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 10.0f;
    st.FrameRounding = 7.0f;
    st.PopupRounding = 7.0f;
    st.ScrollbarRounding = 7.0f;
    st.TabRounding = 7.0f;
    st.GrabRounding = 7.0f;
    st.WindowBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;

    st.ItemSpacing = ImVec2(8.0f, 8.0f);
    st.WindowPadding = ImVec2(10.0f, 10.0f);
    st.FramePadding = ImVec2(10.0f, 6.0f);
}

static void editor_apply_project(editor_layer_data_t *d)
{
    if (!d)
        return;

    if (!d->asset_browser)
        return;

    if (!d->project.HasOpenProject())
    {
        d->asset_browser->SetAssetManager(d->ctx.assets);
        d->asset_browser->SetProjectRoot(std::filesystem::path{});
        d->asset_browser->SetScanRoot(std::filesystem::path{});
        return;
    }

    const editor::CEditorProject *p = d->project.GetProject();
    if (!p)
        return;

    d->asset_browser->SetAssetManager(d->ctx.assets);
    d->asset_browser->SetProjectRoot(p->root_dir);
    d->asset_browser->SetScanRoot(p->assets_dir);
}

static void editor_windows_init(editor_layer_data_t *d, Application *app)
{
    d->windows.clear();
    d->viewport = nullptr;
    d->asset_browser = nullptr;

    {
        auto vp = std::make_unique<editor::CViewPortWindow>();
        d->viewport = vp.get();
        d->windows.emplace_back(std::move(vp));
    }

    d->windows.emplace_back(std::make_unique<editor::CStatsWindow>());
    d->windows.emplace_back(std::make_unique<editor::CAssetManagerWindow>());
    d->windows.emplace_back(std::make_unique<editor::CTextureStreamingWindow>());
    d->windows.emplace_back(std::make_unique<editor::CSceneViewerWindow>());
    d->windows.emplace_back(std::make_unique<editor::CEntityInspectorWindow>());

    {
        auto ab = std::make_unique<editor::CAssetBrowserWindow>();
        d->asset_browser = ab.get();
        d->windows.emplace_back(std::move(ab));
    }

    d->themes.RegisterBuiltins();
    d->themes.ApplyById("graphite_dark");
    ImGui::GetStyle().WindowMinSize.x = 380.0f;
    (void)app;
}

static void editor_windows_tick(editor_layer_data_t *d, float dt, Application *app)
{
    d->ctx.app = app;
    d->ctx.renderer = app ? &app->renderer : nullptr;
    d->ctx.assets = (d->ctx.renderer) ? d->ctx.renderer->assets : nullptr;
    d->ctx.dt = dt;
    d->ctx.fps = (dt > 0.000001f) ? (1.0f / dt) : 0.0f;

    editor_apply_project(d);

    for (auto &wptr : d->windows)
    {
        editor::CBaseWindow *w = wptr.get();
        if (!w)
            continue;

        if (!w->IsVisible())
            continue;

        bool draw = w->Begin();
        if (draw)
            w->Tick(dt, &d->ctx);
        w->End();
    }
}

static void editor_windows_shutdown(editor_layer_data_t *d)
{
    d->windows.clear();
    d->viewport = nullptr;
    d->asset_browser = nullptr;
}

static void editor_draw_project_popups(editor_layer_data_t *d)
{
    if (!d)
        return;

    if (d->show_project_open)
        ImGui::OpenPopup("Open Project");

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        d->show_project_open = false;

        ImGui::TextUnformatted("Project File");
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##open_project_path", d->open_project_path, sizeof(d->open_project_path));

        const auto &recent = d->project.GetRecentProjects();
        if (!recent.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Recent");
            ImGui::BeginChild("##recent_list", ImVec2(520.0f, 160.0f), true);

            for (auto &r : recent)
            {
                auto s = r.project_file.string();
                if (ImGui::Selectable(s.c_str(), false))
                {
                    size_t n = s.size();
                    if (n >= sizeof(d->open_project_path))
                        n = sizeof(d->open_project_path) - 1;
                    memcpy(d->open_project_path, s.data(), n);
                    d->open_project_path[n] = 0;
                }
            }

            ImGui::EndChild();
        }

        ImGui::Separator();

        bool do_open = ImGui::Button("Open");
        ImGui::SameLine();
        bool do_cancel = ImGui::Button("Cancel");

        if (do_open)
        {
            if (d->open_project_path[0])
            {
                d->project.OpenProject(std::filesystem::path(d->open_project_path));
                editor_apply_project(d);
            }
            ImGui::CloseCurrentPopup();
        }

        if (do_cancel)
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (d->show_project_create)
        ImGui::OpenPopup("Create Project");

    if (ImGui::BeginPopupModal("Create Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        d->show_project_create = false;

        ImGui::TextUnformatted("Root Folder");
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##create_project_root", d->create_project_root, sizeof(d->create_project_root));

        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##create_project_name", d->create_project_name, sizeof(d->create_project_name));

        ImGui::Separator();

        bool do_create = ImGui::Button("Create");
        ImGui::SameLine();
        bool do_cancel = ImGui::Button("Cancel");

        if (do_create)
        {
            if (d->create_project_root[0] && d->create_project_name[0])
            {
                d->project.CreateProject(std::filesystem::path(d->create_project_root), std::string(d->create_project_name));
                editor_apply_project(d);
            }
            ImGui::CloseCurrentPopup();
        }

        if (do_cancel)
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void editor_draw_dockspace(editor_layer_data_t *d)
{
    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockspaceHost", nullptr, host_flags);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Project"))
        {
            bool can_save = d && d->project.HasOpenProject();

            if (ImGui::MenuItem("New..."))
            {
                if (d)
                    d->show_project_create = true;
            }

            if (ImGui::MenuItem("Open..."))
            {
                if (d)
                    d->show_project_open = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save", nullptr, false, can_save))
            {
                if (d)
                    d->project.SaveProject();
            }

            if (ImGui::MenuItem("Close", nullptr, false, can_save))
            {
                if (d)
                {
                    d->project.CloseProject();
                    editor_apply_project(d);
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            if (d)
            {
                for (auto &wptr : d->windows)
                {
                    editor::CBaseWindow *w = wptr.get();
                    if (!w)
                        continue;
                    bool open = w->IsVisible();
                    if (ImGui::MenuItem(w->GetName(), nullptr, &open))
                        w->SetVisible(open);
                }
            }

            ImGui::Separator();

            bool show_demo = d ? d->show_demo : false;
            if (ImGui::MenuItem("ImGui Demo", nullptr, &show_demo))
            {
                if (d)
                    d->show_demo = show_demo;
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    editor_draw_project_popups(d);

    ImGui::PopStyleVar(3);

    ImGuiID dock_id = ImGui::GetID("DockspaceID");
    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void layer_init(layer_t *layer)
{
    editor_layer_data_t *d = new editor_layer_data_t();
    layer->data = d;
    if (!d)
        return;

    Application *app = get_application();
    if (!app)
    {
        delete d;
        layer->data = 0;
        return;
    }

    GLFWwindow *w = wm_get_glfw_window(&app->window_manager);
    if (!w)
    {
        delete d;
        layer->data = 0;
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    editor_imgui_style();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // Recommended style tweaks for multi-viewport: platform windows look like regular OS windows.
        ImGuiStyle &st = ImGui::GetStyle();
        st.WindowRounding = 0.0f;
        st.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(w, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    io.Fonts->AddFontFromFileTTF("res/fonts/CascadiaMono-Regular.ttf", 16.0f);

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = 13.0f;

    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

    io.Fonts->AddFontFromFileTTF("res/fonts/fa-solid-900.ttf", 16.0f, &cfg, icon_ranges);

    d->first_frame = 1;
    d->inited = 1;
    d->show_demo = false;

    d->fb.x = 0;
    d->fb.y = 0;

    d->show_project_create = false;
    d->show_project_open = false;

    d->open_project_path[0] = 0;
    d->create_project_root[0] = 0;
    d->create_project_name[0] = 0;

    editor_windows_init(d, app);
}

void layer_update(layer_t *layer, float)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;
}

void layer_post_update(layer_t *layer, float dt)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;

    Application *app = layer->app;
    if (!app)
        return;

    vec2i fb = wm_get_framebuffer_size(&app->window_manager);
    if (fb.x <= 0 || fb.y <= 0)
        return;

    d->fb = fb;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    editor_draw_dockspace(d);
    editor_windows_tick(d, dt, app);

    if (d->show_demo)
        ImGui::ShowDemoWindow(&d->show_demo);

    ImGui::Render();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb.x, fb.y);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow *backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }

    if (d->first_frame)
        d->first_frame = 0;
}

void layer_draw(layer_t *layer)
{
    if (!layer)
        return;

    editor_layer_data_t *d = layer_data(layer);
    if (!d)
        return;

    ecs_world_t *scene = &layer->app->scene;
    if (!scene)
        return;

    ecs_entity_t e = d->ctx.selected_entity;
    if (e == 0 || !ecs_entity_is_alive(scene, e))
        return;

    c_transform_t *tr = ecs_get(scene, e, c_transform_t);
    if (!tr)
        return;

    c_light_t *cl = ecs_get(scene, e, c_light_t);
    if (!cl)
        return;

    const char *icon_path = editor_light_icon_path((light_type_t)cl->type);
    uint32_t gl_tex = CAssetManager::GetIconTexture(icon_path);
    if (!gl_tex)
    {
        return;
    }

    quad3d_t q;
    memset(&q, 0, sizeof(q));

    q.center = tr->position;
    q.size = (vec2){44.0f, 44.0f};
    q.rotation = (vec3){0.0f, 0.0f, 0.0f};
    q.color = (vec4){1.0f, 1.0f, 1.0f, 1.0f};
    q.uv = (vec4){0.0f, 0.0f, 1.0f, 1.0f};

    q.texture.gl = gl_tex;

    q.flags = (quad3d_flags_t)(QUAD3D_FACE_CAMERA | QUAD3D_TRANSLUCENT | QUAD3D_TEX_GL | QUAD3D_SCALE_WITH_VIEW);

    R_push_quad3d(&layer->app->renderer, q);
}

void layer_shutdown(layer_t *layer)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d)
        return;

    if (d->inited)
    {
        editor_windows_shutdown(d);

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    delete d;
    layer->data = 0;
}

bool layer_on_event(layer_t *layer, event_t *e)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited || !e)
        return false;

    ImGuiIO &io = ImGui::GetIO();

    if (e->type == EV_MOUSE_MOVE || e->type == EV_MOUSE_BUTTON_DOWN || e->type == EV_MOUSE_BUTTON_UP || e->type == EV_MOUSE_SCROLL)
        return (io.WantCaptureMouse || io.WantTextInput) ? true : false;

    if (e->type == EV_KEY_DOWN || e->type == EV_KEY_UP || e->type == EV_CHAR)
        return (io.WantCaptureKeyboard || io.WantTextInput) ? true : false;

    return false;
}

layer_t create_editor_layer(void)
{
    layer_t layer = create_layer("Editor");
    layer.init = layer_init;
    layer.shutdown = layer_shutdown;
    layer.update = layer_update;
    layer.post_update = layer_post_update;
    layer.draw = layer_draw;
    layer.on_event = layer_on_event;
    return layer;
}
