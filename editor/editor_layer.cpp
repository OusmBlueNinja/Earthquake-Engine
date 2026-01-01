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

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "editor/CEditorContext.h"
#include "editor/windows/CBaseWindow.h"
#include "editor/windows/CViewPortWindow.h"
#include "editor/windows/CStatsWindow.h"
#include "editor/windows/CAssetManagerWindow.h"
#include "editor/windows/CSceneViewerWindow.h"
#include "editor/windows/CEntityInspectorWindow.h"

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
    editor::CEditorContext ctx;

} editor_layer_data_t;

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

static void editor_windows_init(editor_layer_data_t *d, Application *app)
{
    d->windows.clear();
    d->viewport = nullptr;

    {
        auto vp = std::make_unique<editor::CViewPortWindow>();
        d->viewport = vp.get();
        d->windows.emplace_back(std::move(vp));
    }

    d->windows.emplace_back(std::make_unique<editor::CStatsWindow>());
    d->windows.emplace_back(std::make_unique<editor::CAssetManagerWindow>());
    d->windows.emplace_back(std::make_unique<editor::CSceneViewerWindow>());
    d->windows.emplace_back(std::make_unique<editor::CEntityInspectorWindow>());

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

    ImGui::PopStyleVar(3);

    ImGuiID dock_id = ImGui::GetID("DockspaceID");
    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void layer_init(layer_t *layer)
{
    editor_layer_data_t *d = (editor_layer_data_t *)calloc(1, sizeof(editor_layer_data_t));
    layer->data = d;
    if (!d)
        return;

    Application *app = get_application();
    if (!app)
        return;

    GLFWwindow *w = wm_get_glfw_window(&app->window_manager);
    if (!w)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    editor_imgui_style();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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

    if (d->first_frame)
        d->first_frame = 0;
}

void layer_draw(layer_t *)
{
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

    free(d);
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
