#pragma once

#include "IconsFontAwesome6.h"

namespace editor
{
    class CEditorContext;

    class CBaseWindow
    {
    public:
        explicit CBaseWindow(const char *name) : m_Name(name) {}
        virtual ~CBaseWindow() = default;

        const char *GetName() const { return m_Name; }

        bool IsVisible() const { return m_Open; }
        void SetVisible(bool v) { m_Open = v; }

        bool Begin()
        {
            if (!m_Open)
                return false;
            return BeginImpl();
        }

        void End()
        {
            EndImpl();
        }

        void Tick(float dt, CEditorContext *ctx)
        {
            OnTick(dt, ctx);
        }

    protected:
        virtual bool BeginImpl() = 0;
        virtual void EndImpl() = 0;
        virtual void OnTick(float dt, CEditorContext *ctx) = 0;

    protected:
        const char *m_Name = nullptr;
        bool m_Open = true;
    };
}
