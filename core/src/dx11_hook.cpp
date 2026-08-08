// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — DirectX 11 Hook Implementation
// ═══════════════════════════════════════════════════════════════════════
//  CRITICAL THREAD SAFETY: This file implements full D3D11 state block
//  save/restore around ImGui rendering. Without this, the game engine's
//  render state is corrupted, causing graphical glitches or crashes.
// ═══════════════════════════════════════════════════════════════════════

#include "dx11_hook.hpp"
#include "imgui_overlay.hpp"
#include "input_hook.hpp"
#include "logger.hpp"

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace acu {

DX11Hook& DX11Hook::Instance() {
    static DX11Hook s_instance;
    return s_instance;
}

// ═══════════════════════════════════════════════════════════════════════
//  Dummy Device/SwapChain to Read VTable
// ═══════════════════════════════════════════════════════════════════════

bool DX11Hook::GetSwapChainVTable(void** outVTable, size_t size) {
    // Create a temporary, hidden window for the dummy swap chain
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.lpszClassName  = L"ACU_DummyDX11Window";
    RegisterClassExW(&wc);

    HWND hDummyWnd = CreateWindowExW(
        0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hDummyWnd) {
        LOG_HOOK("Failed to create dummy window for DX11 VTable read");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 1;
    sd.BufferDesc.Width  = 2;
    sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hDummyWnd;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    IDXGISwapChain*   dummySwapChain = nullptr;
    ID3D11Device*     dummyDevice    = nullptr;
    ID3D11DeviceContext* dummyContext = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &dummySwapChain, &dummyDevice, &featureLevel, &dummyContext
    );

    if (FAILED(hr)) {
        // Fallback to WARP adapter
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &dummySwapChain, &dummyDevice, &featureLevel, &dummyContext
        );
    }

    if (FAILED(hr)) {
        LOG_HOOK("Failed to create dummy D3D11 device (HRESULT: 0x%08lX)", hr);
        DestroyWindow(hDummyWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // Read the vtable from the dummy swap chain
    void** vtable = *reinterpret_cast<void***>(dummySwapChain);
    memcpy(outVTable, vtable, size * sizeof(void*));

    // Cleanup dummy objects
    dummySwapChain->Release();
    dummyDevice->Release();
    dummyContext->Release();
    DestroyWindow(hDummyWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Hook Installation
// ═══════════════════════════════════════════════════════════════════════

bool DX11Hook::Init() {
    if (m_initialized) return true;

    LOG_HOOK("DX11Hook: Reading SwapChain VTable...");

    // We need at least 19 entries (Present=8, ResizeBuffers=13, ...)
    void* vtable[19] = {};
    if (!GetSwapChainVTable(vtable, 19)) {
        LOG_HOOK("DX11Hook: Failed to read VTable");
        return false;
    }

    LOG_HOOK("DX11Hook: Present at VTable[8]  = 0x%p", vtable[8]);
    LOG_HOOK("DX11Hook: ResizeBuffers at VTable[13] = 0x%p", vtable[13]);

    // Hook Present (VMT index 8)
    if (MH_CreateHook(vtable[8], reinterpret_cast<void*>(&hkPresent),
                       reinterpret_cast<void**>(&s_originalPresent)) != MH_OK) {
        LOG_HOOK("DX11Hook: MH_CreateHook failed for Present");
        return false;
    }

    // Hook ResizeBuffers (VMT index 13)
    if (MH_CreateHook(vtable[13], reinterpret_cast<void*>(&hkResizeBuffers),
                       reinterpret_cast<void**>(&s_originalResizeBuffers)) != MH_OK) {
        LOG_HOOK("DX11Hook: MH_CreateHook failed for ResizeBuffers");
        return false;
    }

    // Enable both hooks
    if (MH_EnableHook(vtable[8]) != MH_OK || MH_EnableHook(vtable[13]) != MH_OK) {
        LOG_HOOK("DX11Hook: MH_EnableHook failed");
        return false;
    }

    m_initialized = true;
    LOG_HOOK("DX11Hook: Hooks installed successfully");
    return true;
}

void DX11Hook::Shutdown() {
    if (!m_initialized) return;

    if (m_imguiReady) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }

    ReleaseRenderTarget();
    MH_DisableHook(MH_ALL_HOOKS);

    m_initialized = false;
    LOG_HOOK("DX11Hook: Shutdown complete");
}

// ═══════════════════════════════════════════════════════════════════════
//  Render Target Management
// ═══════════════════════════════════════════════════════════════════════

void DX11Hook::InitRenderTarget(IDXGISwapChain* pSwapChain) {
    ID3D11Texture2D* backBuffer = nullptr;
    pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
        backBuffer->Release();
    }
}

void DX11Hook::ReleaseRenderTarget() {
    if (m_renderTargetView) {
        m_renderTargetView->Release();
        m_renderTargetView = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Hooked Present — Full D3D11 State Save/Restore + ImGui Rendering
// ═══════════════════════════════════════════════════════════════════════

HRESULT WINAPI DX11Hook::hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    auto& hook = Instance();

    // ── First-call initialization ─────────────────────────────────────
    if (!hook.m_imguiReady) {
        LOG_HOOK("DX11Hook: First Present call — initializing ImGui...");

        // Capture device + context from the real swap chain
        if (SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&hook.m_device)))) {
            hook.m_device->GetImmediateContext(&hook.m_context);
            hook.m_swapChain = pSwapChain;

            // Get the game's window handle from the swap chain desc
            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            HWND hWnd = desc.OutputWindow;

            // Initialize ImGui
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.IniFilename = nullptr;  // Don't save ImGui layout to disk

            ImGui_ImplWin32_Init(hWnd);
            ImGui_ImplDX11_Init(hook.m_device, hook.m_context);

            // Setup the ImGui overlay theme
            ImGuiOverlay::Instance().SetupTheme();

            // Create render target
            hook.InitRenderTarget(pSwapChain);

            // Install WndProc hook for input
            InputHook::Instance().Init(hWnd);

            hook.m_imguiReady = true;
            LOG_HOOK("DX11Hook: ImGui initialized on HWND 0x%p", hWnd);
        }
    }

    if (!hook.m_imguiReady || !hook.m_renderTargetView) {
        return s_originalPresent(pSwapChain, SyncInterval, Flags);
    }

    // ═══════════════════════════════════════════════════════════════════
    //  CRITICAL: Save the engine's ENTIRE D3D11 state before ImGui draw
    // ═══════════════════════════════════════════════════════════════════
    struct D3D11StateBackup {
        UINT                          ScissorRectsCount, ViewportsCount;
        D3D11_RECT                    ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D11_VIEWPORT                Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11RasterizerState*        RS;
        ID3D11BlendState*             BlendState;
        FLOAT                         BlendFactor[4];
        UINT                          SampleMask;
        UINT                          StencilRef;
        ID3D11DepthStencilState*      DepthStencilState;
        ID3D11ShaderResourceView*     PSShaderResource;
        ID3D11SamplerState*           PSSampler;
        ID3D11PixelShader*            PS;
        ID3D11VertexShader*           VS;
        ID3D11GeometryShader*         GS;
        UINT                          PSInstancesCount, VSInstancesCount, GSInstancesCount;
        ID3D11ClassInstance*          PSInstances[256], *VSInstances[256], *GSInstances[256];
        D3D11_PRIMITIVE_TOPOLOGY      PrimitiveTopology;
        ID3D11Buffer*                 IndexBuffer, *VertexBuffer, *VSConstantBuffer;
        UINT                          IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT                   IndexBufferFormat;
        ID3D11InputLayout*            InputLayout;
        ID3D11RenderTargetView*       RenderTargetView;
        ID3D11DepthStencilView*       DepthStencilView;
    };

    D3D11StateBackup state = {};
    auto* ctx = hook.m_context;

    // Save all state
    state.ScissorRectsCount = state.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetScissorRects(&state.ScissorRectsCount, state.ScissorRects);
    ctx->RSGetViewports(&state.ViewportsCount, state.Viewports);
    ctx->RSGetState(&state.RS);
    ctx->OMGetBlendState(&state.BlendState, state.BlendFactor, &state.SampleMask);
    ctx->OMGetDepthStencilState(&state.DepthStencilState, &state.StencilRef);
    ctx->PSGetShaderResources(0, 1, &state.PSShaderResource);
    ctx->PSGetSamplers(0, 1, &state.PSSampler);
    state.PSInstancesCount = state.VSInstancesCount = state.GSInstancesCount = 256;
    ctx->PSGetShader(&state.PS, state.PSInstances, &state.PSInstancesCount);
    ctx->VSGetShader(&state.VS, state.VSInstances, &state.VSInstancesCount);
    ctx->GSGetShader(&state.GS, state.GSInstances, &state.GSInstancesCount);
    ctx->VSGetConstantBuffers(0, 1, &state.VSConstantBuffer);
    ctx->IAGetPrimitiveTopology(&state.PrimitiveTopology);
    ctx->IAGetIndexBuffer(&state.IndexBuffer, &state.IndexBufferFormat, &state.IndexBufferOffset);
    ctx->IAGetVertexBuffers(0, 1, &state.VertexBuffer, &state.VertexBufferStride, &state.VertexBufferOffset);
    ctx->IAGetInputLayout(&state.InputLayout);
    ctx->OMGetRenderTargets(1, &state.RenderTargetView, &state.DepthStencilView);

    // ── Render ImGui ──────────────────────────────────────────────────
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Draw the overlay UI
    ImGuiOverlay::Instance().Render();

    ImGui::EndFrame();
    ImGui::Render();

    // Set our render target for ImGui draw
    ctx->OMSetRenderTargets(1, &hook.m_renderTargetView, nullptr);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // ═══════════════════════════════════════════════════════════════════
    //  CRITICAL: Restore the engine's ENTIRE D3D11 state after ImGui draw
    // ═══════════════════════════════════════════════════════════════════
    ctx->RSSetScissorRects(state.ScissorRectsCount, state.ScissorRects);
    ctx->RSSetViewports(state.ViewportsCount, state.Viewports);
    ctx->RSSetState(state.RS);
    if (state.RS) state.RS->Release();

    ctx->OMSetBlendState(state.BlendState, state.BlendFactor, state.SampleMask);
    if (state.BlendState) state.BlendState->Release();

    ctx->OMSetDepthStencilState(state.DepthStencilState, state.StencilRef);
    if (state.DepthStencilState) state.DepthStencilState->Release();

    ctx->PSSetShaderResources(0, 1, &state.PSShaderResource);
    if (state.PSShaderResource) state.PSShaderResource->Release();

    ctx->PSSetSamplers(0, 1, &state.PSSampler);
    if (state.PSSampler) state.PSSampler->Release();

    ctx->PSSetShader(state.PS, state.PSInstances, state.PSInstancesCount);
    if (state.PS) state.PS->Release();
    for (UINT i = 0; i < state.PSInstancesCount; i++)
        if (state.PSInstances[i]) state.PSInstances[i]->Release();

    ctx->VSSetShader(state.VS, state.VSInstances, state.VSInstancesCount);
    if (state.VS) state.VS->Release();
    for (UINT i = 0; i < state.VSInstancesCount; i++)
        if (state.VSInstances[i]) state.VSInstances[i]->Release();

    ctx->GSSetShader(state.GS, state.GSInstances, state.GSInstancesCount);
    if (state.GS) state.GS->Release();
    for (UINT i = 0; i < state.GSInstancesCount; i++)
        if (state.GSInstances[i]) state.GSInstances[i]->Release();

    ctx->VSSetConstantBuffers(0, 1, &state.VSConstantBuffer);
    if (state.VSConstantBuffer) state.VSConstantBuffer->Release();

    ctx->IASetPrimitiveTopology(state.PrimitiveTopology);
    ctx->IASetIndexBuffer(state.IndexBuffer, state.IndexBufferFormat, state.IndexBufferOffset);
    if (state.IndexBuffer) state.IndexBuffer->Release();

    ctx->IASetVertexBuffers(0, 1, &state.VertexBuffer, &state.VertexBufferStride, &state.VertexBufferOffset);
    if (state.VertexBuffer) state.VertexBuffer->Release();

    ctx->IASetInputLayout(state.InputLayout);
    if (state.InputLayout) state.InputLayout->Release();

    ctx->OMSetRenderTargets(1, &state.RenderTargetView, state.DepthStencilView);
    if (state.RenderTargetView) state.RenderTargetView->Release();
    if (state.DepthStencilView) state.DepthStencilView->Release();

    // ── Call original Present ─────────────────────────────────────────
    return s_originalPresent(pSwapChain, SyncInterval, Flags);
}

// ═══════════════════════════════════════════════════════════════════════
//  Hooked ResizeBuffers — Release/Recreate render target
// ═══════════════════════════════════════════════════════════════════════

HRESULT WINAPI DX11Hook::hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
                                          UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                          UINT SwapChainFlags) {
    auto& hook = Instance();

    LOG_HOOK("DX11Hook: ResizeBuffers called (%ux%u)", Width, Height);

    // MUST release render target view before ResizeBuffers, or it will fail
    hook.ReleaseRenderTarget();

    HRESULT hr = s_originalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (SUCCEEDED(hr)) {
        // Recreate the render target from the new back buffer
        hook.InitRenderTarget(pSwapChain);
    } else {
        LOG_HOOK("DX11Hook: ResizeBuffers failed (HRESULT: 0x%08lX)", hr);
    }

    return hr;
}

} // namespace acu
