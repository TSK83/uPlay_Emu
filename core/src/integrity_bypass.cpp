// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Integrity Bypass Implementation
// ═══════════════════════════════════════════════════════════════════════

#include "integrity_bypass.hpp"
#include "logger.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

namespace acu {

// ═══════════════════════════════════════════════════════════════════════
//  Pattern Scanner
// ═══════════════════════════════════════════════════════════════════════

void PatternScanner::ParsePattern(const std::string& pattern,
                                  std::vector<uint8_t>& outBytes,
                                  std::vector<bool>& outMask) {
    outBytes.clear();
    outMask.clear();

    std::istringstream stream(pattern);
    std::string token;

    while (stream >> token) {
        if (token == "??" || token == "?") {
            outBytes.push_back(0x00);
            outMask.push_back(false);  // false = wildcard (don't compare)
        } else {
            outBytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
            outMask.push_back(true);   // true = must match
        }
    }
}

ScanResult PatternScanner::ScanRange(uintptr_t start, size_t size,
                                     const std::string& pattern) {
    std::vector<uint8_t> patternBytes;
    std::vector<bool>    patternMask;
    ParsePattern(pattern, patternBytes, patternMask);

    if (patternBytes.empty()) {
        return { 0, false };
    }

    const size_t patternLen = patternBytes.size();
    const uint8_t* scanBase = reinterpret_cast<const uint8_t*>(start);

    for (size_t i = 0; i <= size - patternLen; ++i) {
        bool match = true;
        for (size_t j = 0; j < patternLen; ++j) {
            if (patternMask[j] && scanBase[i + j] != patternBytes[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            return { start + i, true };
        }
    }

    return { 0, false };
}

ScanResult PatternScanner::Scan(const char* moduleName, const std::string& pattern) {
    // Get module handle
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) {
        LOG_HOOK("PatternScanner: Module '%s' not loaded", moduleName);
        return { 0, false };
    }

    // Get module info for base address and size
    MODULEINFO modInfo = {};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        LOG_HOOK("PatternScanner: Failed to get module info for '%s'", moduleName);
        return { 0, false };
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    size_t    size = modInfo.SizeOfImage;

    LOG_HOOK("PatternScanner: Scanning '%s' (base=0x%llX, size=0x%llX)",
             moduleName, base, size);

    // Walk memory pages and only scan readable regions
    uintptr_t current = base;
    uintptr_t end     = base + size;

    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        // Only scan committed, readable pages (skip guard/noaccess pages)
        if (mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {

            size_t regionSize = mbi.RegionSize;
            if (current + regionSize > end) {
                regionSize = end - current;
            }

            auto result = ScanRange(current, regionSize, pattern);
            if (result.found) {
                LOG_HOOK("PatternScanner: Found pattern at 0x%llX (offset: 0x%llX)",
                         result.address, result.address - base);
                return result;
            }
        }

        current += mbi.RegionSize;
    }

    LOG_HOOK("PatternScanner: Pattern not found in '%s'", moduleName);
    return { 0, false };
}

// ═══════════════════════════════════════════════════════════════════════
//  Memory Patching Utilities
// ═══════════════════════════════════════════════════════════════════════

bool IntegrityBypass::PatchBytes(uintptr_t address, const void* data, size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LOG_HOOK("PatchBytes: VirtualProtect failed at 0x%llX (error: 0x%08lX)",
                 address, GetLastError());
        return false;
    }

    memcpy(reinterpret_cast<void*>(address), data, size);

    DWORD dummy = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &dummy);

    // Flush instruction cache to ensure CPU sees the new bytes
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), size);

    return true;
}

bool IntegrityBypass::NopFill(uintptr_t address, size_t size) {
    std::vector<uint8_t> nops(size, 0x90);
    return PatchBytes(address, nops.data(), size);
}

bool IntegrityBypass::PatchRet(uintptr_t address) {
    uint8_t ret = 0xC3;
    return PatchBytes(address, &ret, 1);
}

// ═══════════════════════════════════════════════════════════════════════
//  MIC Bypass — Arxan Anti-Tamper Integrity Check Patterns
// ═══════════════════════════════════════════════════════════════════════
//
//  These AOB patterns target known Arxan/MIC routines in ACU.exe.
//  The patterns use ?? wildcards for ASLR-affected offsets.
//
//  TODO: RE-REQUIRED — Verify these patterns against your specific
//        ACU.exe version (v1.5.0 / v1.5.1). Use IDA Pro or x64dbg
//        to confirm the byte sequences. These are research-based
//        estimates and may need adjustment.
// ═══════════════════════════════════════════════════════════════════════

struct MICPattern {
    const char* name;
    const char* pattern;
    int         patchOffset;  // Offset from found address to patch site
    int         patchSize;    // Number of bytes to NOP or patch
    bool        useRet;       // true = patch with RET, false = NOP fill
};

static const MICPattern g_micPatterns[] = {
    // Pattern 1: Main integrity check loop — hash comparison routine
    // This pattern targets the CRC32/hash validation loop that scans .text pages.
    // When found, we patch the comparison jump to always skip the violation handler.
    {
        "MIC Hash Validation Loop",
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8B ?? ?? ?? ?? ?? 48 33 C4",
        0, 1, true    // Patch with RET at function entry
    },

    // Pattern 2: Integrity violation handler — called when a page mismatch is detected.
    // Patching this with RET prevents the engine from acting on integrity violations.
    {
        "MIC Violation Handler",
        "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ??",
        0, 1, true
    },

    // Pattern 3: Thread-based integrity scanner — spawns a background thread
    // that periodically re-scans code pages. Killing this at entry prevents
    // deferred integrity failures after hooks are installed.
    {
        "MIC Background Scanner Thread",
        "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 33 F6 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??",
        0, 1, true
    },

    // Pattern 4: Anti-debug timing check — detects debugger attachment via
    // RDTSC timing gaps. Not strictly MIC, but disrupts RE workflows.
    {
        "Anti-Debug Timing Check",
        "0F 31 48 C1 E2 20 48 0B C2 48 89 ?? ?? ?? 0F 31 48 C1 E2 20 48 0B C2",
        0, 6, false  // NOP the first RDTSC instruction (6 bytes including prefix)
    },
};

int IntegrityBypass::Apply() {
    LOG_HOOK("═══════════════════════════════════════════════════════");
    LOG_HOOK("  Integrity Bypass — Scanning for Arxan MIC routines");
    LOG_HOOK("═══════════════════════════════════════════════════════");

    int patchCount = 0;

    // Wait for ACU.exe to fully unpack (Arxan unpacks code at runtime)
    // The engine's main module may not be fully decrypted immediately.
    LOG_HOOK("Waiting for ACU.exe to finish unpacking...");

    for (int attempt = 0; attempt < 30; ++attempt) {
        HMODULE hACU = GetModuleHandleA("ACU.exe");
        if (hACU) {
            // Check if the .text section appears to be decrypted
            // by looking for a known engine string or valid code pattern
            MODULEINFO modInfo = {};
            GetModuleInformation(GetCurrentProcess(), hACU, &modInfo, sizeof(modInfo));

            // Simple heuristic: check if the entry point region contains valid x86-64 code
            uintptr_t entry = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll) + 0x1000;
            MEMORY_BASIC_INFORMATION mbi = {};
            VirtualQuery(reinterpret_cast<LPCVOID>(entry), &mbi, sizeof(mbi));

            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_EXECUTE_READ)) {
                LOG_HOOK("ACU.exe appears to be unpacked (attempt %d)", attempt + 1);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Scan and patch each MIC pattern
    for (const auto& mic : g_micPatterns) {
        LOG_HOOK("Scanning for: %s", mic.name);

        auto result = PatternScanner::Scan("ACU.exe", mic.pattern);
        if (!result.found) {
            LOG_HOOK("  [SKIP] Pattern not found (may not exist in this version)");
            continue;
        }

        uintptr_t patchAddr = result.address + mic.patchOffset;

        bool ok = false;
        if (mic.useRet) {
            ok = PatchRet(patchAddr);
            LOG_HOOK("  [%s] Patched with RET at 0x%llX", ok ? "OK" : "FAIL", patchAddr);
        } else {
            ok = NopFill(patchAddr, mic.patchSize);
            LOG_HOOK("  [%s] NOP-filled %d bytes at 0x%llX", ok ? "OK" : "FAIL",
                     mic.patchSize, patchAddr);
        }

        if (ok) patchCount++;
    }

    LOG_HOOK("═══════════════════════════════════════════════════════");
    LOG_HOOK("  Integrity Bypass complete: %d/%zu patches applied",
             patchCount, std::size(g_micPatterns));
    LOG_HOOK("═══════════════════════════════════════════════════════");

    return patchCount;
}

} // namespace acu
