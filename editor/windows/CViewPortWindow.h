#pragma once

extern "C"
{
#include "renderer/renderer.h"
#include "renderer/camera.h"
#include "types/vec3.h"
#include "managers/cvar.h"
#include "core/systems/ecs/ecs.h"
#include "core/systems/ecs/components/c_tag.h"
#include "core/systems/ecs/components/c_transform.h"
#include "core/systems/ecs/components/c_mesh_renderer.h"
}

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include <string.h>
#include <filesystem>

#include "imgui.h"
#include "CBaseWindow.h"
#include "editor/systems/CEditorSceneManager.h"

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

            ecs_world_t *world = (ctx && ctx->app) ? &ctx->app->scene : nullptr;

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
                bool vsync_on = cvar_get_bool_name("cl_vsync");
                if (ImGui::Checkbox("VSync", &vsync_on))
                    cvar_set_bool_name("cl_vsync", vsync_on);

                ImGui::SameLine();
                bool bloom_on = cvar_get_bool_name("cl_bloom");
                if (ImGui::Checkbox("Bloom", &bloom_on))
                    cvar_set_bool_name("cl_bloom", bloom_on);

                ImGui::SameLine();
                bool shadows_on = cvar_get_bool_name("cl_r_shadows");
                if (ImGui::Checkbox("Shadows", &shadows_on))
                    cvar_set_bool_name("cl_r_shadows", shadows_on);

                ImGui::SameLine();
                bool auto_exp_on = cvar_get_bool_name("cl_auto_exposure");
                if (ImGui::Checkbox("AutoExp", &auto_exp_on))
                    cvar_set_bool_name("cl_auto_exposure", auto_exp_on);

                if (auto_exp_on)
                {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(110.0f);
                    int hz = cvar_get_int_name("cl_auto_exposure_hz");
                    if (hz < 1)
                        hz = 1;
                    if (hz > 240)
                        hz = 240;
                    if (ImGui::SliderInt("AE Hz", &hz, 1, 240))
                        cvar_set_int_name("cl_auto_exposure_hz", hz);
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
                ImGui::SetNextItemWidth(110.0f);

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
            bool active = hovered;

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

            m_LastFinalTex = R_get_final_color_tex(r);

            ImVec2 img_pos = ImGui::GetCursorScreenPos();

            if (m_LastFinalTex)
                ImGui::Image((ImTextureID)(intptr_t)m_LastFinalTex, ImVec2(w, h), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            else
                ImGui::Text("No final texture");

            bool img_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            HandleAssetDragDrop(ctx, world, img_pos, ImVec2(w, h), img_hovered);

            UpdateStats(dt);

            if (m_ShowOverlay)
                DrawOverlaySmall(r, img_pos);
        }

    private:
        static constexpr float VP_ZOOM_MULTIPLIER = 1.12f;
        static constexpr float VP_ZOOM_MIN_DISTANCE = 0.001f;

        struct asset_browser_drag_payload_t
        {
            ihandle_t handle;
            uint32_t type;
        };

        static inline vec3 v3_from_vec4_divw(vec4 v)
        {
            float iw = (fabsf(v.w) > 1e-8f) ? (1.0f / v.w) : 1.0f;
            return (vec3){v.x * iw, v.y * iw, v.z * iw};
        }

        bool RayFromMouseInViewport(ImVec2 img_pos, ImVec2 img_size, vec3 *out_origin, vec3 *out_dir) const
        {
            if (!out_origin || !out_dir)
                return false;

            const ImGuiIO &io = ImGui::GetIO();
            float mx = io.MousePos.x - img_pos.x;
            float my = io.MousePos.y - img_pos.y;
            if (mx < 0.0f || my < 0.0f || mx > img_size.x || my > img_size.y)
                return false;

            float ndc_x = (img_size.x > 1e-6f) ? (2.0f * (mx / img_size.x) - 1.0f) : 0.0f;
            float ndc_y = (img_size.y > 1e-6f) ? (1.0f - 2.0f * (my / img_size.y)) : 0.0f;

            vec4 p_ndc_near = vec4_make(ndc_x, ndc_y, -1.0f, 1.0f);
            vec4 p_ndc_far = vec4_make(ndc_x, ndc_y, 1.0f, 1.0f);

            vec4 p_view_near = mat4_mul_vec4(m_Cam.inv_proj, p_ndc_near);
            vec4 p_view_far = mat4_mul_vec4(m_Cam.inv_proj, p_ndc_far);

            vec4 p_world_near4 = mat4_mul_vec4(m_Cam.inv_view, p_view_near);
            vec4 p_world_far4 = mat4_mul_vec4(m_Cam.inv_view, p_view_far);

            vec3 p_world_near = v3_from_vec4_divw(p_world_near4);
            vec3 p_world_far = v3_from_vec4_divw(p_world_far4);

            vec3 dir = vp_v3_norm(vp_v3_sub(p_world_far, p_world_near));
            if (vp_v3_len(dir) <= 1e-8f)
                return false;

            *out_origin = p_world_near;
            *out_dir = dir;
            return true;
        }

        static bool RayPlaneY0(vec3 origin, vec3 dir, vec3 *out_hit)
        {
            if (!out_hit)
                return false;
            float denom = dir.y;
            if (fabsf(denom) < 1e-6f)
                return false;
            float t = -origin.y / denom;
            if (t < 0.0f)
                t = 0.0f;
            *out_hit = vp_v3_add(origin, vp_v3_mul(dir, t));
            return true;
        }

        static void SetEntityName(ecs_world_t *w, ecs_entity_t e, const char *name)
        {
            if (!w || !ecs_entity_is_alive(w, e))
                return;
            c_tag_t *tag = ecs_get(w, e, c_tag_t);
            if (!tag)
                return;
            memset(tag->name, 0, sizeof(tag->name));
            if (name && name[0])
                memcpy(tag->name, name, strlen(name) < (sizeof(tag->name) - 1) ? strlen(name) : (sizeof(tag->name) - 1));
        }

        static void DeriveNameFromPath(const char *path, char *out, uint32_t out_sz)
        {
            if (!out || out_sz == 0)
                return;
            out[0] = 0;
            if (!path || !path[0])
                return;

            const char *base = path;
            for (const char *p = path; *p; ++p)
                if (*p == '/' || *p == '\\')
                    base = p + 1;

            const char *dot = NULL;
            for (const char *p = base; *p; ++p)
                if (*p == '.')
                    dot = p;

            size_t n = dot ? (size_t)(dot - base) : strlen(base);
            if (n >= (size_t)out_sz)
                n = (size_t)out_sz - 1;
            memcpy(out, base, n);
            out[n] = 0;
        }

        void CancelDragPlacement(ecs_world_t *w)
        {
            if (w && m_DragPlaceEntity && ecs_entity_is_alive(w, m_DragPlaceEntity))
                ecs_entity_destroy(w, m_DragPlaceEntity);
            m_DragPlaceActive = false;
            m_DragPlaceEntity = 0;
            m_DragPlaceHandle = ihandle_invalid();
            m_DragPlaceType = ASSET_NONE;
        }

        void HandleAssetDragDrop(CEditorContext *ctx, ecs_world_t *w, ImVec2 img_pos, ImVec2 img_size, bool img_hovered)
        {
            if (!ctx || !ctx->assets || !w)
                return;

            const ImGuiPayload *p = ImGui::GetDragDropPayload();
            const bool dd_active = (p != nullptr);

            // Cancel preview if the drag ended or became incompatible.
            if (m_DragPlaceActive && (!dd_active || !p || !p->IsDataType("ASSET_IHANDLE")))
            {
                CancelDragPlacement(w);
                return;
            }

            if (dd_active && p && p->IsDataType("ASSET_IHANDLE") && img_hovered && p->DataSize >= (int)sizeof(asset_browser_drag_payload_t))
            {
                asset_browser_drag_payload_t pl{};
                memcpy(&pl, p->Data, sizeof(pl));

                if (pl.type == (uint32_t)ASSET_MODEL && ihandle_is_valid(pl.handle))
                {
                    // Start or refresh preview placement.
                    if (!m_DragPlaceActive || !ihandle_eq(m_DragPlaceHandle, pl.handle))
                    {
                        if (m_DragPlaceActive)
                            CancelDragPlacement(w);

                        ecs_entity_t e = ecs_entity_create(w);
                        ecs_add(w, e, c_transform_t);
                        c_mesh_renderer_t *mr = ecs_add(w, e, c_mesh_renderer_t);
                        if (mr)
                        {
                            mr->model = pl.handle;
                            mr->base.entity = e;
                        }

                        char name[128];
                        char path_buf[ASSET_DEBUG_PATH_MAX];
                        path_buf[0] = 0;
                        if (asset_manager_get_path(ctx->assets, pl.handle, path_buf, (uint32_t)sizeof(path_buf)))
                            DeriveNameFromPath(path_buf, name, (uint32_t)sizeof(name));
                        else
                            snprintf(name, sizeof(name), "Model");
                        name[sizeof(name) - 1] = 0;
                        SetEntityName(w, e, name);

                        m_DragPlaceActive = true;
                        m_DragPlaceEntity = e;
                        m_DragPlaceHandle = pl.handle;
                        m_DragPlaceType = (asset_type_t)pl.type;
                    }

                    if (m_DragPlaceEntity && ecs_entity_is_alive(w, m_DragPlaceEntity))
                    {
                        vec3 ro, rd, hit;
                        if (RayFromMouseInViewport(img_pos, img_size, &ro, &rd) && RayPlaneY0(ro, rd, &hit))
                        {
                            c_transform_t *tr = ecs_get(w, m_DragPlaceEntity, c_transform_t);
                            if (tr)
                            {
                                tr->position = hit;
                                tr->base.entity = m_DragPlaceEntity;
                            }
                        }
                    }
                }
            }

            // Drop accept (commit).
            if (img_hovered && ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("ASSET_IHANDLE"))
                {
                    if (pl->DataSize >= (int)sizeof(asset_browser_drag_payload_t))
                    {
                        asset_browser_drag_payload_t dd{};
                        memcpy(&dd, pl->Data, sizeof(dd));

                        if (dd.type == (uint32_t)ASSET_MODEL && ihandle_is_valid(dd.handle))
                        {
                            if (!m_DragPlaceActive || !ecs_entity_is_alive(w, m_DragPlaceEntity) || !ihandle_eq(m_DragPlaceHandle, dd.handle))
                            {
                                if (m_DragPlaceActive)
                                    CancelDragPlacement(w);

                                ecs_entity_t e = ecs_entity_create(w);
                                ecs_add(w, e, c_transform_t);
                                c_mesh_renderer_t *mr = ecs_add(w, e, c_mesh_renderer_t);
                                if (mr)
                                {
                                    mr->model = dd.handle;
                                    mr->base.entity = e;
                                }

                                char name[128];
                                char path_buf[ASSET_DEBUG_PATH_MAX];
                                path_buf[0] = 0;
                                if (asset_manager_get_path(ctx->assets, dd.handle, path_buf, (uint32_t)sizeof(path_buf)))
                                    DeriveNameFromPath(path_buf, name, (uint32_t)sizeof(name));
                                else
                                    snprintf(name, sizeof(name), "Model");
                                name[sizeof(name) - 1] = 0;
                                SetEntityName(w, e, name);

                                vec3 ro, rd, hit;
                                if (RayFromMouseInViewport(img_pos, img_size, &ro, &rd) && RayPlaneY0(ro, rd, &hit))
                                {
                                    c_transform_t *tr = ecs_get(w, e, c_transform_t);
                                    if (tr)
                                    {
                                        tr->position = hit;
                                        tr->base.entity = e;
                                    }
                                }

                                m_DragPlaceActive = true;
                                m_DragPlaceEntity = e;
                                m_DragPlaceHandle = dd.handle;
                                m_DragPlaceType = (asset_type_t)dd.type;
                            }

                            if (ctx->scene)
                                ctx->scene->MarkDirty();

                            // Finalize placement (keep entity).
                            m_DragPlaceActive = false;
                            m_DragPlaceEntity = 0;
                            m_DragPlaceHandle = ihandle_invalid();
                            m_DragPlaceType = ASSET_NONE;
                        }
                        else if (dd.type == (uint32_t)ASSET_SCENE && ihandle_is_valid(dd.handle) && ctx->scene && ctx->app)
                        {
                            char scene_path[ASSET_DEBUG_PATH_MAX];
                            scene_path[0] = 0;
                            if (asset_manager_get_path(ctx->assets, dd.handle, scene_path, (uint32_t)sizeof(scene_path)))
                            {
                                ctx->scene->LoadNow(&ctx->app->scene, ctx->assets, std::filesystem::path(scene_path));
                                ctx->selected_entity = 0;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

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

        void DrawOverlaySmall(const renderer_t *r, ImVec2 img_pos)
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (!dl)
                return;

            vec3 dir = OrbitDir(m_Yaw, m_Pitch);
            vec3 pos = vp_v3_add(m_Target, vp_v3_mul(dir, m_Distance));

            const render_gpu_timings_t *gt = r ? R_get_gpu_timings(r) : nullptr;

            char text[1536];
            char asset_line[256];
            asset_line[0] = 0;

            char tex_lines[768];
            tex_lines[0] = 0;

            if (r && r->assets)
            {
                asset_manager_stats_t st;
                if (asset_manager_get_stats(r->assets, &st))
                {
                    double vram_mb = (double)st.vram_resident_bytes / (1024.0 * 1024.0);
                    double bud_mb = (double)st.vram_budget_bytes / (1024.0 * 1024.0);
                    double up_mb = (double)st.tex_stream_uploaded_bytes_last_frame / (1024.0 * 1024.0);
                    double ev_mb = (double)st.tex_stream_evicted_bytes_last_frame / (1024.0 * 1024.0);

                    if (st.vram_budget_bytes)
                    {
                        snprintf(asset_line, sizeof(asset_line),
                                 "Tex: %.1f/%.0f MB  up %.2f MB (%u)  ev %.2f MB (%u)  pend %u  jobs %u/%u",
                                 vram_mb, bud_mb, up_mb, (unsigned)st.tex_stream_uploads_last_frame, ev_mb, (unsigned)st.tex_stream_evictions_last_frame,
                                 (unsigned)st.tex_stream_pending_uploads,
                                 (unsigned)st.jobs_pending, (unsigned)st.done_pending);
                    }
                    else
                    {
                        snprintf(asset_line, sizeof(asset_line),
                                 "Tex: %.1f MB  up %.2f MB (%u)  ev %.2f MB (%u)  pend %u  jobs %u/%u",
                                 vram_mb, up_mb, (unsigned)st.tex_stream_uploads_last_frame, ev_mb, (unsigned)st.tex_stream_evictions_last_frame,
                                 (unsigned)st.tex_stream_pending_uploads,
                                 (unsigned)st.jobs_pending, (unsigned)st.done_pending);
                    }
                }
            }

            if (r && r->assets)
            {
                const uint32_t slot_count = asset_manager_debug_get_slot_count(r->assets);
                if (slot_count > 0)
                {
                    static std::vector<asset_debug_slot_t> slots;
                    slots.resize(slot_count);

                    asset_manager_debug_snapshot_t snap = {};
                    asset_manager_debug_get_slots(r->assets, slots.data(), slot_count, &snap);

                    struct top_t
                    {
                        uint32_t slot_index = 0;
                        uint32_t delta = 0;
                        uint16_t priority = 0;
                        const char *path = nullptr;
                        uint32_t res = 0;
                        uint32_t tgt = 0;
                    };

                    top_t top[4] = {};
                    uint32_t top_n = 0;

                    for (uint32_t i = 0; i < slot_count; ++i)
                    {
                        const asset_debug_slot_t &s = slots[i];
                        if (s.type != ASSET_IMAGE || s.state != ASSET_STATE_READY)
                            continue;
                        if (s.img_mip_count == 0)
                            continue;
                        if (s.img_resident_top_mip <= s.img_target_top_mip)
                            continue;

                        const uint32_t delta = s.img_resident_top_mip - s.img_target_top_mip;
                        const uint16_t prio = s.img_priority;

                        // Insert into a tiny fixed top-k list.
                        uint32_t ins = top_n;
                        if (ins > 4)
                            ins = 4;
                        for (uint32_t k = 0; k < top_n && k < 4; ++k)
                        {
                            if (prio > top[k].priority || (prio == top[k].priority && delta > top[k].delta))
                            {
                                ins = k;
                                break;
                            }
                        }

                        if (ins >= 4)
                            continue;

                        if (top_n < 4)
                            top_n++;

                        for (uint32_t k = top_n - 1u; k > ins; --k)
                            top[k] = top[k - 1u];

                        top[ins] = top_t{.slot_index = s.slot_index, .delta = delta, .priority = prio, .path = s.path, .res = s.img_resident_top_mip, .tgt = s.img_target_top_mip};
                    }

                    if (top_n)
                    {
                        size_t off = 0;
                        off += snprintf(tex_lines + off, sizeof(tex_lines) - off, "Streaming:\n");
                        for (uint32_t k = 0; k < top_n && off + 64 < sizeof(tex_lines); ++k)
                        {
                            const char *p = top[k].path ? top[k].path : "-";
                            // Show a short suffix of the path.
                            const char *tail = p;
                            for (const char *q = p; *q; ++q)
                            {
                                if (*q == '/' || *q == '\\')
                                    tail = q + 1;
                            }

                            off += snprintf(tex_lines + off, sizeof(tex_lines) - off,
                                            "  #%u %u/%u (+%u) %s\n",
                                            (unsigned)top[k].slot_index,
                                            (unsigned)top[k].res,
                                            (unsigned)top[k].tgt,
                                            (unsigned)top[k].delta,
                                            tail);
                        }
                    }
                }
            }

            if (gt && gt->valid)
            {
                snprintf(text, sizeof(text),
                         "%.1f fps  %.2f ms  (%.2f %.2f %.2f)\n"
                         "GPU: sh %.2f dp %.2f fp %.2f sky %.2f fwd %.2f rs %.2f blm %.2f cmp %.2f%s%s",
                         (double)m_FpsSmooth, (double)(m_DtSmooth * 1000.0f),
                         (double)pos.x, (double)pos.y, (double)pos.z,
                         gt->ms[R_GPU_SHADOW],
                         gt->ms[R_GPU_DEPTH_PREPASS],
                         gt->ms[R_GPU_FP_CULL],
                         gt->ms[R_GPU_SKY],
                         gt->ms[R_GPU_FORWARD],
                         gt->ms[R_GPU_RESOLVE_COLOR],
                         gt->ms[R_GPU_BLOOM],
                         gt->ms[R_GPU_COMPOSITE],
                         asset_line[0] ? "\n" : "",
                         asset_line[0] ? asset_line : "");

                if (tex_lines[0])
                {
                    strncat(text, "\n", sizeof(text) - strlen(text) - 1u);
                    strncat(text, tex_lines, sizeof(text) - strlen(text) - 1u);
                }
            }
            else
            {
                snprintf(text, sizeof(text), "%.1f fps  %.2f ms  (%.2f %.2f %.2f)%s%s",
                         (double)m_FpsSmooth, (double)(m_DtSmooth * 1000.0f),
                         (double)pos.x, (double)pos.y, (double)pos.z,
                         asset_line[0] ? "\n" : "",
                         asset_line[0] ? asset_line : "");

                if (tex_lines[0])
                {
                    strncat(text, "\n", sizeof(text) - strlen(text) - 1u);
                    strncat(text, tex_lines, sizeof(text) - strlen(text) - 1u);
                }
            }

            float pad = 8.0f;
            ImVec2 p = ImVec2(img_pos.x + pad, img_pos.y + pad);
            ImVec2 sz = ImGui::CalcTextSize(text);

            ImVec2 b0 = ImVec2(p.x - 6.0f, p.y - 4.0f);
            ImVec2 b1 = ImVec2(p.x + sz.x + 6.0f, p.y + sz.y + 4.0f);

            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 95), 5.0f);
            dl->AddText(p, IM_COL32(255, 255, 255, 215), text);
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

        bool m_DragPlaceActive = false;
        ecs_entity_t m_DragPlaceEntity = 0;
        ihandle_t m_DragPlaceHandle = ihandle_invalid();
        asset_type_t m_DragPlaceType = ASSET_NONE;
    };
}
