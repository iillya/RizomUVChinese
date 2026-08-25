#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path LauncherDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

std::wstring Quote(const std::wstring& value) { return L"\"" + value + L"\""; }

LPTHREAD_START_ROUTINE ResolveRemoteLoadLibrary(DWORD processId) {
    HMODULE localKernel = GetModuleHandleW(L"kernel32.dll");
    FARPROC localFunction = localKernel
        ? GetProcAddress(localKernel, "LoadLibraryW") : nullptr;
    if (!localFunction) return nullptr;

    const uintptr_t offset = reinterpret_cast<uintptr_t>(localFunction) -
                             reinterpret_cast<uintptr_t>(localKernel);
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE)
        return reinterpret_cast<LPTHREAD_START_ROUTINE>(localFunction);

    LPTHREAD_START_ROUTINE result = nullptr;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, L"kernel32.dll") == 0) {
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

    return L"C:\\Program Files\\Rizom Lab\\RizomUV 2025.0\\rizomuv.exe";
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
    const std::filesystem::path rizomuvExecutable = argc > 1
        ? std::filesystem::path(argv[1])
        : FindInstalledRizomUV();
    const std::filesystem::path runtimePath = LauncherDirectory() / L"RizomUVChineseRuntime.dll";
    const std::filesystem::path dictionaryPath = LauncherDirectory() / L"dictionary_zh.json";

    if (!std::filesystem::is_regular_file(rizomuvExecutable)) {
        std::wcerr << L"找不到 RizomUV：" << rizomuvExecutable.wstring() << L"\n";
        return 2;
    }
    if (!std::filesystem::is_regular_file(runtimePath) ||
        !std::filesystem::is_regular_file(dictionaryPath)) {
        std::wcerr << L"中文运行时或词库不完整，请重新构建/安装补丁。\n";
        return 3;
    }

    std::wstring commandLine = Quote(rizomuvExecutable.wstring());
    for (int index = 2; index < argc; ++index) commandLine += L" " + Quote(argv[index]);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(rizomuvExecutable.c_str(), mutableCommand.data(), nullptr, nullptr,
                        FALSE, CREATE_SUSPENDED, nullptr,
                        rizomuvExecutable.parent_path().c_str(), &startup, &process)) {
        std::wcerr << L"启动 RizomUV 失败，错误码：" << GetLastError() << L"\n";
        return 4;
    }

    std::wstring error;
    const bool loaded = LoadRuntimeIntoProcess(process.hProcess, runtimePath, error);
    bool started = false;
    if (loaded)
        started = ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    if (!started) {
        TerminateProcess(process.hProcess, 1);
        if (error.empty()) error = L"无法恢复 RizomUV 主线程";
        std::wcerr << error << L"。为避免不完整状态，RizomUV 未启动。\n";
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return started ? 0 : 5;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    const int result = RunLauncher(argc, argv);
    LocalFree(argv);
    return result;
}
