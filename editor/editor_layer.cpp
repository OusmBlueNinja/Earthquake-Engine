
extern "C"
{
#include "editor_layer.h"
#include "core/core.h"
#include "cvar.h"
#include "asset_manager/asset_manager.h"
#include "managers/window_manager.h"
#include "vector.h"
}

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

typedef struct editor_layer_data_t
{
    int inited;
    int first_frame;

    float dt;
    float fps;

    bool show_demo;

    bool am_show_modules;
    bool am_show_queues;

    int asset_slot_pick;

    vec2i last_scene_size;
} editor_layer_data_t;

static editor_layer_data_t *layer_data(layer_t *layer)
{
    return (editor_layer_data_t *)layer->data;
}

static uint32_t editor_resolve_tex_from_fbo_or_tex(uint32_t id)
{
    if (id == 0)
        return 0;

    if (glIsFramebuffer((GLuint)id))
    {
        GLint prev = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)id);

        GLint obj_type = 0;
        GLint obj_name = 0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &obj_type);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &obj_name);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prev);

        if (obj_type == GL_TEXTURE)
            return (uint32_t)obj_name;
        return 0;
    }

    if (glIsTexture((GLuint)id))
        return id;

    return 0;
}

static uint32_t editor_asset_used_slots(const asset_manager_t *am)
{
    if (!am)
        return 0;

    uint32_t used = 0;

    asset_slot_t *it = 0;
    VECTOR_FOR_EACH(am->slots, asset_slot_t, it)
    {
        if (it && it->generation != 0)
            used += 1;
    }
    return used;
}

static uint32_t editor_guess_asset_handle_u32(uint32_t slot_index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (slot_index & 0xFFFFu);
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

    io.Fonts->AddFontFromFileTTF("res/fonts/0xProtoNerdFontPropo-Regular.ttf", 16.0f);

    d->first_frame = 1;
    d->inited = 1;

    d->dt = 0.0f;
    d->fps = 0.0f;

    d->show_demo = false;

    d->am_show_modules = true;
    d->am_show_queues = true;

    d->asset_slot_pick = 0;

    d->last_scene_size.x = 0;
    d->last_scene_size.y = 0;
}

void layer_update(layer_t *layer, float dt)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;

    d->dt = dt;
    d->fps = (dt > 0.000001f) ? (1.0f / dt) : 0.0f;
}

static void editor_draw_stats(editor_layer_data_t *d, renderer_t *r)
{
    if (!ImGui::Begin("Stats"))
    {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Performance");

    ImGui::Text("dt: %.3f ms", (double)d->dt * 1000.0);
    ImGui::Text("fps: %.1f", (double)d->fps);

    {
        float dt_ms = d->dt * 1000.0f;
        float t01 = dt_ms / 16.0f;
        if (t01 < 0.0f)
            t01 = 0.0f;
        if (t01 > 1.0f)
            t01 = 1.0f;
        ImGui::ProgressBar(t01, ImVec2(-1.0f, 0.0f), "Frame Time");
    }

    ImGui::SeparatorText("Toggles");

    {
        int vsync = cvar_get_bool_name("cl_vsync") ? 1 : 0;
        if (ImGui::Checkbox("VSync", (bool *)&vsync))
            cvar_set_bool_name("cl_vsync", vsync != 0);
    }

    {
        int wf = cvar_get_bool_name("cl_r_wireframe") ? 1 : 0;
        if (ImGui::Checkbox("Wireframe", (bool *)&wf))
            cvar_set_bool_name("cl_r_wireframe", wf != 0);
    }

    ImGui::Checkbox("Show ImGui Demo", &d->show_demo);

    ImGui::SeparatorText("Render Debug");

    {
        int dbg = cvar_get_int_name("cl_render_debug");

        if (ImGui::RadioButton("0 None", dbg == 0))
        {
            dbg = 0;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("1 LODs", dbg == 1))
        {
            dbg = 1;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("2 Light Clusters", dbg == 2))
        {
            dbg = 2;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("3 Normals", dbg == 3))
        {
            dbg = 3;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("4 Alpha", dbg == 4))
        {
            dbg = 4;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("5 Albedo", dbg == 5))
        {
            dbg = 5;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("6 LOD Dither", dbg == 6))
        {
            dbg = 6;
            cvar_set_int_name("cl_render_debug", dbg);
        }
        if (ImGui::RadioButton("7 Shadow Cascades", dbg == 7))
        {
            dbg = 7;
            cvar_set_int_name("cl_render_debug", dbg);
        }
    }

    ImGui::SeparatorText("Renderer Stats");

    {
        const render_stats_t *s = r ? R_get_stats(r) : 0;
        if (s)
        {
            ImGui::Text("draw_calls: %" PRIu64, (uint64_t)s->draw_calls);
            ImGui::Text("triangles: %" PRIu64, (uint64_t)s->triangles);
            ImGui::Text("inst_draw_calls: %" PRIu64, (uint64_t)s->instanced_draw_calls);
            ImGui::Text("instances: %" PRIu64, (uint64_t)s->instances);
            ImGui::Text("inst_triangles: %" PRIu64, (uint64_t)s->instanced_triangles);
        }
        else
        {
            ImGui::Text("stats: null");
        }
    }

    ImGui::End();
}

static void editor_draw_assets(editor_layer_data_t *d, asset_manager_t *am)
{
    if (!ImGui::Begin("Assets"))
    {
        ImGui::End();
        return;
    }

    if (!am)
    {
        ImGui::Text("asset_manager: null");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Asset Manager Overview");

    ImGui::Text("ptr: %p", (void *)am);
    ImGui::Text("worker_count: %u", (unsigned)am->worker_count);
    ImGui::Text("workers_ptr: %p", (void *)am->workers);
    ImGui::Text("handle_type: %u", (unsigned)am->handle_type);
    ImGui::Text("shutting_down: %u", (unsigned)am->shutting_down);
    ImGui::Text("modules: %u", (unsigned)(uint32_t)am->modules.size);

    {
        uint32_t total = (uint32_t)am->slots.size;
        uint32_t used = editor_asset_used_slots(am);
        ImGui::Text("loaded_slots: %u / %u", (unsigned)used, (unsigned)total);
    }

    ImGui::Separator();

    d->am_show_queues = ImGui::CollapsingHeader("Queues", d->am_show_queues ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (d->am_show_queues)
    {
        ImGui::Text("jobs.count: %u", (unsigned)am->jobs.count);
        ImGui::Text("jobs.cap: %u", (unsigned)am->jobs.cap);
        ImGui::Text("jobs.head: %u", (unsigned)am->jobs.head);
        ImGui::Text("jobs.tail: %u", (unsigned)am->jobs.tail);

        ImGui::Separator();

        ImGui::Text("done.count: %u", (unsigned)am->done.count);
        ImGui::Text("done.cap: %u", (unsigned)am->done.cap);
        ImGui::Text("done.head: %u", (unsigned)am->done.head);
        ImGui::Text("done.tail: %u", (unsigned)am->done.tail);
    }

    ImGui::Separator();

    d->am_show_modules = ImGui::CollapsingHeader("Supported Modules", d->am_show_modules ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (d->am_show_modules)
    {
        uint32_t mcount = (uint32_t)am->modules.size;
        if (mcount == 0)
        {
            ImGui::Text("(none)");
        }
        else
        {
            for (uint32_t i = 0; i < mcount; ++i)
            {
                const asset_module_desc_t *m = (const asset_module_desc_t *)((uint8_t *)am->modules.data + (size_t)i * (size_t)am->modules.element_size);
                if (!m)
                    continue;

                const char *nm = m->name ? m->name : "(null)";
                ImGui::Text("[%u] name=%s type=%d", (unsigned)i, nm, (int)m->type);
            }
        }
    }

    ImGui::SeparatorText("Final Assets (READY images)");

    {
        uint32_t scount = (uint32_t)am->slots.size;
        uint32_t shown = 0;
        uint32_t max_show = 256;

        for (uint32_t i = 0; i < scount; ++i)
        {
            const asset_slot_t *s = (const asset_slot_t *)((uint8_t *)am->slots.data + (size_t)i * (size_t)am->slots.element_size);
            if (!s)
                continue;
            if (s->generation == 0)
                continue;
            if (s->asset.type != ASSET_IMAGE)
                continue;
            if (s->asset.state != ASSET_STATE_READY)
                continue;

            uint32_t tex = s->asset.as.image.gl_handle;

            ImGui::PushID((int)i);
            ImGui::Text("#%u  handle=0x%08X  tex=%u", (unsigned)i, (unsigned)editor_guess_asset_handle_u32(i, s->generation), (unsigned)tex);

            if (tex)
                ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(64.0f, 64.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

            ImGui::Separator();
            ImGui::PopID();

            shown += 1;
            if (shown >= max_show)
            {
                ImGui::Text("showing first %u", (unsigned)max_show);
                break;
            }
        }

        if (shown == 0)
            ImGui::Text("(none)");
    }

    ImGui::End();
}

static void editor_draw_asset_inspector(editor_layer_data_t *d, asset_manager_t *am)
{
    if (!ImGui::Begin("Asset Inspector"))
    {
        ImGui::End();
        return;
    }

    if (!am)
    {
        ImGui::Text("asset_manager: null");
        ImGui::End();
        return;
    }

    uint32_t scount = (uint32_t)am->slots.size;

    ImGui::SeparatorText("Asset Inspector");

    if (scount == 0)
    {
        ImGui::Text("(no slots)");
        ImGui::End();
        return;
    }

    int maxv = (int)scount - 1;
    if (d->asset_slot_pick < 0)
        d->asset_slot_pick = 0;
    if (d->asset_slot_pick > maxv)
        d->asset_slot_pick = maxv;

    ImGui::SliderInt("slot_index", &d->asset_slot_pick, 0, maxv);

    uint32_t idx = (uint32_t)d->asset_slot_pick;
    const asset_slot_t *s = (const asset_slot_t *)((uint8_t *)am->slots.data + (size_t)idx * (size_t)am->slots.element_size);
    if (!s)
    {
        ImGui::Text("slot: null");
        ImGui::End();
        return;
    }

    ImGui::Text("id: %u", (unsigned)idx);
    ImGui::Text("asset_handle: 0x%08X", (unsigned)editor_guess_asset_handle_u32(idx, s->generation));
    ImGui::Text("generation: %u", (unsigned)s->generation);
    ImGui::Text("module_index: %u", (unsigned)s->module_index);
    ImGui::Text("asset.type: %d", (int)s->asset.type);
    ImGui::Text("asset.state: %d", (int)s->asset.state);

    ImGui::Separator();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f)
        avail.x = 1.0f;
    if (avail.y < 1.0f)
        avail.y = 1.0f;

    if (s->asset.type == ASSET_IMAGE && s->asset.state == ASSET_STATE_READY)
    {
        uint32_t tex = s->asset.as.image.gl_handle;
        if (tex)
            ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        else
            ImGui::Text("READY image, but gl_handle=0");
    }
    else
    {
        ImGui::Text("Selected slot is not a READY image");
    }

    ImGui::End();
}

static void editor_draw_scene(editor_layer_data_t *d, renderer_t *r)
{
    if (!ImGui::Begin("Scene Renderer"))
    {
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float w = avail.x;
    float h = avail.y;

    if (w < 1.0f)
        w = 1.0f;
    if (h < 1.0f)
        h = 1.0f;

    vec2i scene_fb;
    scene_fb.x = (int)(w + 0.5f);
    scene_fb.y = (int)(h + 0.5f);
    if (scene_fb.x < 1)
        scene_fb.x = 1;
    if (scene_fb.y < 1)
        scene_fb.y = 1;

    if (scene_fb.x != d->last_scene_size.x || scene_fb.y != d->last_scene_size.y)
    {
        R_resize(r, scene_fb);
        d->last_scene_size = scene_fb;
    }

    uint32_t fbo_or_tex = R_get_final_fbo(r);
    uint32_t tex = editor_resolve_tex_from_fbo_or_tex(fbo_or_tex);

    if (tex)
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, h), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    else
        ImGui::Text("No final texture");

    ImGui::End();
}

void layer_post_update(layer_t *layer, float dt)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d || !d->inited)
        return;

    Application *app = get_application();
    if (!app)
        return;

    vec2i fb = wm_get_framebuffer_size(&app->window_manager);
    if (fb.x <= 0 || fb.y <= 0)
        return;

    renderer_t *r = &app->renderer;
    asset_manager_t *am = r ? r->assets : 0;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

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

    ImGui::PopStyleVar(3);

    ImGuiID dock_id = ImGui::GetID("DockspaceID");
    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();

    editor_draw_stats(d, r);
    editor_draw_assets(d, am);
    editor_draw_asset_inspector(d, am);
    editor_draw_scene(d, r);

    if (d->show_demo)
        ImGui::ShowDemoWindow(&d->show_demo);

    ImGui::Render();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb.x, fb.y);
    glDisable(GL_SCISSOR_TEST);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    (void)dt;

    if (d->first_frame)
        d->first_frame = 0;
}

void layer_draw(layer_t *layer)
{
    (void)layer;
}

void layer_shutdown(layer_t *layer)
{
    editor_layer_data_t *d = layer_data(layer);
    if (!d)
        return;

    if (d->inited)
    {
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
