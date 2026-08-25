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
    std::error_code error;
    std::filesystem::create_directories(runtimeDirectory, error);
    if (error) {
        g_logPath.clear();
        return;
    }
    g_logPath = runtimeDirectory / L"RizomUVChineseRuntime.log";
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
