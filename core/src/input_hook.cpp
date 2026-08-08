// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Input Hook Implementation
// ═══════════════════════════════════════════════════════════════════════

#include "input_hook.hpp"
#include "logger.hpp"

#include <imgui.h>
#include <imgui_impl_win32.h>

// ImGui Win32 message handler (declared in imgui_impl_win32.h)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace acu {

InputHook& InputHook::Instance() {
    static InputHook s_instance;
    return s_instance;
}

bool InputHook::Init(HWND hWnd) {
    if (m_initialized) return true;
    if (!hWnd) return false;

    m_hWnd = hWnd;

    // Replace the window's WndProc with our hook
    m_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hkWndProc))
    );

    if (!m_originalWndProc) {
        LOG_HOOK("InputHook: SetWindowLongPtrW failed (error: 0x%08lX)", GetLastError());
        return false;
    }

    m_initialized = true;
    LOG_HOOK("InputHook: WndProc hook installed on HWND 0x%p", hWnd);
    return true;
}

void InputHook::Shutdown() {
    if (!m_initialized) return;

    if (m_hWnd && m_originalWndProc) {
        SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalWndProc));
    }

    m_initialized     = false;
    m_originalWndProc = nullptr;
    m_hWnd            = nullptr;

    LOG_HOOK("InputHook: WndProc hook removed");
}

LRESULT CALLBACK InputHook::hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& input = Instance();

    // ── Toggle overlay with F2 or Insert key ──────────────────────────
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_F2 || wParam == VK_INSERT) {
            input.ToggleOverlay();
            LOG_UI("Overlay %s", input.IsOverlayVisible() ? "OPENED" : "CLOSED");

            // Show/hide cursor based on overlay state
            if (input.IsOverlayVisible()) {
                // Disable game cursor clipping when overlay is open
                ClipCursor(nullptr);
            }
            return 0; // Consume the key event
        }
    }

    // ── Forward input to ImGui ────────────────────────────────────────
    if (input.IsOverlayVisible()) {
        // Let ImGui process the message first
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            return 1; // ImGui consumed this message
        }

        // Check if ImGui wants to capture mouse/keyboard
        ImGuiIO& io = ImGui::GetIO();

        // Block mouse input to game when ImGui wants it
        if (io.WantCaptureMouse) {
            switch (msg) {
                case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
                case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
                case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
                case WM_MOUSEMOVE:
                    return 0; // Block these messages from reaching the game
            }
        }

        // Block keyboard input to game when ImGui wants it
        if (io.WantCaptureKeyboard) {
            switch (msg) {
                case WM_KEYDOWN:    case WM_KEYUP:
                case WM_SYSKEYDOWN: case WM_SYSKEYUP:
                case WM_CHAR:
                    return 0; // Block these messages from reaching the game
            }
        }
    }

    // ── Pass through to the game's original WndProc ───────────────────
    return CallWindowProcW(input.m_originalWndProc, hWnd, msg, wParam, lParam);
}

} // namespace acu
