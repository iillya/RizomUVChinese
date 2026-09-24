#include "rizomuv_localizer/runtime_log.h"

#include <windows.h>

#include <cstdio>
#include <mutex>

namespace rizomuv::localizer {
namespace {
std::filesystem::path g_logPath;
std::mutex g_logMutex;
}

void InitializeRuntimeLog(const std::filesystem::path& runtimeDirectory) {
    g_logPath = runtimeDirectory / L"RizomUVChineseRuntime.log";
    FILE* probe = nullptr;
    if (_wfopen_s(&probe, g_logPath.c_str(), L"a, ccs=UTF-8") != 0 || !probe) {
        wchar_t local[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (!length || length >= 32768) { g_logPath.clear(); return; }
        const auto fallback = std::filesystem::path(local) / L"RizomUVChinese";
        std::error_code error;
        std::filesystem::create_directories(fallback, error);
        if (error) { g_logPath.clear(); return; }
        g_logPath = fallback / L"RizomUVChineseRuntime.log";
    } else {
        std::fclose(probe);
    }
    // Keep one previous log; no log IO is performed by the text drawing hooks.
    std::error_code error;
    const auto size = std::filesystem::file_size(g_logPath, error);
    if (!error && size > 256 * 1024) {
        auto previous = g_logPath;
        previous += L".previous";
        std::filesystem::remove(previous, error);
        error.clear();
        std::filesystem::rename(g_logPath, previous, error);
    }
}

void RuntimeLog(const std::wstring& message) {
    if (g_logPath.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* file = nullptr;
    if (_wfopen_s(&file, g_logPath.c_str(), L"a, ccs=UTF-8") != 0 || !file) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fwprintf(file, L"[%04u-%02u-%02u %02u:%02u:%02u] %ls\n",
                  time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                  time.wSecond, message.c_str());
    std::fclose(file);
}

} // namespace rizomuv::localizer
