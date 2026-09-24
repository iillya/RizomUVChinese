#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include "rizomuv_localizer/command_line.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path LauncherDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

void ShowError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"RizomUV 中文补丁", MB_OK | MB_ICONERROR);
}

LPTHREAD_START_ROUTINE ResolveRemoteLoadLibrary(DWORD processId) {
    HMODULE localKernel = GetModuleHandleW(L"kernel32.dll");
    FARPROC localFunction = localKernel
        ? GetProcAddress(localKernel, "LoadLibraryW") : nullptr;
    if (!localFunction) return nullptr;

    // LoadLibraryW can be forwarded to another system module.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(localFunction), &owner)) return nullptr;
    wchar_t ownerPath[32768]{};
    if (!GetModuleFileNameW(owner, ownerPath, 32768)) return nullptr;
    const auto ownerName = std::filesystem::path(ownerPath).filename().wstring();
    const uintptr_t offset = reinterpret_cast<uintptr_t>(localFunction) - reinterpret_cast<uintptr_t>(owner);
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
        return reinterpret_cast<LPTHREAD_START_ROUTINE>(localFunction);

    LPTHREAD_START_ROUTINE result = nullptr;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, ownerName.c_str()) == 0) {
                result = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                    reinterpret_cast<uintptr_t>(module.modBaseAddr) + offset);
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    // A newly created suspended process can temporarily reject module
    // snapshots. Both processes have the same architecture and share the
    // boot-time system DLL mapping, so retain the stable compatibility path.
    return result ? result
                  : reinterpret_cast<LPTHREAD_START_ROUTINE>(localFunction);
}

std::filesystem::path FindInstalledRizomUV() {
    const std::filesystem::path launcherDirectory = LauncherDirectory();
    const std::filesystem::path besideLauncher = launcherDirectory / L"rizomuv.exe";
    if (std::filesystem::is_regular_file(besideLauncher)) return besideLauncher;

    // 一键安装器把启动器放在 RizomUV\ChineseLauncher 中。
    const std::filesystem::path besidePlugin = launcherDirectory.parent_path() / L"rizomuv.exe";
    if (std::filesystem::is_regular_file(besidePlugin)) return besidePlugin;

    return besidePlugin; // Report the missing installation instead of guessing a version.
}

bool LoadRuntimeIntoProcess(HANDLE process, const std::filesystem::path& runtimePath,
                            std::wstring& error) {
    const std::wstring path = runtimePath.wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (!remotePath) { error = L"无法在目标进程分配路径内存"; return false; }
    bool success = false;
    bool remoteThreadCompleted = false;
    if (WriteProcessMemory(process, remotePath, path.c_str(), bytes, nullptr)) {
        auto loadLibrary = ResolveRemoteLoadLibrary(GetProcessId(process));
        if (loadLibrary) {
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath,
                                               0, nullptr);
            if (thread) {
                if (WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0) {
                    remoteThreadCompleted = true;
                    DWORD result = 0;
                    success = GetExitCodeThread(thread, &result) && result != 0;
                }
                CloseHandle(thread);
            }
        }
    }
    // If the remote thread timed out it may still be reading the DLL path.
    // The caller terminates the suspended process on failure, so leave this
    // allocation to process teardown rather than creating a use-after-free.
    if (remoteThreadCompleted) VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if (!success) error = L"目标进程未能加载中文运行时";
    return success;
}

} // namespace

int RunLauncher(int argc, wchar_t** argv) {
    const bool explicitHost = argc > 1 &&
        _wcsicmp(std::filesystem::path(argv[1]).filename().c_str(), L"rizomuv.exe") == 0;
    const auto rizomuvExecutable = std::filesystem::absolute(explicitHost
        ? std::filesystem::path(argv[1]) : FindInstalledRizomUV());
    const std::filesystem::path runtimePath = LauncherDirectory() / L"RizomUVChineseRuntime.dll";
    const std::filesystem::path dictionaryPath = LauncherDirectory() / L"dictionary_zh.json";

    if (!std::filesystem::is_regular_file(rizomuvExecutable)) {
        ShowError(L"找不到 RizomUV：" + rizomuvExecutable.wstring());
        return 2;
    }
    if (!std::filesystem::is_regular_file(runtimePath) ||
        !std::filesystem::is_regular_file(dictionaryPath)) {
        ShowError(L"中文运行时或词库不完整，请重新安装补丁。");
        return 3;
    }

    std::wstring commandLine = rizomuv::localizer::QuoteArgument(rizomuvExecutable.wstring());
    for (int index = explicitHost ? 2 : 1; index < argc; ++index)
        commandLine += L" " + rizomuv::localizer::QuoteArgument(argv[index]);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(rizomuvExecutable.c_str(), mutableCommand.data(), nullptr, nullptr,
                        FALSE, CREATE_SUSPENDED, nullptr,
                        rizomuvExecutable.parent_path().c_str(), &startup, &process)) {
        ShowError(L"启动 RizomUV 失败，错误码：" + std::to_wstring(GetLastError()));
        return 4;
    }

    struct ProcessGuard {
        PROCESS_INFORMATION info;
        bool started = false;
        ~ProcessGuard() {
            if (!started) TerminateProcess(info.hProcess, 1);
            CloseHandle(info.hThread);
            CloseHandle(info.hProcess);
        }
    } guard{process};
    BOOL wow64 = FALSE;
    if (!IsWow64Process(process.hProcess, &wow64) || wow64) {
        ShowError(L"中文补丁仅支持 Windows x64 版 RizomUV。");
        return 5;
    }
    std::wstring error;
    const bool loaded = LoadRuntimeIntoProcess(process.hProcess, runtimePath, error);
    bool started = false;
    if (loaded)
        started = ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    if (!started) {
        TerminateProcess(process.hProcess, 1);
        if (error.empty()) error = L"无法恢复 RizomUV 主线程";
        ShowError(error + L"。RizomUV 未启动。");
    }
    guard.started = started;
    return started ? 0 : 5;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    int result = 1;
    try { result = RunLauncher(argc, argv); }
    catch (...) { ShowError(L"启动器遇到异常，请检查软件路径与汉化文件是否完整。"); }
    LocalFree(argv);
    return result;
}
