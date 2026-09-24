// Validate the host and bound installation/uninstallation to its plugin directory.
// No Qt dependency, process injection, process termination or shell execution.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>


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
    if (root.size() < 4 || root.size() > 180 || root[1] != L':' || root[2] != L'\\' ||
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

bool hostStopped(const std::wstring& root) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W item{};
    item.dwSize = sizeof(item);
    if (!Process32FirstW(snapshot.value, &item)) return false;
    do {
        if (_wcsicmp(item.szExeFile, kHostName) != 0 &&
            _wcsicmp(item.szExeFile, kLauncher) != 0) continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.th32ProcessID));
        if (!process.value) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) continue; // Exited meanwhile.
            return false;
        }
        wchar_t path[32768]{};
        DWORD length = 32768;
        if (!QueryFullProcessImageNameW(process.value, 0, path, &length)) return false;
        if (_wcsicmp(path, (root + L"\\" + kHostName).c_str()) == 0 ||
            _wcsicmp(path, (root + L"\\ChineseLauncher\\" + kLauncher).c_str()) == 0) return false;
    } while (Process32NextW(snapshot.value, &item));
    return GetLastError() == ERROR_NO_MORE_FILES;
}

} // namespace

extern "C" __declspec(dllexport) BOOL __stdcall CheckTarget(
    const wchar_t* directory, BOOL checkVersion, wchar_t* message, unsigned capacity) {
    if (message && capacity) message[0] = L'\0';
    try {
        const std::wstring root(directory ? directory : L"");
        if (!safePath(root)) return fail(L"请选择本地、无目录链接的 RizomUV 目录；路径长度上限为 180 字符。", message, capacity);
        unsigned count = 0;
        if (!safeTree(std::filesystem::path(root) / L"ChineseLauncher", 0, count))
            return fail(L"ChineseLauncher 内含目录链接、不可访问文件或超出检查上限，已停止操作。", message, capacity);
        if (checkVersion) {
            if (!regularFile(root + L"\\" + kHostName) || !amd64(root + L"\\" + kHostName))
                return fail(L"请选择包含 x64 rizomuv.exe 的软件目录，不要选择 ChineseLauncher 子目录。", message, capacity);
        }
        if (!hostStopped(root))
            return fail(L"RizomUV 或中文启动器正在运行，或无法确认进程状态。请保存工程并正常退出后重试。", message, capacity);
        return TRUE;
    } catch (...) { return fail(L"目录检查出现异常，未执行安装或卸载。", message, capacity); }
}
