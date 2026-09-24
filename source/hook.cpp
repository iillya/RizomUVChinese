#include "rizomuv_localizer/gdi_iat_hooks.h"
#include "rizomuv_localizer/native_menu_localizer.h"
#include "rizomuv_localizer/runtime_log.h"
#include "rizomuv_localizer/translation_dictionary.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace {

HMODULE g_runtimeModule = nullptr;
std::atomic<bool> g_running{true};
rizomuv::localizer::TranslationDictionary g_dictionary;

std::filesystem::path RuntimeDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(g_runtimeModule, path.data(),
                                             static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

void RefreshApplicationModules(const std::wstring& root) {
    DWORD needed = 0;
    thread_local std::vector<HMODULE> modules(256);
    if (!EnumProcessModules(GetCurrentProcess(), modules.data(),
                            static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) return;
    if (needed > modules.size() * sizeof(HMODULE)) {
        modules.resize(needed / sizeof(HMODULE));
        if (!EnumProcessModules(GetCurrentProcess(), modules.data(), needed, &needed)) return;
    }
    const size_t count = (std::min)(modules.size(), static_cast<size_t>(needed / sizeof(HMODULE)));
    // The API writes the returned path and terminator. Do not clear 64 KiB
    // for every module on every scan.
    wchar_t path[32768];
    auto isApplicationModule = [&](HMODULE module) {
        const DWORD length = GetModuleFileNameW(module, path, 32768);
        return length > root.size() && length < 32768 &&
            CompareStringOrdinal(path, static_cast<int>(root.size()), root.c_str(),
                                 static_cast<int>(root.size()), TRUE) == CSTR_EQUAL;
    };
    for (size_t i = 0; i < count; ++i) {
        if (modules[i] == g_runtimeModule || !isApplicationModule(modules[i])) continue;
        struct ModuleReference {
            HMODULE value = nullptr;
            ~ModuleReference() { if (value) FreeLibrary(value); }
        } held;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               reinterpret_cast<LPCWSTR>(modules[i]), &held.value)) continue;
        // The module may unload/reload at the same address between enumeration
        // and acquisition. Recheck while holding its reference before touching IAT.
        if (isApplicationModule(held.value)) {
            std::wstring error;
            rizomuv::localizer::InstallGdiIatHooks(held.value, &g_dictionary, error);
        }
    }
}

DWORD InitializeLocalizerImpl() {
    using namespace rizomuv::localizer;
    // One deadline for the entire startup window, including the menu phase.
    const ULONGLONG scanDeadline = GetTickCount64() + 120000;
    const std::filesystem::path directory = RuntimeDirectory();
    InitializeRuntimeLog(directory);
    RuntimeLog(L"RizomUV 中文运行时开始初始化");

    std::wstring error;
    const std::filesystem::path dictionaryPath = directory / L"dictionary_zh.json";
    if (!g_dictionary.Load(dictionaryPath, error)) {
        RuntimeLog(L"词库加载失败，保持英文运行：" + error);
        return 1;
    }
    RuntimeLog(L"已加载词条：" + std::to_wstring(g_dictionary.Size()));

    // IAT callbacks and dictionary remain valid after the startup worker exits.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(g_runtimeModule), &pinned)) return 3;
    wchar_t hostPath[32768]{};
    GetModuleFileNameW(nullptr, hostPath, 32768);
    const std::wstring hostRoot = std::filesystem::path(hostPath).parent_path().wstring() + L"\\";

    if (!InstallGdiIatHooks(GetModuleHandleW(nullptr), &g_dictionary, error)) {
        RuntimeLog(L"主程序 GDI 入口不可用，继续检测应用 DLL 和原生菜单：" + error);
    }
    if (StartMenuBarCredits(g_runtimeModule))
        RuntimeLog(L"菜单栏署名线程已启动");
    else
        RuntimeLog(L"菜单栏署名启动失败");

    // Menus are created during startup. Re-scan briefly without touching other controls.
    size_t totalMenus = 0;
    for (int pass = 0; pass < 40 && g_running.load() && GetTickCount64() < scanDeadline; ++pass) {
        if (pass % 8 == 0) RefreshApplicationModules(hostRoot);
        totalMenus += TranslateNativeMenus(GetCurrentProcessId(), g_dictionary);
        Sleep(250);
    }
    RuntimeLog(L"原生菜单翻译操作次数：" + std::to_wstring(totalMenus));
    RuntimeLog(L"GDI 翻译命中次数：" + std::to_wstring(FinishGdiStartupDiagnostics()));
    RuntimeLog(L"RizomUV 中文运行时初始化完成");
    // Cover late startup modules/delay imports only within the first 120 seconds.
    // Only scan application-directory modules; do not patch Windows modules.
    LARGE_INTEGER frequency{}, accumulated{};
    QueryPerformanceFrequency(&frequency);
    unsigned samples = 0;
    while (g_running.load()) {
        const ULONGLONG now = GetTickCount64();
        if (now >= scanDeadline) break;
        Sleep(static_cast<DWORD>((std::min)(2000ULL, scanDeadline - now)));
        // Check after waking so no new scan starts at/after the deadline.
        if (!g_running.load() || GetTickCount64() >= scanDeadline) break;
        LARGE_INTEGER begin{}, end{};
        QueryPerformanceCounter(&begin);
        RefreshApplicationModules(hostRoot);
        QueryPerformanceCounter(&end);
        if (samples < 5 && frequency.QuadPart > 0) {
            accumulated.QuadPart += end.QuadPart - begin.QuadPart;
            if (++samples == 5)
                RuntimeLog(L"后台模块检测平均耗时（前 5 次，微秒）：" +
                    std::to_wstring(accumulated.QuadPart * 1000000 / frequency.QuadPart / samples));
        }
    }
    if (g_running.load())
        RuntimeLog(L"启动后 120 秒模块扫描窗口已结束，扫描线程退出；已接入的汉化继续生效");
    return 0;
}

DWORD WINAPI InitializeLocalizer(void*) {
    try { return InitializeLocalizerImpl(); }
    catch (...) {
        OutputDebugStringW(L"RizomUV Chinese: initialization/refresh failed.\n");
        return 4;
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_runtimeModule = instance;
        DisableThreadLibraryCalls(instance);
        if (HANDLE thread = CreateThread(nullptr, 0, InitializeLocalizer, nullptr, 0, nullptr))
            CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running.store(false);
    }
    return TRUE;
}
