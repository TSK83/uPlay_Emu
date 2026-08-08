// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Uplay R1 API Emulation Implementation
// ═══════════════════════════════════════════════════════════════════════
//  All functions return 0 (success) for auth checks and generate a
//  structurally valid, non-zero 64-bit UplayID. This satisfies the
//  engine's multiplayer state machine requirements.
// ═══════════════════════════════════════════════════════════════════════

#include "uplay_api.hpp"
#include "config.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <Windows.h>
#include <ShlObj.h>

// ── Global State ──────────────────────────────────────────────────────
static uint64_t g_uplayId       = 0;
static wchar_t  g_playerName[64] = L"ACU_Player";
static bool     g_initialized    = false;

// Attempt to read UplayID and player name from shared config
static void LoadFromSharedConfig() {
    acu::SharedMemHandle sharedMem;
    if (sharedMem.Open() && sharedMem.pConfig && sharedMem.pConfig->IsValid()) {
        g_uplayId = sharedMem.pConfig->uplay_id;
        wcscpy_s(g_playerName, sharedMem.pConfig->player_name);
    }

    // Ensure non-zero ID (engine rejects zero)
    if (g_uplayId == 0) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        g_uplayId = gen() | 0x8000000000000000ULL;
    }
}

// Debug logging helper
#ifdef _DEBUG
#define UPLAY_LOG(fmt, ...) do { \
    char _buf[512]; \
    snprintf(_buf, sizeof(_buf), "[UplayEmu] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(_buf); \
} while(0)
#else
#define UPLAY_LOG(fmt, ...) do { \
    char _buf[512]; \
    snprintf(_buf, sizeof(_buf), "[UplayEmu] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(_buf); \
} while(0)
#endif

// ═══════════════════════════════════════════════════════════════════════
//  Core Lifecycle
// ═══════════════════════════════════════════════════════════════════════

extern "C" {

int __cdecl UplayStart(unsigned productId, unsigned flags) {
    UPLAY_LOG("UplayStart(productId=%u, flags=%u) -> 0", productId, flags);
    return 0;
}

int __cdecl UplayStop() {
    UPLAY_LOG("UplayStop() -> 0");
    return 0;
}

int __cdecl UplayInitialize(unsigned version, void* config) {
    if (!g_initialized) {
        LoadFromSharedConfig();
        g_initialized = true;
    }
    UPLAY_LOG("UplayInitialize(version=%u) -> 0 (UplayID=0x%016llX)", version, g_uplayId);
    return 0;  // SUCCESS — critical for engine initialization
}

int __cdecl UplayUpdate() {
    // Called every frame — must be fast and return success
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  User / Authentication — Critical for multiplayer state machine
// ═══════════════════════════════════════════════════════════════════════

int __cdecl UplayUserGetTicket(void* outTicket, unsigned maxSize, unsigned* outSize) {
    UPLAY_LOG("UplayUserGetTicket() -> generating mock ticket");

    // Generate a structurally valid mock Uplay ticket.
    // The ticket must contain the UplayID and not be empty,
    // or the engine's multiplayer state machine will reject it.
    if (outTicket && maxSize >= 128) {
        // Simple ticket structure: [magic(4)] [version(4)] [uplayId(8)] [timestamp(8)] [padding]
        uint8_t* ticket = static_cast<uint8_t*>(outTicket);
        memset(ticket, 0, maxSize);

        // Magic "UPLK"
        ticket[0] = 'U'; ticket[1] = 'P'; ticket[2] = 'L'; ticket[3] = 'K';

        // Version
        *reinterpret_cast<uint32_t*>(ticket + 4) = 1;

        // UplayID (64-bit, non-zero)
        *reinterpret_cast<uint64_t*>(ticket + 8) = g_uplayId;

        // Timestamp (current time as Unix epoch)
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        *reinterpret_cast<uint64_t*>(ticket + 16) = 
            (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

        // Fill remainder with deterministic padding based on UplayID
        for (unsigned i = 24; i < 128 && i < maxSize; ++i) {
            ticket[i] = static_cast<uint8_t>((g_uplayId >> ((i - 24) % 8 * 8)) & 0xFF);
        }

        if (outSize) *outSize = 128;
    } else if (outSize) {
        *outSize = 128;
    }

    return 0;  // SUCCESS
}

int __cdecl UplayUserGetTicketSize(unsigned* outSize) {
    if (outSize) *outSize = 128;
    return 0;
}

int __cdecl UplayUserGetCdKeys(void** outList) {
    if (outList) *outList = nullptr;
    return 0;
}

int __cdecl UplayUserGetCdKeyUtf8(char* outKey, unsigned maxSize) {
    if (outKey && maxSize > 0) {
        strncpy_s(outKey, maxSize, "ACU-CUSTOM-CLIENT-KEY-0000", maxSize - 1);
    }
    return 0;
}

int __cdecl UplayUserGetCredentials(void* outCreds) {
    return 0;
}

int __cdecl UplayUserIsConnected() {
    return 1;  // Always connected
}

int __cdecl UplayUserIsOwned(unsigned productId) {
    UPLAY_LOG("UplayUserIsOwned(productId=%u) -> 1 (owned)", productId);
    return 1;  // Always owned
}

int __cdecl UplayUserGetEmail(char* outEmail, unsigned maxSize) {
    if (outEmail && maxSize > 0) {
        strncpy_s(outEmail, maxSize, "player@acu-custom.local", maxSize - 1);
    }
    return 0;
}

int __cdecl UplayUserGetName(wchar_t* outName, unsigned maxSize) {
    if (outName && maxSize > 0) {
        wcsncpy_s(outName, maxSize, g_playerName, maxSize - 1);
    }
    return 0;
}

int __cdecl UplayUserGetNameUtf8(char* outName, unsigned maxSize) {
    if (outName && maxSize > 0) {
        // Convert wide player name to UTF-8
        WideCharToMultiByte(CP_UTF8, 0, g_playerName, -1, outName, maxSize, nullptr, nullptr);
    }
    return 0;
}

int __cdecl UplayUserGetAccountId(void* outId) {
    // Write the 64-bit UplayID
    if (outId) {
        *static_cast<uint64_t*>(outId) = g_uplayId;
    }
    return 0;
}

int __cdecl UplayUserGetAccountIdUtf8(char* outId, unsigned maxSize) {
    if (outId && maxSize > 0) {
        snprintf(outId, maxSize, "%016llX", g_uplayId);
    }
    return 0;
}

int __cdecl UplayUserGetGPUScoreConfidenceLevel(int* outLevel) {
    if (outLevel) *outLevel = 3;  // High confidence
    return 0;
}

int __cdecl UplayUserGetGPUScore(int* outScore) {
    if (outScore) *outScore = 9000;  // High score
    return 0;
}

int __cdecl UplayUserGetCPUScore(int* outScore) {
    if (outScore) *outScore = 9000;
    return 0;
}

int __cdecl UplayUserClearGameSession() {
    UPLAY_LOG("UplayUserClearGameSession() -> 0");
    return 0;
}

int __cdecl UplayUserSetGameSession(void* sessionData) {
    UPLAY_LOG("UplayUserSetGameSession() -> 0");
    return 0;
}

int __cdecl UplayUserGetConsumableItems(void** outList) {
    if (outList) *outList = nullptr;
    return 0;
}

int __cdecl UplayUserConsumeItem(unsigned itemId) {
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Overlay — No-ops (custom ImGui overlay replaces this)
// ═══════════════════════════════════════════════════════════════════════

int __cdecl UplayOverlayShow(unsigned overlayType) {
    UPLAY_LOG("UplayOverlayShow(type=%u) -> no-op", overlayType);
    return 0;
}

int __cdecl UplayOverlaySetShopUrl(const char* url) {
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Product / Install
// ═══════════════════════════════════════════════════════════════════════

int __cdecl UplayProductIsInstalled(unsigned productId) {
    return 1;  // Always installed
}

int __cdecl UplayProductGetId(unsigned* outId) {
    if (outId) *outId = 720;  // ACU's Uplay product ID
    return 0;
}

int __cdecl UplayProductGetIdUtf8(char* outId, unsigned maxSize) {
    if (outId && maxSize > 0) {
        strncpy_s(outId, maxSize, "720", maxSize - 1);
    }
    return 0;
}

int __cdecl UplayInstallGetChunks(void** outChunks) {
    if (outChunks) *outChunks = nullptr;
    return 0;
}

int __cdecl UplayInstallGetLanguage(unsigned* outLang) {
    if (outLang) *outLang = 0;  // English
    return 0;
}

int __cdecl UplayInstallIsDlcInstalled(unsigned dlcId) {
    UPLAY_LOG("UplayInstallIsDlcInstalled(dlcId=%u) -> 1", dlcId);
    return 1;  // All DLC reported as installed
}

int __cdecl UplayInstallGetLanguageUtf8(char* outLang, unsigned maxSize) {
    if (outLang && maxSize > 0) {
        strncpy_s(outLang, maxSize, "en-US", maxSize - 1);
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Friends — Stubs
// ═══════════════════════════════════════════════════════════════════════

int __cdecl UplayFriendInviteToGame(const char*) { return 0; }
int __cdecl UplayFriendGetList(void** outList) { if (outList) *outList = nullptr; return 0; }
int __cdecl UplayFriendIsFriend(const char*) { return 0; }
int __cdecl UplayFriendGetNameUtf8(const char*, char* outName, unsigned maxSize) {
    if (outName && maxSize > 0) outName[0] = '\0';
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Party — Stubs
// ═══════════════════════════════════════════════════════════════════════

int __cdecl UplayPartyInit() { return 0; }
int __cdecl UplayPartyGetId(void*) { return 0; }
int __cdecl UplayPartyGetMembers(void** outList) { if (outList) *outList = nullptr; return 0; }
int __cdecl UplayPartyGetInGameMembers(void** outList) { if (outList) *outList = nullptr; return 0; }
int __cdecl UplayPartyInviteToParty(const char*) { return 0; }
int __cdecl UplayPartySetUserData(void*, unsigned) { return 0; }
int __cdecl UplayPartySendGameInvitation(const char*) { return 0; }
int __cdecl UplayPartyIsPartyLeader() { return 1; }
int __cdecl UplayPartyIsInParty() { return 0; }
int __cdecl UplayPartyMemberIsLeader(const char*) { return 0; }
int __cdecl UplayPartyRespondToGameInvitation(int) { return 0; }
int __cdecl UplayPartyShowGameInvitationDialog() { return 0; }

// ═══════════════════════════════════════════════════════════════════════
//  Miscellaneous
// ═══════════════════════════════════════════════════════════════════════

int __cdecl UplayWinIsInstalled() { return 1; }
int __cdecl UplayAchievement(unsigned) { return 0; }
int __cdecl UplayAchievementWrite(unsigned, unsigned) { return 0; }
int __cdecl UplayEventRegisterHandler(unsigned, void*) { return 0; }
int __cdecl UplayEventUnregisterHandler(unsigned) { return 0; }

// ═══════════════════════════════════════════════════════════════════════
//  Save System — Redirect to local AppData directory
// ═══════════════════════════════════════════════════════════════════════

static wchar_t g_savePath[MAX_PATH] = {};

static void EnsureSavePath() {
    if (g_savePath[0] != L'\0') return;

    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) == S_OK) {
        swprintf_s(g_savePath, L"%s\\ACU_CustomClient\\saves", appData);
        CreateDirectoryW(g_savePath, nullptr);
    }
}

int __cdecl UplaySaveGetPath(wchar_t* outPath, unsigned maxSize) {
    EnsureSavePath();
    if (outPath && maxSize > 0) {
        wcsncpy_s(outPath, maxSize, g_savePath, maxSize - 1);
    }
    return 0;
}

int __cdecl UplaySaveGetPathUtf8(char* outPath, unsigned maxSize) {
    EnsureSavePath();
    if (outPath && maxSize > 0) {
        WideCharToMultiByte(CP_UTF8, 0, g_savePath, -1, outPath, maxSize, nullptr, nullptr);
    }
    return 0;
}

int __cdecl UplaySaveOpen(const char*, void** outHandle) {
    if (outHandle) *outHandle = nullptr;
    return 0;
}

int __cdecl UplaySaveClose(void*) { return 0; }
int __cdecl UplaySaveRead(void*, void*, unsigned, unsigned* outRead) {
    if (outRead) *outRead = 0;
    return 0;
}
int __cdecl UplaySaveWrite(void*, const void*, unsigned, unsigned* outWritten) {
    if (outWritten) *outWritten = 0;
    return 0;
}
int __cdecl UplaySaveRemove(const char*) { return 0; }

} // extern "C"
