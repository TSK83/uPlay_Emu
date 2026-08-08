// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — DLL Injector Implementation
// ═══════════════════════════════════════════════════════════════════════

#include "injector.hpp"
#include <cstdio>

namespace acu {

const char* InjectionResultToString(InjectionResult result) {
    switch (result) {
        case InjectionResult::Success:              return "Success";
        case InjectionResult::AllocFailed:          return "VirtualAllocEx failed";
        case InjectionResult::WriteFailed:          return "WriteProcessMemory failed";
        case InjectionResult::ThreadCreationFailed: return "CreateRemoteThread failed";
        case InjectionResult::ThreadWaitFailed:     return "WaitForSingleObject on remote thread failed";
        case InjectionResult::LoadLibraryFailed:    return "LoadLibraryW returned NULL in remote process";
        case InjectionResult::InvalidHandle:        return "Invalid process handle";
        default:                                    return "Unknown error";
    }
}

InjectionResult InjectDLL(HANDLE hProcess, const std::wstring& dllPath) {
    if (!hProcess || hProcess == INVALID_HANDLE_VALUE) {
        return InjectionResult::InvalidHandle;
    }

    // Calculate the size of the DLL path string in bytes (including null terminator)
    const size_t pathSizeBytes = (dllPath.size() + 1) * sizeof(wchar_t);

    // ── Step 1: Allocate memory in the target process for the DLL path ──
    LPVOID pRemotePath = VirtualAllocEx(
        hProcess,
        nullptr,
        pathSizeBytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!pRemotePath) {
        fprintf(stderr, "[Injector] VirtualAllocEx failed. Error: 0x%08lX\n", GetLastError());
        return InjectionResult::AllocFailed;
    }

    // ── Step 2: Write the DLL path into the allocated memory ────────────
    SIZE_T bytesWritten = 0;
    BOOL writeOk = WriteProcessMemory(
        hProcess,
        pRemotePath,
        dllPath.c_str(),
        pathSizeBytes,
        &bytesWritten
    );

    if (!writeOk || bytesWritten != pathSizeBytes) {
        fprintf(stderr, "[Injector] WriteProcessMemory failed. Error: 0x%08lX\n", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        return InjectionResult::WriteFailed;
    }

    // ── Step 3: Get the address of LoadLibraryW in kernel32.dll ─────────
    // kernel32.dll is loaded at the same base address in every process on
    // the same boot session (ASLR is per-boot, not per-process for system DLLs).
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC pLoadLibraryW = GetProcAddress(hKernel32, "LoadLibraryW");

    if (!pLoadLibraryW) {
        fprintf(stderr, "[Injector] Could not resolve LoadLibraryW. Error: 0x%08lX\n", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        return InjectionResult::ThreadCreationFailed;
    }

    // ── Step 4: Create a remote thread that calls LoadLibraryW(dllPath) ─
    HANDLE hRemoteThread = CreateRemoteThread(
        hProcess,
        nullptr,                                        // default security
        0,                                              // default stack size
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLibraryW),
        pRemotePath,                                    // argument = pointer to DLL path
        0,                                              // run immediately
        nullptr                                         // don't need thread ID
    );

    if (!hRemoteThread) {
        fprintf(stderr, "[Injector] CreateRemoteThread failed. Error: 0x%08lX\n", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        return InjectionResult::ThreadCreationFailed;
    }

    // ── Step 5: Wait for LoadLibraryW to complete ───────────────────────
    DWORD waitResult = WaitForSingleObject(hRemoteThread, 10000); // 10s timeout

    if (waitResult != WAIT_OBJECT_0) {
        fprintf(stderr, "[Injector] WaitForSingleObject failed. Wait result: %lu, Error: 0x%08lX\n",
                waitResult, GetLastError());
        CloseHandle(hRemoteThread);
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        return InjectionResult::ThreadWaitFailed;
    }

    // ── Step 6: Check LoadLibraryW return value ─────────────────────────
    DWORD exitCode = 0;
    GetExitCodeThread(hRemoteThread, &exitCode);

    CloseHandle(hRemoteThread);
    VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);

    // LoadLibraryW returns an HMODULE (non-null on success).
    // On x64, we only get the lower 32 bits via GetExitCodeThread,
    // but a non-zero value still indicates success.
    if (exitCode == 0) {
        fprintf(stderr, "[Injector] LoadLibraryW returned NULL in remote process.\n");
        return InjectionResult::LoadLibraryFailed;
    }

    return InjectionResult::Success;
}

} // namespace acu
