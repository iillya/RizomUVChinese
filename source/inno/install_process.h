#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <utility>

// Only exact image paths may be stopped. A name is merely a scan filter.
namespace InstallerProcess {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
};
inline bool matches(HANDLE process, const std::vector<std::wstring>& paths, bool& match) {
    wchar_t image[32768]{};
    DWORD length = 32768;
    match = false;
    if (!QueryFullProcessImageNameW(process, 0, image, &length))
        return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    for (const auto& path : paths)
        if (_wcsicmp(path.c_str(), image) == 0) match = true;
    return true;
}
inline bool collect(const std::vector<std::wstring>& paths, bool terminate,
                    std::vector<Handle>& processes) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W item{};
    item.dwSize = sizeof(item);
    if (!Process32FirstW(snapshot.value, &item)) return GetLastError() == ERROR_NO_MORE_FILES;
    do {
        bool candidate = false;
        for (const auto& path : paths) {
            const auto slash = path.find_last_of(L"\\/");
            if (_wcsicmp(item.szExeFile, path.c_str() + (slash == std::wstring::npos ? 0 : slash + 1)) == 0)
                candidate = true;
        }
        if (!candidate) continue;
        Handle query(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, item.th32ProcessID));
        if (!query.value) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
            return false;
        }
        bool match = false;
        if (!matches(query.value, paths, match)) return false;
        if (!match || WaitForSingleObject(query.value, 0) == WAIT_OBJECT_0) continue;
        if (!terminate) { processes.emplace_back(std::move(query)); continue; }
        if (item.th32ProcessID == GetCurrentProcessId()) return false;
        Handle target(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                  FALSE, item.th32ProcessID));
        if (!target.value) {
            if (WaitForSingleObject(query.value, 0) == WAIT_OBJECT_0) continue;
            return false;
        }
        // Recheck the terminating handle, including PID reuse between opens.
        if (!matches(target.value, paths, match)) return false;
        if (match && WaitForSingleObject(target.value, 0) != WAIT_OBJECT_0)
            processes.emplace_back(std::move(target));
    } while (Process32NextW(snapshot.value, &item));
    return GetLastError() == ERROR_NO_MORE_FILES;
}
// -1 unknown, 0 stopped, 1 running. Unknown must never be presented as running.
inline int state(const std::vector<std::wstring>& paths) {
    std::vector<Handle> processes;
    if (!collect(paths, false, processes)) return -1;
    return processes.empty() ? 0 : 1;
}
inline bool stop(const std::vector<std::wstring>& paths, DWORD* stopped) {
    if (stopped) *stopped = 0;
    std::vector<Handle> processes;
    // Acquire and verify every handle before terminating any target.
    if (!collect(paths, true, processes)) return false;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    for (const auto& process : processes) {
        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) continue;
        if (!TerminateProcess(process.value, ERROR_CANCELLED) &&
            WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0) return false;
        if (stopped) ++*stopped;
    }
    for (const auto& process : processes) {
        const auto now = GetTickCount64();
        if (WaitForSingleObject(process.value, now < deadline ? static_cast<DWORD>(deadline - now) : 0)
            != WAIT_OBJECT_0) return false;
    }
    return state(paths) == 0;
}
}
