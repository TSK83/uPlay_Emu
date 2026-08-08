// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Dedicated Server Mode Implementation
// ═══════════════════════════════════════════════════════════════════════
//  When --dedicated is passed, this module:
//    1. Hooks D3D11CreateDeviceAndSwapChain to return a stub device
//    2. Hooks XAudio2Create to disable audio
//    3. Auto-invokes the engine's internal "Create Session" function
//    4. Runs a heartbeat loop against the master server
// ═══════════════════════════════════════════════════════════════════════

#include "dedicated_mode.hpp"
#include "integrity_bypass.hpp"
#include "logger.hpp"

#include <MinHook.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <d3d11.h>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace acu {

DedicatedMode& DedicatedMode::Instance() {
    static DedicatedMode s_instance;
    return s_instance;
}

// ═══════════════════════════════════════════════════════════════════════
//  Rendering Disable Hook
// ═══════════════════════════════════════════════════════════════════════

static decltype(&D3D11CreateDeviceAndSwapChain) o_D3D11CreateDeviceAndSwapChain = nullptr;

static HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
    LOG_ENGINE("[Dedicated] D3D11CreateDeviceAndSwapChain intercepted — using WARP (no GPU)");

    // Force WARP software renderer to avoid GPU usage
    return o_D3D11CreateDeviceAndSwapChain(
        nullptr,                       // No specific adapter
        D3D_DRIVER_TYPE_WARP,         // Software rasterizer — no GPU load
        Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain, ppDevice,
        pFeatureLevel, ppImmediateContext
    );
}

void DedicatedMode::DisableRendering() {
    LOG_ENGINE("[Dedicated] Disabling GPU rendering (forcing WARP adapter)...");

    // Hook D3D11CreateDeviceAndSwapChain to force WARP
    HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
    if (!hD3D11) {
        hD3D11 = LoadLibraryA("d3d11.dll");
    }

    if (hD3D11) {
        auto pFunc = GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain");
        if (pFunc) {
            if (MH_CreateHook(pFunc, reinterpret_cast<void*>(&hk_D3D11CreateDeviceAndSwapChain),
                               reinterpret_cast<void**>(&o_D3D11CreateDeviceAndSwapChain)) == MH_OK) {
                MH_EnableHook(pFunc);
                LOG_ENGINE("[Dedicated] D3D11CreateDeviceAndSwapChain hooked → WARP mode");
            }
        }
    }

    // Additionally, try to find and minimize the game window
    // (it will still create a window, but we can minimize it)
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        HWND hWnd = FindWindowW(nullptr, L"Assassin's Creed Unity");
        if (hWnd) {
            ShowWindow(hWnd, SW_MINIMIZE);
            LOG_ENGINE("[Dedicated] Game window minimized");
        }
    }).detach();
}

// ═══════════════════════════════════════════════════════════════════════
//  Audio Disable
// ═══════════════════════════════════════════════════════════════════════

// XAudio2Create hook — simply return success without creating the audio engine
using XAudio2CreateFn = HRESULT(WINAPI*)(void**, UINT32, UINT32);
static XAudio2CreateFn o_XAudio2Create = nullptr;

static HRESULT WINAPI hk_XAudio2Create(void** ppXAudio2, UINT32 Flags, UINT32 XAudio2Processor) {
    LOG_ENGINE("[Dedicated] XAudio2Create intercepted — audio disabled");
    *ppXAudio2 = nullptr;
    return S_OK;  // Pretend success, but don't create audio engine
}

void DedicatedMode::DisableAudio() {
    LOG_ENGINE("[Dedicated] Disabling audio engine...");

    // Try XAudio2_9.dll first (Windows 10+), then XAudio2_8.dll
    const char* xaudioDlls[] = { "XAudio2_9.dll", "XAudio2_8.dll", "XAudio2_7.dll" };

    for (const auto* dllName : xaudioDlls) {
        HMODULE hXAudio = GetModuleHandleA(dllName);
        if (!hXAudio) {
            hXAudio = LoadLibraryA(dllName);
        }

        if (hXAudio) {
            auto pFunc = GetProcAddress(hXAudio, "XAudio2Create");
            if (pFunc) {
                if (MH_CreateHook(pFunc, reinterpret_cast<void*>(&hk_XAudio2Create),
                                   reinterpret_cast<void**>(&o_XAudio2Create)) == MH_OK) {
                    MH_EnableHook(pFunc);
                    LOG_ENGINE("[Dedicated] %s::XAudio2Create hooked — audio muted", dllName);
                    return;
                }
            }
        }
    }

    LOG_ENGINE("[Dedicated] WARNING: Could not hook XAudio2Create (audio may still play)");
}

// ═══════════════════════════════════════════════════════════════════════
//  Auto Create Session
// ═══════════════════════════════════════════════════════════════════════

void DedicatedMode::AutoCreateSession() {
    LOG_ENGINE("[Dedicated] Auto-creating game session...");

    // TODO: RE-REQUIRED — This requires reverse engineering the ACU.exe binary
    // to find the session creation function. The typical approach:
    //
    // 1. AOB scan for the session manager vtable
    // 2. Call the "CreateSession" method with default parameters
    // 3. The function signature is approximately:
    //    void* __fastcall CreateSession(SessionManager* this, SessionParams* params)
    //
    // For now, we register with the master server and wait for incoming connections.

    try {
        httplib::Client cli(m_masterIp, m_masterPort);
        cli.set_connection_timeout(5, 0);

        json payload = {
            {"host_name",          "ACU Dedicated Server"},
            {"max_players",        4},
            {"current_players",    0},
            {"game_id",            "ACU_v1.5.0"},
            {"map_name",           "Paris_FreeRoam"},
            {"host_socket_id",     "dedicated_001"},
            {"heartbeat_interval", 30000},
            {"is_dedicated",       true}
        };

        auto res = cli.Post("/servers", payload.dump(), "application/json");

        if (res && res->status == 201) {
            auto j = json::parse(res->body);
            m_serverId = j.value("id", "");
            std::string joinCode = j.value("join_code", "??????");
            LOG_ENGINE("[Dedicated] Registered with master server (ID: %s, Code: %s)",
                       m_serverId.c_str(), joinCode.c_str());
            printf("[Dedicated] Join code: %s\n", joinCode.c_str());
        } else {
            LOG_ENGINE("[Dedicated] Failed to register with master server: %s",
                       res ? res->body.c_str() : "Connection failed");
        }
    } catch (const std::exception& e) {
        LOG_ENGINE("[Dedicated] Master server registration error: %s", e.what());
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Heartbeat Loop
// ═══════════════════════════════════════════════════════════════════════

void DedicatedMode::HeartbeatLoop() {
    while (m_heartbeatRunning.load()) {
        if (!m_serverId.empty()) {
            try {
                httplib::Client cli(m_masterIp, m_masterPort);
                cli.set_connection_timeout(3, 0);

                auto res = cli.Put("/servers/" + m_serverId + "/heartbeat", "", "application/json");

                if (!res || res->status != 200) {
                    LOG_ENGINE("[Dedicated] Heartbeat failed — re-registering...");
                    AutoCreateSession();
                }
            } catch (...) {
                LOG_ENGINE("[Dedicated] Heartbeat exception");
            }
        }

        // Sleep for 25 seconds (heartbeat_interval is 30s, send before timeout)
        for (int i = 0; i < 25 && m_heartbeatRunning.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void DedicatedMode::StartHeartbeat() {
    m_heartbeatRunning.store(true);
    std::thread(&DedicatedMode::HeartbeatLoop, this).detach();
    LOG_ENGINE("[Dedicated] Heartbeat loop started (interval: 25s)");
}

void DedicatedMode::StopHeartbeat() {
    m_heartbeatRunning.store(false);
    LOG_ENGINE("[Dedicated] Heartbeat loop stopped");
}

// ═══════════════════════════════════════════════════════════════════════
//  Initialization
// ═══════════════════════════════════════════════════════════════════════

void DedicatedMode::Init(const std::string& masterIp, uint16_t masterPort) {
    m_masterIp   = masterIp;
    m_masterPort = masterPort;
    m_active     = true;

    LOG_ENGINE("════════════════════════════════════════════════════");
    LOG_ENGINE("  DEDICATED SERVER MODE ACTIVE");
    LOG_ENGINE("  Master Server: %s:%u", masterIp.c_str(), masterPort);
    LOG_ENGINE("════════════════════════════════════════════════════");

    DisableRendering();
    DisableAudio();

    // Delay session creation to allow engine to finish initializing
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        AutoCreateSession();
        StartHeartbeat();
    }).detach();
}

} // namespace acu
