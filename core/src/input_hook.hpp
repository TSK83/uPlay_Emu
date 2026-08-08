#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Input Hook (WndProc)
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_INPUT_HOOK_HPP
#define ACU_INPUT_HOOK_HPP

#include <Windows.h>

namespace acu {

class InputHook {
public:
    static InputHook& Instance();

    /// Install WndProc hook on the game window.
    bool Init(HWND hWnd);

    /// Remove the WndProc hook.
    void Shutdown();

    /// Check if overlay is currently visible (toggled by F2/Insert).
    bool IsOverlayVisible() const { return m_overlayVisible; }

    /// Toggle overlay visibility.
    void ToggleOverlay() { m_overlayVisible = !m_overlayVisible; }

private:
    InputHook() = default;
    InputHook(const InputHook&) = delete;
    InputHook& operator=(const InputHook&) = delete;

    /// Our custom WndProc that intercepts input.
    static LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND    m_hWnd              = nullptr;
    WNDPROC m_originalWndProc   = nullptr;
    bool    m_overlayVisible    = false;
    bool    m_initialized       = false;
};

} // namespace acu

#endif // ACU_INPUT_HOOK_HPP
