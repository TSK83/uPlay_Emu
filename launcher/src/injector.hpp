#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — DLL Injector
// ═══════════════════════════════════════════════════════════════════════
//  Injects DLLs into a suspended target process using the classic
//  VirtualAllocEx → WriteProcessMemory → CreateRemoteThread → LoadLibraryW
//  technique. Designed for injecting into a CREATE_SUSPENDED process
//  before the main thread is resumed.
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_INJECTOR_HPP
#define ACU_INJECTOR_HPP

#include <string>
#include <cstdint>
#include <Windows.h>

namespace acu {

enum class InjectionResult : uint8_t {
    Success = 0,
    AllocFailed,
    WriteFailed,
    ThreadCreationFailed,
    ThreadWaitFailed,
    LoadLibraryFailed,
    InvalidHandle,
};

/// Human-readable error string for InjectionResult.
const char* InjectionResultToString(InjectionResult result);

/// Inject a DLL into the target process.
/// @param hProcess      Handle to the target process (must have appropriate access rights).
/// @param dllPath       Absolute path to the DLL to inject.
/// @return              InjectionResult indicating success or the failure stage.
InjectionResult InjectDLL(HANDLE hProcess, const std::wstring& dllPath);

} // namespace acu

#endif // ACU_INJECTOR_HPP
