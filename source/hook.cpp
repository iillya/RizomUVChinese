#include "rizomuv_localizer/gdi_iat_hooks.h"
#include "rizomuv_localizer/native_menu_localizer.h"
#include "rizomuv_localizer/runtime_log.h"
#include "rizomuv_localizer/translation_dictionary.h"

#include <windows.h>

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

DWORD WINAPI InitializeLocalizer(void*) {
    using namespace rizomuv::localizer;
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

    if (!InstallGdiIatHooks(GetModuleHandleW(nullptr), &g_dictionary, error)) {
        RuntimeLog(L"GDI Hook 安装失败，保持英文运行：" + error);
        return 2;
    }
    if (StartMenuBarCredits(g_runtimeModule))
        RuntimeLog(L"Toolbag 风格菜单栏署名已启用");
    else
        RuntimeLog(L"Toolbag 风格菜单栏署名启动失败");

    // Menus are created during startup. Re-scan briefly without touching other controls.
    size_t totalMenus = 0;
    for (int pass = 0; pass < 40 && g_running.load(); ++pass) {
        totalMenus += TranslateNativeMenus(GetCurrentProcessId(), g_dictionary);
        Sleep(250);
    }
    RuntimeLog(L"原生菜单翻译操作次数：" + std::to_wstring(totalMenus));
    RuntimeLog(L"GDI 翻译命中次数：" + std::to_wstring(GetGdiTranslationHitCount()));
    RuntimeLog(L"RizomUV 中文运行时初始化完成");
    return 0;
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
