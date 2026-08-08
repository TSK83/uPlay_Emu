// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Core DLL Entry Point (ACU-Core.dll)
// ═══════════════════════════════════════════════════════════════════════
//  Initialization sequence (runs on a dedicated thread to avoid loader lock):
//    1. Open shared memory config from launcher
//    2. Initialize MinHook
//    3. Initialize logger
//    4. Run MIC bypass (AOB scan + patch)
//    5. Initialize PRUDP & Pia handlers
//    6. Install Winsock hooks (DNS redirect + packet inspection)
//    7. If NOT dedicated: Install DX11 hooks + ImGui
//    8. If dedicated: Disable rendering, audio, auto-create session
// ═══════════════════════════════════════════════════════════════════════

#include "config.hpp"
#include "logger.hpp"
#include "integrity_bypass.hpp"
#include "dx11_hook.hpp"
#include "input_hook.hpp"
#include "imgui_overlay.hpp"
#include "network_hook.hpp"
#include "prudp_handler.hpp"
#include "pia_handler.hpp"
#include "dedicated_mode.hpp"

#include <MinHook.h>
#include <Windows.h>
#include <thread>
#include <chrono>

// Forward declaration
static void InitThread(HMODULE hModule);

// ═══════════════════════════════════════════════════════════════════════
//  DLL Entry Point
// ═══════════════════════════════════════════════════════════════════════

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Spawn initialization on a separate thread to avoid DLL loader lock.
            // The loader lock prevents calling LoadLibrary, creating windows, or
            // doing anything complex inside DllMain.
            std::thread(InitThread, hModule).detach();
            break;

        case DLL_PROCESS_DETACH:
            // Cleanup in reverse order
            acu::DedicatedMode::Instance().StopHeartbeat();
            acu::NetworkHook::Instance().Shutdown();
            acu::InputHook::Instance().Shutdown();
            acu::DX11Hook::Instance().Shutdown();
            MH_Uninitialize();
            acu::Logger::Instance().Shutdown();
            break;
    }
    return TRUE;
}

// ═══════════════════════════════════════════════════════════════════════
//  Initialization Thread
// ═══════════════════════════════════════════════════════════════════════

static void InitThread(HMODULE hModule) {
    // Brief delay to let the process finish basic initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ── 1. Initialize Logger ──────────────────────────────────────────
    acu::Logger::Instance().Init("acu_client.log");
    LOG(GENERAL, "════════════════════════════════════════════════════════");
    LOG(GENERAL, "  ACU Custom Client — Core DLL v1.0.0");
    LOG(GENERAL, "  Build: " __DATE__ " " __TIME__);
    LOG(GENERAL, "════════════════════════════════════════════════════════");

    // ── 2. Read Shared Configuration ──────────────────────────────────
    acu::SharedMemHandle sharedMem;
    bool hasConfig = sharedMem.Open();

    acu::SharedConfig localConfig;
    localConfig.SetDefaults();

    if (hasConfig && sharedMem.pConfig->IsValid()) {
        localConfig = *sharedMem.pConfig;
        LOG(GENERAL, "Shared config loaded successfully");
        LOG(GENERAL, "  Player: %ls", localConfig.player_name);
        LOG(GENERAL, "  UplayID: 0x%016llX", localConfig.uplay_id);
        LOG(GENERAL, "  Master: %s:%u", localConfig.master_server_ip, localConfig.master_server_port);
        LOG(GENERAL, "  Dedicated: %s", localConfig.is_dedicated ? "YES" : "NO");
    } else {
        LOG(GENERAL, "WARNING: Shared config not available — using defaults");
        LOG(GENERAL, "  (This is normal if launched without ACU-Launcher.exe)");
    }

    // ── 3. Initialize MinHook ─────────────────────────────────────────
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK) {
        LOG(HOOK, "FATAL: MinHook initialization failed (status: %d)", mhStatus);
        return;
    }
    LOG(HOOK, "MinHook initialized");

    // ── 4. Integrity Bypass (Arxan MIC) ───────────────────────────────
    LOG(HOOK, "Running integrity bypass...");
    int patchCount = acu::IntegrityBypass::Apply();
    LOG(HOOK, "Integrity bypass complete (%d patches applied)", patchCount);

    // ── 5. Initialize Protocol Handlers ───────────────────────────────
    acu::PrudpHandler::Instance().Init();
    acu::PiaHandler::Instance().Init();

    // ── 6. Install Network Hooks ──────────────────────────────────────
    std::string masterIp(localConfig.master_server_ip);
    uint16_t masterPort = localConfig.master_server_port;

    acu::NetworkHook::Instance().Init(masterIp, masterPort);

    // If direct connect IP is specified, configure Pia handler
    if (localConfig.direct_connect_ip[0] != '\0') {
        acu::PiaHandler::Instance().SetDirectBindIP(localConfig.direct_connect_ip);
    }

    // ── 7. Mode-Specific Initialization ───────────────────────────────
    if (localConfig.is_dedicated) {
        // Dedicated server mode — no rendering, no ImGui
        acu::DedicatedMode::Instance().Init(masterIp, masterPort);
    } else {
        // Client mode — install DX11 hooks for ImGui overlay
        LOG(HOOK, "Installing DX11 hooks for ImGui overlay...");
        acu::ImGuiOverlay::Instance().SetMasterServer(masterIp, masterPort);

        if (!acu::DX11Hook::Instance().Init()) {
            LOG(HOOK, "WARNING: DX11 hook installation failed — overlay unavailable");
            LOG(HOOK, "  (This may happen if the game hasn't initialized DirectX yet)");
            LOG(HOOK, "  The hooks will retry on the first Present call.");
        }
    }

    // ── 8. Mark initialization complete ───────────────────────────────
    if (hasConfig && sharedMem.pConfig) {
        sharedMem.pConfig->core_initialized = true;
    }

    LOG(GENERAL, "════════════════════════════════════════════════════════");
    LOG(GENERAL, "  Core DLL initialization complete");
    if (!localConfig.is_dedicated) {
        LOG(GENERAL, "  Press F2 or Insert to toggle the multiplayer overlay");
    }
    LOG(GENERAL, "════════════════════════════════════════════════════════");
}
