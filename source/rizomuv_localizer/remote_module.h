#pragma once
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

namespace rizomuv::localizer {
// Thread exit codes are DWORDs, while a Win64 HMODULE is a pointer. Confirm
// the loaded module instead of interpreting a truncated LoadLibraryW result.
inline bool HasLoadedModule(HANDLE process, const std::wstring& expectedPath) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 5; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetProcessId(process));
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(10);
    }
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{snapshot};
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    std::vector<wchar_t> path(32768);
    if (!Module32FirstW(snapshot, &module)) return false;
    do {
        const DWORD count = GetModuleFileNameExW(process, module.hModule, path.data(),
                                               static_cast<DWORD>(path.size()));
        if (count && count < path.size() &&
            CompareStringOrdinal(path.data(), static_cast<int>(count), expectedPath.c_str(),
                                 static_cast<int>(expectedPath.size()), TRUE) == CSTR_EQUAL) return true;
    } while (Module32NextW(snapshot, &module));
    return false;
}
} // namespace rizomuv::localizer
