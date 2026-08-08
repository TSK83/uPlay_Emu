#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Integrity Bypass (AOB Scanner + MIC Patcher)
// ═══════════════════════════════════════════════════════════════════════
//  Dynamically scans the ACU.exe memory space for Arxan anti-tamper
//  integrity check routines using Array of Bytes (AOB) patterns with
//  wildcard support. Patches found routines with NOP/RET to disable
//  memory integrity validation, enabling safe runtime hooks.
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_INTEGRITY_BYPASS_HPP
#define ACU_INTEGRITY_BYPASS_HPP

#include <cstdint>
#include <vector>
#include <string>

namespace acu {

/// Result of a pattern scan
struct ScanResult {
    uintptr_t address = 0;
    bool      found   = false;
};

/// AOB pattern scanner — scans module memory for byte patterns with wildcards.
/// Pattern format: "48 8B 05 ?? ?? ?? ?? 48 85 C0" where ?? = wildcard byte.
class PatternScanner {
public:
    /// Scan a specific module for a byte pattern.
    /// @param moduleName   Name of the module (e.g., "ACU.exe")
    /// @param pattern      AOB pattern string with ?? wildcards
    /// @return             ScanResult with address if found
    static ScanResult Scan(const char* moduleName, const std::string& pattern);

    /// Scan a memory range for a byte pattern.
    static ScanResult ScanRange(uintptr_t start, size_t size, const std::string& pattern);

    /// Parse pattern string into bytes + mask vectors.
    static void ParsePattern(const std::string& pattern,
                             std::vector<uint8_t>& outBytes,
                             std::vector<bool>& outMask);
};

/// Integrity bypass manager — finds and disables Arxan MIC routines.
class IntegrityBypass {
public:
    /// Run the full bypass sequence. Returns the number of patches applied.
    static int Apply();

    /// Write bytes to a memory address, temporarily changing page protection.
    static bool PatchBytes(uintptr_t address, const void* data, size_t size);

    /// Fill a memory region with NOP (0x90) instructions.
    static bool NopFill(uintptr_t address, size_t size);

    /// Write a single RET (0xC3) instruction at an address.
    static bool PatchRet(uintptr_t address);
};

} // namespace acu

#endif // ACU_INTEGRITY_BYPASS_HPP
