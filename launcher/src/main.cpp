// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Launcher (Bootstrapper)
// ═══════════════════════════════════════════════════════════════════════
//  Launches ACU.exe in a suspended state, injects uplay_r1_loader64.dll
//  and ACU-Core.dll, writes shared configuration to a memory-mapped file,
//  then resumes the game's main thread.
//
//  Usage:
//    ACU-Launcher.exe [options]
//      --game-dir <path>       Path to ACU installation directory
//      --dedicated             Launch in headless dedicated server mode
//      --master-server <ip>    Master server IP (default: 127.0.0.1)
//      --port <port>           Master server port (default: 3000)
//      --name <name>           Player display name (default: Player)
//      --direct-ip <ip:port>   Direct connect to a specific host
//      --skip-intro            Skip intro videos (default: true)
// ═══════════════════════════════════════════════════════════════════════

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <random>
#include <chrono>

#include <Windows.h>
#include <Shlwapi.h>

#include "config.hpp"
#include "injector.hpp"

namespace fs = std::filesystem;

// ── Forward Declarations ──────────────────────────────────────────────
static void PrintBanner();
static void PrintUsage();
static uint64_t GenerateUplayId(const std::wstring& playerName);
static std::wstring GetLauncherDirectory();

// ═══════════════════════════════════════════════════════════════════════
//  Main Entry Point
// ═══════════════════════════════════════════════════════════════════════
int wmain(int argc, wchar_t* argv[]) {
    PrintBanner();

    // ── Parse Command-Line Arguments ──────────────────────────────────
    std::wstring gameDir;
    std::wstring playerName    = L"Player";
    std::string  masterIp      = "127.0.0.1";
    uint16_t     masterPort    = 3000;
    bool         isDedicated   = false;
    bool         skipIntro     = true;
    std::string  directIp;
    uint16_t     directPort    = 0;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];

        if (arg == L"--game-dir" && i + 1 < argc) {
            gameDir = argv[++i];
        }
        else if (arg == L"--dedicated") {
            isDedicated = true;
        }
        else if (arg == L"--master-server" && i + 1 < argc) {
            // Convert wide to narrow for IP
            std::wstring wip = argv[++i];
            masterIp = std::string(wip.begin(), wip.end());
        }
        else if (arg == L"--port" && i + 1 < argc) {
            masterPort = static_cast<uint16_t>(_wtoi(argv[++i]));
        }
        else if (arg == L"--name" && i + 1 < argc) {
            playerName = argv[++i];
        }
        else if (arg == L"--direct-ip" && i + 1 < argc) {
            std::wstring wdirect = argv[++i];
            std::string direct(wdirect.begin(), wdirect.end());
            // Parse ip:port
            auto colonPos = direct.rfind(':');
            if (colonPos != std::string::npos) {
                directIp   = direct.substr(0, colonPos);
                directPort = static_cast<uint16_t>(std::stoi(direct.substr(colonPos + 1)));
            } else {
                directIp   = direct;
                directPort = 3074; // Default PRUDP game port
            }
        }
        else if (arg == L"--skip-intro") {
            skipIntro = true;
        }
        else if (arg == L"--no-skip-intro") {
            skipIntro = false;
        }
        else if (arg == L"--help" || arg == L"-h") {
            PrintUsage();
            return 0;
        }
        else {
            fwprintf(stderr, L"[Launcher] Unknown argument: %s\n", arg.c_str());
            PrintUsage();
            return 1;
        }
    }

    // ── Resolve Paths ─────────────────────────────────────────────────
    std::wstring launcherDir = GetLauncherDirectory();

    // If no game dir specified, look for ACU.exe in the launcher directory
    if (gameDir.empty()) {
        gameDir = launcherDir;
    }

    // Normalize path
    fs::path gameDirPath = fs::absolute(gameDir);
    fs::path acuExePath  = gameDirPath / L"ACU.exe";

    if (!fs::exists(acuExePath)) {
        fwprintf(stderr, L"[Launcher] ERROR: ACU.exe not found at: %s\n", acuExePath.c_str());
        fwprintf(stderr, L"           Use --game-dir <path> to specify the ACU installation directory.\n");
        return 1;
    }

    wprintf(L"[Launcher] Game directory : %s\n", gameDirPath.c_str());
    wprintf(L"[Launcher] ACU executable : %s\n", acuExePath.c_str());
    wprintf(L"[Launcher] Mode           : %s\n", isDedicated ? L"DEDICATED SERVER" : L"CLIENT");
    wprintf(L"[Launcher] Player name    : %s\n", playerName.c_str());
    printf( "[Launcher] Master server  : %s:%u\n", masterIp.c_str(), masterPort);

    if (!directIp.empty()) {
        printf("[Launcher] Direct connect : %s:%u\n", directIp.c_str(), directPort);
    }

    // ── Build DLL Paths ───────────────────────────────────────────────
    fs::path uplayDllPath = fs::absolute(launcherDir) / L"uplay_r1_loader64.dll";
    fs::path coreDllPath  = fs::absolute(launcherDir) / L"ACU-Core.dll";

    if (!fs::exists(uplayDllPath)) {
        fwprintf(stderr, L"[Launcher] WARNING: uplay_r1_loader64.dll not found at: %s\n", uplayDllPath.c_str());
        fwprintf(stderr, L"           Uplay emulation will NOT be available.\n");
    }
    if (!fs::exists(coreDllPath)) {
        fwprintf(stderr, L"[Launcher] ERROR: ACU-Core.dll not found at: %s\n", coreDllPath.c_str());
        return 1;
    }

    // ── Create Shared Configuration ───────────────────────────────────
    acu::SharedMemHandle sharedMem;
    if (!sharedMem.Create()) {
        fprintf(stderr, "[Launcher] ERROR: Failed to create shared memory. Error: 0x%08lX\n", GetLastError());
        return 1;
    }

    acu::SharedConfig* cfg = sharedMem.pConfig;
    cfg->SetDefaults();

    cfg->is_dedicated = isDedicated;
    cfg->skip_intro   = skipIntro;

    wcscpy_s(cfg->player_name, playerName.c_str());
    cfg->uplay_id = GenerateUplayId(playerName);

    strcpy_s(cfg->master_server_ip, masterIp.c_str());
    cfg->master_server_port = masterPort;

    wcscpy_s(cfg->game_dir, gameDirPath.c_str());
    wcscpy_s(cfg->launcher_dir, fs::absolute(launcherDir).c_str());

    if (!directIp.empty()) {
        strcpy_s(cfg->direct_connect_ip, directIp.c_str());
        cfg->direct_connect_port = directPort;
    }

    wprintf(L"[Launcher] UplayID        : 0x%016llX\n", cfg->uplay_id);
    printf( "[Launcher] Shared config created successfully.\n\n");

    // ── Launch ACU.exe (Suspended) ────────────────────────────────────
    printf("[Launcher] Launching ACU.exe in suspended state...\n");

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // Build command line (pass --dedicated if applicable)
    std::wstring cmdLine = L"\"" + acuExePath.wstring() + L"\"";

    BOOL created = CreateProcessW(
        acuExePath.c_str(),         // Application name
        cmdLine.data(),             // Command line (mutable!)
        nullptr,                    // Process security attributes
        nullptr,                    // Thread security attributes
        FALSE,                      // Inherit handles
        CREATE_SUSPENDED,           // Creation flags — CRITICAL: process starts frozen
        nullptr,                    // Environment (inherit)
        gameDirPath.c_str(),        // Working directory
        &si,
        &pi
    );

    if (!created) {
        fprintf(stderr, "[Launcher] ERROR: CreateProcessW failed. Error: 0x%08lX\n", GetLastError());
        return 1;
    }

    printf("[Launcher] ACU.exe launched (PID: %lu, TID: %lu)\n", pi.dwProcessId, pi.dwThreadId);

    // ── Inject DLLs ───────────────────────────────────────────────────
    // Inject Uplay emulator FIRST (must be loaded before the engine queries Uplay APIs)
    if (fs::exists(uplayDllPath)) {
        printf("[Launcher] Injecting uplay_r1_loader64.dll...\n");
        auto result = acu::InjectDLL(pi.hProcess, uplayDllPath.wstring());
        if (result != acu::InjectionResult::Success) {
            fprintf(stderr, "[Launcher] ERROR: Uplay DLL injection failed: %s\n",
                    acu::InjectionResultToString(result));
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return 1;
        }
        printf("[Launcher] uplay_r1_loader64.dll injected successfully.\n");
    }

    // Inject Core DLL (hooks, ImGui, networking)
    printf("[Launcher] Injecting ACU-Core.dll...\n");
    auto coreResult = acu::InjectDLL(pi.hProcess, coreDllPath.wstring());
    if (coreResult != acu::InjectionResult::Success) {
        fprintf(stderr, "[Launcher] ERROR: Core DLL injection failed: %s\n",
                acu::InjectionResultToString(coreResult));
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }
    printf("[Launcher] ACU-Core.dll injected successfully.\n\n");

    // ── Resume Main Thread ────────────────────────────────────────────
    printf("[Launcher] Resuming ACU.exe main thread...\n");
    DWORD prevSuspendCount = ResumeThread(pi.hThread);
    if (prevSuspendCount == static_cast<DWORD>(-1)) {
        fprintf(stderr, "[Launcher] ERROR: ResumeThread failed. Error: 0x%08lX\n", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }

    printf("[Launcher] Main thread resumed (previous suspend count: %lu)\n", prevSuspendCount);
    printf("[Launcher] ════════════════════════════════════════════════\n");
    printf("[Launcher] ACU Custom Client launched successfully!\n");

    if (isDedicated) {
        printf("[Launcher] Running in DEDICATED SERVER mode.\n");
        printf("[Launcher] Waiting for process to exit...\n");
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        printf("[Launcher] ACU.exe exited with code: %lu\n", exitCode);
    } else {
        printf("[Launcher] Press F2/Insert in-game to open the multiplayer overlay.\n");
        printf("[Launcher] Launcher will now exit. The game continues running.\n");
    }

    // ── Cleanup ───────────────────────────────────────────────────────
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    // Note: SharedMemHandle is NOT closed here — DLLs in ACU.exe need it.
    // It will be cleaned up when the launcher process exits (after WaitForSingleObject
    // in dedicated mode, or immediately in client mode — DLLs have their own handle).

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Utility Functions
// ═══════════════════════════════════════════════════════════════════════

static void PrintBanner() {
    printf("\n");
    printf("  ╔═══════════════════════════════════════════════════════════╗\n");
    printf("  ║         ACU Custom Client — Launcher v1.0.0              ║\n");
    printf("  ║     Assassin's Creed Unity Multiplayer Preservation      ║\n");
    printf("  ╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

static void PrintUsage() {
    printf("Usage: ACU-Launcher.exe [options]\n\n");
    printf("Options:\n");
    printf("  --game-dir <path>      Path to ACU installation directory\n");
    printf("  --dedicated            Launch in headless dedicated server mode\n");
    printf("  --master-server <ip>   Master server IP (default: 127.0.0.1)\n");
    printf("  --port <port>          Master server port (default: 3000)\n");
    printf("  --name <name>          Player display name (default: Player)\n");
    printf("  --direct-ip <ip:port>  Direct connect to a specific host\n");
    printf("  --skip-intro           Skip intro videos (default: enabled)\n");
    printf("  --no-skip-intro        Do NOT skip intro videos\n");
    printf("  --help, -h             Show this help message\n");
    printf("\nExamples:\n");
    printf("  ACU-Launcher.exe --game-dir \"C:\\Games\\ACU\" --name \"Arno\"\n");
    printf("  ACU-Launcher.exe --dedicated --master-server 192.168.1.100\n");
    printf("  ACU-Launcher.exe --direct-ip 10.10.0.5:3074\n");
}

/// Generates a deterministic, non-zero 64-bit UplayID from the player name.
/// Uses a hash of the player name + machine-specific entropy for consistency.
static uint64_t GenerateUplayId(const std::wstring& playerName) {
    // Combine player name hash with a time-seeded component for uniqueness
    std::hash<std::wstring> hasher;
    uint64_t nameHash = hasher(playerName);

    // Get a machine-specific seed from the computer name
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &size);
    uint64_t machineHash = hasher(computerName);

    // Mix hashes to produce a deterministic ID
    uint64_t id = nameHash ^ (machineHash << 13) ^ (machineHash >> 7);

    // Ensure non-zero (engine rejects zero IDs)
    if (id == 0) id = 0xACE0ACE0ACE0ACE0ULL;

    // Set high bit to mark as custom/emulated ID
    id |= 0x8000000000000000ULL;

    return id;
}

/// Get the directory containing the launcher executable.
static std::wstring GetLauncherDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return std::wstring(path);
}
