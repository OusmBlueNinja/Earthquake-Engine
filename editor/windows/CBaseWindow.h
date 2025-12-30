#pragma once

extern "C" {
#include "core/core.h"
}

namespace editor {

class CBaseWindow {
public:
    explicit CBaseWindow(const char* name)
        : m_Name(name ? name : "Window")
    {}

    virtual ~CBaseWindow() = default;

    CBaseWindow(const CBaseWindow&) = delete;
    CBaseWindow& operator=(const CBaseWindow&) = delete;
    CBaseWindow(CBaseWindow&&) = delete;
    CBaseWindow& operator=(CBaseWindow&&) = delete;

    const char* GetName() const { return m_Name; }

    bool IsOpen() const { return m_Open; }
    void SetOpen(bool v) { m_Open = v; }

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool v) { m_Visible = v; }

    bool Begin() {
        if (!m_Visible) return false;
        if (!m_Open) return false;
        return BeginImpl();
    }

    void End() {
        EndImpl();
    }

    void Tick(float dt, Application* app) {
        if (!m_Visible) return;
        if (!m_Open) return;
        OnTick(dt, app);
    }

protected:
    virtual bool BeginImpl() = 0;
    virtual void EndImpl() = 0;
    virtual void OnTick(float dt, Application* app) = 0;

protected:
    const char* m_Name = nullptr;
    bool m_Open = true;
    bool m_Visible = true;
};

} // namespace editor
