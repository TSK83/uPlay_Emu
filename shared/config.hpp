#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Shared Configuration (IPC via Memory-Mapped File)
// ═══════════════════════════════════════════════════════════════════════
//  Shared between ACU-Launcher.exe, ACU-Core.dll, and uplay_r1_loader64.dll
//  via a named memory-mapped file for zero-copy IPC.
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_SHARED_CONFIG_HPP
#define ACU_SHARED_CONFIG_HPP

#include <cstdint>
#include <Windows.h>

namespace acu {

// ── Constants ─────────────────────────────────────────────────────────
inline constexpr const wchar_t* kSharedMemName   = L"ACU_CustomClient_SharedConfig_v1";
inline constexpr const wchar_t* kMutexName       = L"ACU_CustomClient_Mutex_v1";
inline constexpr uint32_t       kConfigMagic     = 0xACU10001;
inline constexpr size_t         kMaxPathLen       = 512;
inline constexpr size_t         kMaxNameLen       = 64;
inline constexpr size_t         kMaxIpLen         = 46;  // IPv6 max

// ── Shared Configuration Structure ────────────────────────────────────
// Must be POD-compatible for cross-process memory mapping.
#pragma pack(push, 1)
struct SharedConfig {
    uint32_t magic;                           // Validation magic number
    uint32_t version;                         // Config struct version

    // Launch mode
    bool     is_dedicated;                    // --dedicated headless server mode
    bool     skip_intro;                      // --skip-intro bypass intro videos

    // Player identity
    wchar_t  player_name[kMaxNameLen];        // Player display name
    uint64_t uplay_id;                        // Generated mock Uplay ID (must be non-zero)

    // Master server connection
    char     master_server_ip[kMaxIpLen];     // Master server IP/hostname
    uint16_t master_server_port;              // Master server port (default 3000)

    // Game directory
    wchar_t  game_dir[kMaxPathLen];           // Path to ACU installation
    wchar_t  launcher_dir[kMaxPathLen];       // Path to launcher directory (for DLL paths)

    // Direct connect override
    char     direct_connect_ip[kMaxIpLen];    // If set, bypass master server and connect directly
    uint16_t direct_connect_port;

    // Runtime flags (written by Core DLL)
    bool     core_initialized;                // Set true after ACU-Core.dll init completes
    bool     uplay_initialized;              // Set true after uplay_r1_loader64.dll init completes

    // ── Helpers ───────────────────────────────────────────────────────
    void SetDefaults() {
        magic   = kConfigMagic;
        version = 1;

        is_dedicated = false;
        skip_intro   = true;

        wcscpy_s(player_name, L"Player");
        uplay_id = 0;

        strcpy_s(master_server_ip, "127.0.0.1");
        master_server_port = 3000;

        game_dir[0]     = L'\0';
        launcher_dir[0] = L'\0';

        direct_connect_ip[0] = '\0';
        direct_connect_port  = 0;

        core_initialized  = false;
        uplay_initialized = false;
    }

    bool IsValid() const {
        return magic == kConfigMagic && version == 1;
    }
};
#pragma pack(pop)

// ── Shared Memory Helper ──────────────────────────────────────────────
// Creates or opens the shared config. Returns handle + pointer.
struct SharedMemHandle {
    HANDLE   hMapFile = nullptr;
    SharedConfig* pConfig = nullptr;

    bool Create() {
        hMapFile = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, sizeof(SharedConfig), kSharedMemName
        );
        if (!hMapFile) return false;

        pConfig = static_cast<SharedConfig*>(
            MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedConfig))
        );
        if (!pConfig) {
            CloseHandle(hMapFile);
            hMapFile = nullptr;
            return false;
        }
        return true;
    }

    bool Open() {
        hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kSharedMemName);
        if (!hMapFile) return false;

        pConfig = static_cast<SharedConfig*>(
            MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedConfig))
        );
        if (!pConfig) {
            CloseHandle(hMapFile);
            hMapFile = nullptr;
            return false;
        }
        return true;
    }

    void Close() {
        if (pConfig) { UnmapViewOfFile(pConfig); pConfig = nullptr; }
        if (hMapFile) { CloseHandle(hMapFile); hMapFile = nullptr; }
    }

    ~SharedMemHandle() { Close(); }
};

} // namespace acu

#endif // ACU_SHARED_CONFIG_HPP
