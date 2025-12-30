#pragma once

extern "C"
{
#include "renderer/renderer.h"
#include "renderer/camera.h"
#include "types/vec3.h"
#include "managers/cvar.h"
}

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <GL/glew.h>

#include "imgui.h"
#include "CBaseWindow.h"

namespace editor
{
    static inline float vp_clampf(float x, float lo, float hi)
    {
        if (x < lo)
            return lo;
        if (x > hi)
            return hi;
        return x;
    }

    static inline float vp_lerpf(float a, float b, float t) { return a + (b - a) * t; }

    static inline float vp_smooth_alpha(float dt, float sharpness)
    {
        if (dt <= 0.0f)
            return 1.0f;
        if (sharpness <= 0.0f)
            return 1.0f;
        return 1.0f - expf(-sharpness * dt);
    }

    static inline float vp_wrap_pi(float a)
    {
        const float pi = 3.1415926535f;
        const float two_pi = 6.2831853070f;
        while (a > pi)
            a -= two_pi;
        while (a < -pi)
            a += two_pi;
        return a;
    }

    static inline vec3 vp_v3_add(vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
    static inline vec3 vp_v3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
    static inline vec3 vp_v3_mul(vec3 a, float s) { return (vec3){a.x * s, a.y * s, a.z * s}; }
    static inline vec3 vp_v3_lerp(vec3 a, vec3 b, float t) { return (vec3){vp_lerpf(a.x, b.x, t), vp_lerpf(a.y, b.y, t), vp_lerpf(a.z, b.z, t)}; }
    static inline float vp_v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    static inline vec3 vp_v3_cross(vec3 a, vec3 b)
    {
        return (vec3){
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    }

    static inline float vp_v3_len(vec3 a) { return sqrtf(vp_v3_dot(a, a)); }

    static inline vec3 vp_v3_norm(vec3 a)
    {
        float l = vp_v3_len(a);
        if (l <= 1e-8f)
            return (vec3){0, 0, 0};
        float inv = 1.0f / l;
        return (vec3){a.x * inv, a.y * inv, a.z * inv};
    }

    class CViewPortWindow final : public CBaseWindow
    {
    public:
        CViewPortWindow() : CBaseWindow("Viewport") { ResetOrbit(); }

        vec2i GetLastSceneSize() const { return m_LastSceneSize; }
        uint32_t GetLastFinalTex() const { return m_LastFinalTex; }
        const camera_t *GetCamera() const { return &m_Cam; }

    protected:
        bool BeginImpl() override
        {
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
            return ImGui::Begin(m_Name, &m_Open, flags);
        }

        void EndImpl() override { ImGui::End(); }

        void OnTick(float dt, CEditorContext *ctx) override
        {
            renderer_t *r = ctx ? ctx->renderer : nullptr;
            if (!r)
            {
                ImGui::Text("renderer: null");
                return;
            }

            if (dt < 0.0f)
                dt = 0.0f;
            if (dt > 0.25f)
                dt = 0.25f;

            {
                int msaa = cvar_get_bool_name("cl_msaa_enabled") ? 1 : 0;
                bool msaa_on = (msaa != 0);
                if (ImGui::Checkbox("MSAA", &msaa_on))
                {
                    msaa = msaa_on ? 1 : 0;
                    cvar_set_bool_name("cl_msaa_enabled", msaa != 0);
                }

                ImGui::SameLine();
                int samples = cvar_get_int_name("cl_msaa_samples");
                if (samples < 1)
                    samples = 1;

                static const int kSamples[] = {1, 2, 4, 8, 16};
                int curIdx = 0;
                for (int i = 0; i < (int)(sizeof(kSamples) / sizeof(kSamples[0])); ++i)
                    if (kSamples[i] == samples)
                        curIdx = i;

                char label[32];
                snprintf(label, sizeof(label), "%dx", kSamples[curIdx]);

                if (ImGui::BeginCombo("Samples", label))
                {
                    for (int i = 0; i < (int)(sizeof(kSamples) / sizeof(kSamples[0])); ++i)
                    {
                        bool sel = (i == curIdx);
                        char opt[32];
                        snprintf(opt, sizeof(opt), "%dx", kSamples[i]);
                        if (ImGui::Selectable(opt, sel))
                        {
                            cvar_set_int_name("cl_msaa_samples", kSamples[i]);
                            curIdx = i;
                        }
                        if (sel)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                ImGui::Checkbox("Overlay", &m_ShowOverlay);

                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                ImGui::SliderFloat("Sens", &m_Sensitivity, 0.10f, 6.0f, "%.2f");

                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                ImGui::SliderFloat("Look", &m_RmbLookMult, 0.10f, 1.25f, "%.2f");

                ImGui::Separator();
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            float w = avail.x;
            float h = avail.y;
            if (w < 1.0f)
                w = 1.0f;
            if (h < 1.0f)
                h = 1.0f;

            UpdatePerspective(w, h);

            const ImGuiIO &io = ImGui::GetIO();
            bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            bool active = hovered && focused;

            if (active)
                UpdateNavGodotFeel(io, dt, h);

            ApplySmoothing(dt);

            RecalcView();
            R_push_camera(r, &m_Cam);

            vec2i scene_fb;
            scene_fb.x = (int)(w + 0.5f);
            scene_fb.y = (int)(h + 0.5f);
            if (scene_fb.x < 1)
                scene_fb.x = 1;
            if (scene_fb.y < 1)
                scene_fb.y = 1;

            if (scene_fb.x != m_LastSceneSize.x || scene_fb.y != m_LastSceneSize.y)
            {
                R_resize(r, scene_fb);
                m_LastSceneSize = scene_fb;
            }

            uint32_t fbo_or_tex = R_get_final_fbo(r);
            uint32_t tex = ResolveTexFromFboOrTex(fbo_or_tex);
            m_LastFinalTex = tex;

            ImVec2 img_pos = ImGui::GetCursorScreenPos();

            if (tex)
                ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, h), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            else
                ImGui::Text("No final texture");

            UpdateStats(dt);

            if (m_ShowOverlay)
                DrawOverlaySmall(img_pos);
        }

    private:
        static constexpr float VP_ZOOM_MULTIPLIER = 1.12f;
        static constexpr float VP_ZOOM_MIN_DISTANCE = 0.001f;

        void ResetOrbit()
        {
            m_Target = (vec3){0, 0, 0};
            m_WorldUp = (vec3){0, 1, 0};

            m_Distance = 6.0f;
            m_Yaw = 0.0f;
            m_Pitch = 0.35f;

            m_TargetGoal = m_Target;
            m_DistanceGoal = m_Distance;
            m_YawGoal = m_Yaw;
            m_PitchGoal = m_Pitch;

            m_FovY = 60.0f * (3.1415926535f / 180.0f);
            m_Near = 0.05f;
            m_Far = 500.0f;
            m_Aspect = 16.0f / 9.0f;

            m_Cam = camera_create();
            camera_set_perspective(&m_Cam, m_FovY, m_Aspect, m_Near, m_Far);
            RecalcView();
        }

        void UpdatePerspective(float w, float h)
        {
            float a = w / h;
            if (a <= 0.00001f)
                a = 1.0f;
            if (fabsf(a - m_Aspect) > 0.00001f)
            {
                m_Aspect = a;
                camera_set_perspective(&m_Cam, m_FovY, m_Aspect, m_Near, m_Far);
            }
        }

        vec3 OrbitDir(float yaw, float pitch) const
        {
            float cy = cosf(yaw);
            float sy = sinf(yaw);
            float cp = cosf(pitch);
            float sp = sinf(pitch);
            return (vec3){cp * sy, sp, cp * cy};
        }

        void RecalcView()
        {
            vec3 dir = OrbitDir(m_Yaw, m_Pitch);
            vec3 pos = vp_v3_add(m_Target, vp_v3_mul(dir, m_Distance));
            camera_look_at(&m_Cam, pos, m_Target, m_WorldUp);
        }

        void UpdateNavGodotFeel(const ImGuiIO &io, float dt, float viewport_h)
        {
            (void)dt;

            float orbit_deg_per_px = 0.22f;
            float look_deg_per_px = 0.20f;

            float mult = m_Sensitivity;
            if (io.KeyAlt)
                mult *= m_FineMult;
            if (io.KeyCtrl)
                mult *= m_FastMult;

            float orbit_rad_per_px = (orbit_deg_per_px * (3.1415926535f / 180.0f)) * mult;
            float look_rad_per_px = (look_deg_per_px * (3.1415926535f / 180.0f)) * (mult * m_RmbLookMult);

            bool mmb = io.MouseDown[2] != 0;
            bool rmb = io.MouseDown[1] != 0;
            bool shift = io.KeyShift;

            float dx = io.MouseDelta.x;
            float dy = io.MouseDelta.y;

            float wheel = io.MouseWheel;
            if (wheel != 0.0f)
            {
                float steps = wheel < 0.0f ? -wheel : wheel;
                float factor = powf(VP_ZOOM_MULTIPLIER, steps);
                float d = m_DistanceGoal;
                if (wheel > 0.0f)
                    d /= factor;
                else
                    d *= factor;
                if (d < VP_ZOOM_MIN_DISTANCE)
                    d = VP_ZOOM_MIN_DISTANCE;
                m_DistanceGoal = d;
            }

            if (mmb && shift)
            {
                float safe_h = viewport_h < 1.0f ? 1.0f : viewport_h;
                float world_per_px = (2.0f * m_DistanceGoal * tanf(m_FovY * 0.5f)) / safe_h;

                vec3 dir = OrbitDir(m_YawGoal, m_PitchGoal);
                vec3 pos = vp_v3_add(m_TargetGoal, vp_v3_mul(dir, m_DistanceGoal));

                vec3 fwd = vp_v3_norm(vp_v3_sub(m_TargetGoal, pos));
                vec3 right = vp_v3_norm(vp_v3_cross(fwd, m_WorldUp));
                vec3 up = vp_v3_norm(vp_v3_cross(right, fwd));

                float s = world_per_px;
                vec3 pan = (vec3){0, 0, 0};
                pan = vp_v3_add(pan, vp_v3_mul(right, -dx * s));
                pan = vp_v3_add(pan, vp_v3_mul(up, dy * s));
                m_TargetGoal = vp_v3_add(m_TargetGoal, pan);
            }
            else if (mmb)
            {
                float yaw_delta = -dx * orbit_rad_per_px;
                float pitch_delta = dy * orbit_rad_per_px;

                if (m_InvertX)
                    yaw_delta = -yaw_delta;
                if (m_InvertY)
                    pitch_delta = -pitch_delta;

                m_YawGoal += yaw_delta;
                m_PitchGoal += pitch_delta;
            }
            else if (rmb)
            {
                float yaw_delta = dx * look_rad_per_px;
                float pitch_delta = -dy * look_rad_per_px;

                if (m_InvertX)
                    yaw_delta = -yaw_delta;
                if (m_InvertY)
                    pitch_delta = -pitch_delta;

                vec3 dir0 = OrbitDir(m_YawGoal, m_PitchGoal);
                vec3 pos0 = vp_v3_add(m_TargetGoal, vp_v3_mul(dir0, m_DistanceGoal));

                m_YawGoal += yaw_delta;
                m_PitchGoal += pitch_delta;

                float lim = 3.1415926535f * 0.5f - 0.001f;
                m_PitchGoal = vp_clampf(m_PitchGoal, -lim, lim);
                m_YawGoal = vp_wrap_pi(m_YawGoal);

                vec3 dir1 = OrbitDir(m_YawGoal, m_PitchGoal);
                m_TargetGoal = vp_v3_sub(pos0, vp_v3_mul(dir1, m_DistanceGoal));
            }

            float lim = 3.1415926535f * 0.5f - 0.001f;
            m_PitchGoal = vp_clampf(m_PitchGoal, -lim, lim);
            m_YawGoal = vp_wrap_pi(m_YawGoal);
        }

        void ApplySmoothing(float dt)
        {
            float aRot = vp_smooth_alpha(dt, m_RotationSharpness);
            float aPos = vp_smooth_alpha(dt, m_PanSharpness);
            float aZoom = vp_smooth_alpha(dt, m_ZoomSharpness);

            float lim = 3.1415926535f * 0.5f - 0.001f;

            float yaw_err = vp_wrap_pi(m_YawGoal - m_Yaw);
            m_Yaw += yaw_err * aRot;
            m_Yaw = vp_wrap_pi(m_Yaw);

            float pitch_goal = vp_clampf(m_PitchGoal, -lim, lim);
            m_Pitch = vp_lerpf(m_Pitch, pitch_goal, aRot);

            m_Distance = vp_lerpf(m_Distance, m_DistanceGoal, aZoom);
            if (m_Distance < VP_ZOOM_MIN_DISTANCE)
                m_Distance = VP_ZOOM_MIN_DISTANCE;

            m_Target = vp_v3_lerp(m_Target, m_TargetGoal, aPos);
        }

        void UpdateStats(float dt)
        {
            if (dt <= 0.0f)
                return;
            float fps = 1.0f / dt;
            float a = vp_smooth_alpha(dt, 8.0f);
            m_DtSmooth = vp_lerpf(m_DtSmooth, dt, a);
            m_FpsSmooth = vp_lerpf(m_FpsSmooth, fps, a);
        }

        void DrawOverlaySmall(ImVec2 img_pos)
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (!dl)
                return;

            vec3 dir = OrbitDir(m_Yaw, m_Pitch);
            vec3 pos = vp_v3_add(m_Target, vp_v3_mul(dir, m_Distance));

            char text[256];
            snprintf(text, sizeof(text), "%.1f fps  %.2f ms  (%.2f %.2f %.2f)",
                     (double)m_FpsSmooth, (double)(m_DtSmooth * 1000.0f),
                     (double)pos.x, (double)pos.y, (double)pos.z);

            float pad = 8.0f;
            ImVec2 p = ImVec2(img_pos.x + pad, img_pos.y + pad);
            ImVec2 sz = ImGui::CalcTextSize(text);

            ImVec2 b0 = ImVec2(p.x - 6.0f, p.y - 4.0f);
            ImVec2 b1 = ImVec2(p.x + sz.x + 6.0f, p.y + sz.y + 4.0f);

            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 95), 5.0f);
            dl->AddText(p, IM_COL32(255, 255, 255, 215), text);
        }

        static uint32_t ResolveTexFromFboOrTex(uint32_t id)
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

    private:
        camera_t m_Cam = camera_create();

        vec3 m_Target{0, 0, 0};
        vec3 m_WorldUp{0, 1, 0};

        float m_Distance = 6.0f;
        float m_Yaw = 0.0f;
        float m_Pitch = 0.35f;

        float m_FovY = 60.0f * (3.1415926535f / 180.0f);
        float m_Aspect = 16.0f / 9.0f;
        float m_Near = 0.05f;
        float m_Far = 500.0f;

        bool m_InvertX = false;
        bool m_InvertY = false;

        vec2i m_LastSceneSize{0, 0};
        uint32_t m_LastFinalTex = 0;

        bool m_ShowOverlay = true;

        float m_YawGoal = 0.0f;
        float m_PitchGoal = 0.35f;
        float m_DistanceGoal = 6.0f;
        vec3 m_TargetGoal{0, 0, 0};

        float m_RotationSharpness = 18.0f;
        float m_PanSharpness = 16.0f;
        float m_ZoomSharpness = 20.0f;

        float m_DtSmooth = 1.0f / 60.0f;
        float m_FpsSmooth = 60.0f;

        float m_Sensitivity = 1.75f;
        float m_FineMult = 0.35f;
        float m_FastMult = 2.25f;

        float m_RmbLookMult = 0.45f;
    };
}
