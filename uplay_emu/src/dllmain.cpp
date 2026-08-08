// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Uplay R1 Loader DLL Entry Point
// ═══════════════════════════════════════════════════════════════════════
//  This DLL acts as a drop-in replacement for the original
//  uplay_r1_loader64.dll. The game's import table resolves the Uplay
//  API functions from this DLL at load time via the .def file exports.
// ═══════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include "config.hpp"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hModule);

            OutputDebugStringA("[UplayEmu] uplay_r1_loader64.dll loaded (emulation mode)\n");

            // Try to signal to the shared config that we're initialized
            acu::SharedMemHandle sharedMem;
            if (sharedMem.Open() && sharedMem.pConfig && sharedMem.pConfig->IsValid()) {
                sharedMem.pConfig->uplay_initialized = true;
                OutputDebugStringA("[UplayEmu] Shared config updated: uplay_initialized = true\n");
            }
            break;
        }

        case DLL_PROCESS_DETACH:
            OutputDebugStringA("[UplayEmu] uplay_r1_loader64.dll unloaded\n");
            break;
    }
    return TRUE;
}
