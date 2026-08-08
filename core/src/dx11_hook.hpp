#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — DirectX 11 Hook (SwapChain Present + ResizeBuffers)
// ═══════════════════════════════════════════════════════════════════════
//  Hooks IDXGISwapChain::Present (VMT index 8) and ResizeBuffers (index 13)
//  by creating a dummy device/swapchain to read the vtable, then detouring
//  via MinHook. Implements full D3D11 state save/restore for thread-safe
//  ImGui rendering within the engine's render thread.
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_DX11_HOOK_HPP
#define ACU_DX11_HOOK_HPP

#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

namespace acu {

class DX11Hook {
public:
    static DX11Hook& Instance();

    /// Initialize DX11 hooks. Must be called from within the game process.
    bool Init();

    /// Shutdown and remove hooks.
    void Shutdown();

    /// Get the captured device and context (valid after first Present call).
    ID3D11Device*        GetDevice()  const { return m_device; }
    ID3D11DeviceContext* GetContext() const { return m_context; }
    IDXGISwapChain*      GetSwapChain() const { return m_swapChain; }

    bool IsInitialized() const { return m_initialized; }

private:
    DX11Hook() = default;
    ~DX11Hook() { Shutdown(); }
    DX11Hook(const DX11Hook&) = delete;
    DX11Hook& operator=(const DX11Hook&) = delete;

    /// Create a dummy window + device + swapchain to read the vtable.
    bool GetSwapChainVTable(void** outVTable, size_t size);

    /// Hooked Present function (static for MinHook compatibility).
    static HRESULT WINAPI hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

    /// Hooked ResizeBuffers function.
    static HRESULT WINAPI hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
                                           UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                           UINT SwapChainFlags);

    /// Initialize ImGui and render target on first Present call.
    void InitRenderTarget(IDXGISwapChain* pSwapChain);

    /// Release render target (before ResizeBuffers or shutdown).
    void ReleaseRenderTarget();

    // ── State ─────────────────────────────────────────────────────────
    bool                     m_initialized      = false;
    bool                     m_imguiReady       = false;

    ID3D11Device*            m_device           = nullptr;
    ID3D11DeviceContext*     m_context          = nullptr;
    IDXGISwapChain*          m_swapChain        = nullptr;
    ID3D11RenderTargetView*  m_renderTargetView = nullptr;

    // Original function pointers (for calling the real functions)
    using PresentFn        = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn  = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    static inline PresentFn       s_originalPresent       = nullptr;
    static inline ResizeBuffersFn s_originalResizeBuffers = nullptr;
};

} // namespace acu

#endif // ACU_DX11_HOOK_HPP
