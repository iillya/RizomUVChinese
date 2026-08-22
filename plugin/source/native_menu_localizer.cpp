#include "rizomuv_localizer/native_menu_localizer.h"

#include "rizomuv_localizer/translation_dictionary.h"

#include <vector>

namespace rizomuv::localizer {
namespace {

size_t TranslateMenuTree(HMENU menu, const TranslationDictionary& dictionary) {
    if (!menu) return 0;
    size_t translatedCount = 0;
    const int count = GetMenuItemCount(menu);
    for (int position = 0; position < count; ++position) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE;
        wchar_t buffer[4096]{};
        info.dwTypeData = buffer;
        info.cch = static_cast<UINT>(std::size(buffer) - 1);
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &info)) continue;
        const std::wstring source(buffer, info.cch);
        const size_t shortcutOffset = source.find(L'\t');
        const std::wstring label = source.substr(0, shortcutOffset);
        if (const std::wstring* translated = dictionary.Find(label)) {
            std::wstring replacementText = *translated;
            if (shortcutOffset != std::wstring::npos)
                replacementText += source.substr(shortcutOffset);
            MENUITEMINFOW replacement{};
            replacement.cbSize = sizeof(replacement);
            replacement.fMask = MIIM_STRING;
            replacement.dwTypeData = replacementText.data();
            if (SetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &replacement))
                ++translatedCount;
        }
        if (info.hSubMenu) translatedCount += TranslateMenuTree(info.hSubMenu, dictionary);
    }
    return translatedCount;
}

struct MenuContext { DWORD pid; const TranslationDictionary* dictionary; size_t count; };

} // namespace

size_t TranslateNativeMenus(DWORD processId, const TranslationDictionary& dictionary) {
    MenuContext context{processId, &dictionary, 0};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto* context = reinterpret_cast<MenuContext*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != context->pid) return TRUE;
        if (HMENU menu = GetMenu(window)) {
            context->count += TranslateMenuTree(menu, *context->dictionary);
            DrawMenuBar(window);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.count;
}

} // namespace rizomuv::localizer
