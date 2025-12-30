#pragma once

extern "C"
{
#include "core/core.h"
#include "managers/window_manager.h"
}

#include <stdint.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "CBaseWindow.h"

#include "imgui.h"

namespace editor
{

    class CViewPortWindow final : public CBaseWindow
    {
    public:
        CViewPortWindow()
            : CBaseWindow("Viewport")
        {
        }

        void SetRenderer(renderer_t *r) { m_Renderer = r; }

        vec2i GetLastSceneSize() const { return m_LastSceneSize; }
        uint32_t GetLastFinalTex() const { return m_LastFinalTex; }

    protected:
        bool BeginImpl() override
        {
            return ImGui::Begin(m_Name, &m_Open);
        }

        void EndImpl() override
        {
            ImGui::End();
        }

        void OnTick(float, Application *) override
        {
            if (!m_Renderer)
            {
                ImGui::Text("renderer: null");
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

            if (scene_fb.x != m_LastSceneSize.x || scene_fb.y != m_LastSceneSize.y)
            {
                R_resize(m_Renderer, scene_fb);
                m_LastSceneSize = scene_fb;
            }

            uint32_t fbo_or_tex = R_get_final_fbo(m_Renderer);
            uint32_t tex = ResolveTexFromFboOrTex(fbo_or_tex);
            m_LastFinalTex = tex;

            if (tex)
            {
                ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, h), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            }
            else
            {
                ImGui::Text("No final texture");
            }
        }

    private:
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
        renderer_t *m_Renderer = nullptr;
        vec2i m_LastSceneSize{0, 0};
        uint32_t m_LastFinalTex = 0;
    };

}
