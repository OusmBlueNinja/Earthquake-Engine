#include "editor/windows/CStatsWindow.h"

extern "C"
{
#include "cvar.h"
}

#include <inttypes.h>
#include <stdint.h>

namespace editor
{
    void CStatsWindow::OnTick(float, CEditorContext *ctx)
    {
        float dt = ctx ? ctx->dt : 0.0f;
        float fps = ctx ? ctx->fps : 0.0f;

        ImGui::SeparatorText("Performance");
        ImGui::Text("dt: %.3f ms", (double)dt * 1000.0);
        ImGui::Text("fps: %.1f", (double)fps);

        float dt_ms = dt * 1000.0f;
        float t01 = dt_ms / 16.0f;
        if (t01 < 0.0f)
            t01 = 0.0f;
        if (t01 > 1.0f)
            t01 = 1.0f;
        ImGui::ProgressBar(t01, ImVec2(-1.0f, 0.0f), "Frame Time");

        ImGui::SeparatorText("Toggles");

        {
            bool vsync = cvar_get_bool_name("cl_vsync") != 0;
            if (ImGui::Checkbox("VSync", &vsync))
                cvar_set_bool_name("cl_vsync", vsync ? 1 : 0);
        }

        {
            bool wf = cvar_get_bool_name("cl_r_wireframe") != 0;
            if (ImGui::Checkbox("Wireframe", &wf))
                cvar_set_bool_name("cl_r_wireframe", wf ? 1 : 0);
        }

        ImGui::SeparatorText("Render Debug");

        {
            int dbg = cvar_get_int_name("cl_render_debug");

            if (ImGui::RadioButton("0 None", dbg == 0))
                dbg = 0;
            if (ImGui::RadioButton("1 LODs", dbg == 1))
                dbg = 1;
            if (ImGui::RadioButton("2 Light Clusters", dbg == 2))
                dbg = 2;
            if (ImGui::RadioButton("3 Normals", dbg == 3))
                dbg = 3;
            if (ImGui::RadioButton("4 Alpha", dbg == 4))
                dbg = 4;
            if (ImGui::RadioButton("5 Albedo", dbg == 5))
                dbg = 5;
            if (ImGui::RadioButton("6 LOD Dither", dbg == 6))
                dbg = 6;
            if (ImGui::RadioButton("7 Shadow Cascades", dbg == 7))
                dbg = 7;

            cvar_set_int_name("cl_render_debug", dbg);
        }

        ImGui::SeparatorText("Renderer Stats");

        if (!ctx || !ctx->renderer)
        {
            ImGui::Text("renderer: null");
            return;
        }

        const render_stats_t *s = R_get_stats(ctx->renderer);
        if (!s)
        {
            ImGui::Text("stats: null");
            return;
        }

        ImGui::Text("draw_calls: %" PRIu64, (uint64_t)s->draw_calls);
        ImGui::Text("triangles: %" PRIu64, (uint64_t)s->triangles);
        ImGui::Text("inst_draw_calls: %" PRIu64, (uint64_t)s->instanced_draw_calls);
        ImGui::Text("instances: %" PRIu64, (uint64_t)s->instances);
        ImGui::Text("inst_triangles: %" PRIu64, (uint64_t)s->instanced_triangles);
    }
}
