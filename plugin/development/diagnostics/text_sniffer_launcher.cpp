#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::wstring Quote(const std::wstring& value) { return L"\"" + value + L"\""; }

std::filesystem::path OwnDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

bool Inject(HANDLE process, const std::wstring& dllPath) {
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;
    bool success = false;
    if (WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel, "LoadLibraryW"));
        if (loadLibrary) {
            HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
            if (thread) {
                if (WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0) {
                    DWORD result = 0;
                    success = GetExitCodeThread(thread, &result) && result != 0;
                }
                CloseHandle(thread);
            }
        }
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return success;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::filesystem::path exe = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path(L"C:\\Program Files\\Rizom Lab\\RizomUV 2025.0\\rizomuv.exe");
    const std::filesystem::path dll = OwnDirectory() / L"RizomUVTextSniffer.dll";
    if (!std::filesystem::exists(exe) || !std::filesystem::exists(dll)) {
        std::wcerr << L"Missing RizomUV executable or sniffer DLL.\nEXE: " << exe.wstring()
                   << L"\nDLL: " << dll.wstring() << L"\n";
        return 2;
    }
    std::wstring command = Quote(exe.wstring());
    for (int i = 2; i < argc; ++i) command += L" " + Quote(argv[i]);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, exe.parent_path().c_str(), &startup, &process)) {
        std::wcerr << L"CreateProcessW failed: " << GetLastError() << L"\n";
        return 3;
    }
    const bool injected = Inject(process.hProcess, dll.wstring());
    if (injected) ResumeThread(process.hThread);
    else TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!injected) {
        std::wcerr << L"DLL injection failed. RizomUV was not started.\n";
        return 4;
    }
    std::wcout << L"RizomUV started with the text sniffer. Press Ctrl+Shift+F12 to capture for 3 seconds.\n"
                  L"Log: " << (OwnDirectory() / L"RizomUV_text_sniffer.jsonl").wstring() << L"\n";
    return 0;
}
