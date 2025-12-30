#pragma once

extern "C"
{
#include "renderer/camera.h"
#include "types/vec3.h"
#include "types/mat4.h"
}

#include <math.h>
#include "imgui.h"

namespace editor
{
    static inline float oc_clampf(float x, float lo, float hi)
    {
        if (x < lo)
            return lo;
        if (x > hi)
            return hi;
        return x;
    }

    static inline vec3 oc_v3_add(vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
    static inline vec3 oc_v3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
    static inline vec3 oc_v3_mul(vec3 a, float s) { return (vec3){a.x * s, a.y * s, a.z * s}; }

    static inline float oc_v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    static inline vec3 oc_v3_cross(vec3 a, vec3 b)
    {
        return (vec3){
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    }

    static inline float oc_v3_len(vec3 a)
    {
        return sqrtf(oc_v3_dot(a, a));
    }

    static inline vec3 oc_v3_norm(vec3 a)
    {
        float l = oc_v3_len(a);
        if (l <= 1e-8f)
            return (vec3){0, 0, 0};
        float inv = 1.0f / l;
        return (vec3){a.x * inv, a.y * inv, a.z * inv};
    }

    class COrbitCamera final
    {
    public:
        void Reset()
        {
            m_Target = (vec3){0, 0, 0};
            m_Distance = 6.0f;
            m_Yaw = 0.0f;
            m_Pitch = 0.35f;
            m_WorldUp = (vec3){0, 1, 0};

            m_FovY = 60.0f * (3.1415926535f / 180.0f);
            m_Near = 0.05f;
            m_Far = 500.0f;

            m_Aspect = 16.0f / 9.0f;

            m_Cam = camera_create();
            camera_set_perspective(&m_Cam, m_FovY, m_Aspect, m_Near, m_Far);
            Recalc();
        }

        void SetViewportSize(float w, float h)
        {
            if (w < 1.0f)
                w = 1.0f;
            if (h < 1.0f)
                h = 1.0f;
            float a = w / h;
            if (a <= 0.00001f)
                a = 1.0f;
            if (fabsf(a - m_Aspect) > 0.00001f)
            {
                m_Aspect = a;
                camera_set_perspective(&m_Cam, m_FovY, m_Aspect, m_Near, m_Far);
            }
        }

        void UpdateFromImGui(const ImGuiIO &io, bool active, float dt)
        {
            if (!active)
                return;

            float orbit_sens = 0.0125f;
            float pan_sens = 0.00125f;
            float zoom_sens = 0.90f;

            bool orbit = io.MouseDown[1] != 0;
            bool pan = io.MouseDown[2] != 0;

            if (orbit)
            {
                float dx = io.MouseDelta.x;
                float dy = io.MouseDelta.y;

                m_Yaw += dx * orbit_sens;
                m_Pitch += dy * orbit_sens;

                float lim = 1.5533430343f;
                m_Pitch = oc_clampf(m_Pitch, -lim, lim);
            }

            if (pan)
            {
                float dx = io.MouseDelta.x;
                float dy = io.MouseDelta.y;

                vec3 fwd = GetForward();
                vec3 right = oc_v3_norm(oc_v3_cross(fwd, m_WorldUp));
                vec3 up = oc_v3_norm(oc_v3_cross(right, fwd));

                float scale = m_Distance * pan_sens;
                vec3 t = m_Target;
                t = oc_v3_sub(t, oc_v3_mul(right, dx * scale));
                t = oc_v3_add(t, oc_v3_mul(up, dy * scale));
                m_Target = t;
            }

            if (io.MouseWheel != 0.0f)
            {
                float z = io.MouseWheel;
                if (z > 0.0f)
                    m_Distance *= zoom_sens;
                else if (z < 0.0f)
                    m_Distance /= zoom_sens;

                m_Distance = oc_clampf(m_Distance, 0.25f, 2000.0f);
            }

            (void)dt;
            Recalc();
        }

        const camera_t *GetCamera() const { return &m_Cam; }
        vec3 GetTarget() const { return m_Target; }
        float GetDistance() const { return m_Distance; }

    private:
        vec3 GetForward() const
        {
            vec3 pos = ComputePosition();
            return oc_v3_norm(oc_v3_sub(m_Target, pos));
        }

        vec3 ComputePosition() const
        {
            float cy = cosf(m_Yaw);
            float sy = sinf(m_Yaw);
            float cp = cosf(m_Pitch);
            float sp = sinf(m_Pitch);

            vec3 dir = (vec3){cp * sy, sp, cp * cy};
            vec3 pos = oc_v3_add(m_Target, oc_v3_mul(dir, m_Distance));
            return pos;
        }

        void Recalc()
        {
            vec3 pos = ComputePosition();
            camera_look_at(&m_Cam, pos, m_Target, m_WorldUp);
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
    };
}
