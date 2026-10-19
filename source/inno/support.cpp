// Validate the host and bound installation/uninstallation to its plugin directory.
// No Qt dependency, injection or shell execution; consent-gated scoped process closure.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>


#include "install_process.h"

namespace {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

bool fail(const wchar_t* text, wchar_t* out, unsigned capacity) {
    if (out && capacity) wcsncpy_s(out, capacity, text, _TRUNCATE);
    return false;
}

bool regularFile(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

bool amd64(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{};
    DWORD count = 0, signature = 0;
    IMAGE_FILE_HEADER pe{};
    LARGE_INTEGER offset{};
    if (!ReadFile(file.value, &dos, sizeof(dos), &count, nullptr) ||
        count != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos)) return false;
    offset.QuadPart = dos.e_lfanew;
    return SetFilePointerEx(file.value, offset, nullptr, FILE_BEGIN) &&
        ReadFile(file.value, &signature, sizeof(signature), &count, nullptr) && count == sizeof(signature) &&
        signature == IMAGE_NT_SIGNATURE && ReadFile(file.value, &pe, sizeof(pe), &count, nullptr) &&
        count == sizeof(pe) && pe.Machine == IMAGE_FILE_MACHINE_AMD64;
}

constexpr wchar_t kHostName[] = L"rizomuv.exe";
constexpr wchar_t kLauncher[] = L"RizomUVChineseLauncher.exe";

bool safePath(const std::wstring& root) {
    // A host directory, not a drive root, UNC share or Win32 device path.
    if (root.size() < 4 || root[1] != L':' || root[2] != L'\\' ||
        !((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')) ||
        root.find_first_of(L"\"<>|?*") != std::wstring::npos || root.find(L':', 2) != std::wstring::npos) return false;
    wchar_t full[32768]{};
    const DWORD count = GetFullPathNameW(root.c_str(), 32768, full, nullptr);
    if (!count || count >= 32768 || _wcsicmp(full, root.c_str()) != 0) return false;
    std::filesystem::path current(root);
    while (!current.empty()) {
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        const auto parent = current.parent_path();
        if (current == parent) break;
        current = parent;
    }
    return (GetFileAttributesW(root.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool safeTree(const std::filesystem::path& path, unsigned depth, unsigned& count) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) || depth > 16 || ++count > 10000) return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    std::error_code error;
    std::filesystem::directory_iterator it(path, error), end;
    if (error) return false;
    while (it != end) {
        if (!safeTree(it->path(), depth + 1, count)) return false;
        it.increment(error);
        if (error) return false;
    }
    return true;
}

std::vector<std::wstring> installProcessPaths(const std::wstring& root) {
    return {root + L"\\" + kHostName, root + L"\\ChineseLauncher\\" + kLauncher};
}
bool hostStopped(const std::wstring& root) {
    return InstallerProcess::state(installProcessPaths(root)) == 0;
}

} // namespace

extern "C" __declspec(dllexport) BOOL __stdcall CheckTargetEx(
    const wchar_t* directory, BOOL checkVersion, BOOL checkRunning, wchar_t* message, unsigned capacity) {
    if (message && capacity) message[0] = L'\0';
    try {
        const std::wstring root(directory ? directory : L"");
        if (!safePath(root)) return fail(L"请选择本地磁盘上真实存在、无目录链接的 RizomUV 目录。", message, capacity);
        unsigned count = 0;
        if (!safeTree(std::filesystem::path(root) / L"ChineseLauncher", 0, count))
            return fail(L"ChineseLauncher 内含目录链接、不可访问文件或超出检查上限，已停止操作。", message, capacity);
        if (checkVersion) {
            if (!regularFile(root + L"\\" + kHostName) || !amd64(root + L"\\" + kHostName))
                return fail(L"请选择包含 x64 rizomuv.exe 的软件目录，不要选择 ChineseLauncher 子目录。", message, capacity);
        }
        if (checkRunning && !hostStopped(root))
            return fail(L"RizomUV 或中文启动器正在运行，或无法确认进程状态。请保存工程并正常退出后重试。", message, capacity);
        return TRUE;
    } catch (...) { return fail(L"目录检查出现异常，未执行安装或卸载。", message, capacity); }
}

// Preserve installed uninstaller ABI; setup can validate separately from running state.
extern "C" __declspec(dllexport) BOOL __stdcall CheckTarget(
    const wchar_t* directory, BOOL checkVersion, wchar_t* message, unsigned capacity) {
    return CheckTargetEx(directory, checkVersion, TRUE, message, capacity);
}
extern "C" __declspec(dllexport) int __stdcall QueryInstallProcesses(const wchar_t* directory) {
    try {
        if (!directory || !safePath(directory)) return -1;
        return InstallerProcess::state(installProcessPaths(directory));
    } catch (...) { return -1; }
}
extern "C" __declspec(dllexport) BOOL __stdcall StopInstallProcesses(
    const wchar_t* directory, DWORD* stopped, wchar_t* message, unsigned capacity) {
    if (stopped) *stopped = 0;
    if (!CheckTargetEx(directory, TRUE, FALSE, message, capacity)) return FALSE;
    try {
        if (!InstallerProcess::stop(installProcessPaths(directory), stopped))
            return fail(L"无法关闭目标进程、进程重新启动或状态无法确认。尚未替换汉化文件，请重新检测。", message, capacity);
        return CheckTargetEx(directory, TRUE, TRUE, message, capacity);
    } catch (...) { return fail(L"关闭目标进程失败，尚未替换汉化文件。", message, capacity); }
}
