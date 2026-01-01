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

        ImGui::SeparatorText("GPU Timings (ms)");

        const render_gpu_timings_t *gt = R_get_gpu_timings(ctx->renderer);
        if (!gt || !gt->valid)
        {
            ImGui::Text("warming up / unsupported");
        }
        else
        {
            ImGui::Text("shadow: %.3f", gt->ms[R_GPU_SHADOW]);
            ImGui::Text("depth: %.3f", gt->ms[R_GPU_DEPTH_PREPASS]);
            ImGui::Text("resolve_depth: %.3f", gt->ms[R_GPU_RESOLVE_DEPTH]);
            ImGui::Text("light_cull: %.3f", gt->ms[R_GPU_FP_CULL]);
            ImGui::Text("sky: %.3f", gt->ms[R_GPU_SKY]);
            ImGui::Text("forward: %.3f", gt->ms[R_GPU_FORWARD]);
            ImGui::Text("resolve_color: %.3f", gt->ms[R_GPU_RESOLVE_COLOR]);
            ImGui::Text("auto_exposure: %.3f", gt->ms[R_GPU_AUTO_EXPOSURE]);
            ImGui::Text("bloom: %.3f", gt->ms[R_GPU_BLOOM]);
            ImGui::Text("composite: %.3f", gt->ms[R_GPU_COMPOSITE]);
        }

        ImGui::SeparatorText("CPU Timings (ms)");

        const render_cpu_timings_t *ct = R_get_cpu_timings(ctx->renderer);
        if (!ct || !ct->valid)
        {
            ImGui::Text("unavailable");
            return;
        }

        ImGui::Text("build_instancing: %.3f", ct->ms[R_CPU_BUILD_INSTANCING]);
        ImGui::Text("build_shadow_inst: %.3f", ct->ms[R_CPU_BUILD_SHADOW_INSTANCING]);
        ImGui::Text("shadow: %.3f", ct->ms[R_CPU_SHADOW]);
        ImGui::Text("depth: %.3f", ct->ms[R_CPU_DEPTH_PREPASS]);
        ImGui::Text("resolve_depth: %.3f", ct->ms[R_CPU_RESOLVE_DEPTH]);
        ImGui::Text("light_cull: %.3f", ct->ms[R_CPU_FP_CULL]);
        ImGui::Text("sky: %.3f", ct->ms[R_CPU_SKY]);
        ImGui::Text("forward: %.3f", ct->ms[R_CPU_FORWARD]);
        ImGui::Text("resolve_color: %.3f", ct->ms[R_CPU_RESOLVE_COLOR]);
        ImGui::Text("auto_exposure: %.3f", ct->ms[R_CPU_AUTO_EXPOSURE]);
        ImGui::Text("bloom: %.3f", ct->ms[R_CPU_BLOOM]);
        ImGui::Text("composite: %.3f", ct->ms[R_CPU_COMPOSITE]);
    }
}
