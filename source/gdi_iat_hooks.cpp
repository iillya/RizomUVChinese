#include "rizomuv_localizer/gdi_iat_hooks.h"

#include "rizomuv_localizer/runtime_log.h"
#include "rizomuv_localizer/translation_dictionary.h"

#include <winnt.h>

#include <atomic>
#include <mutex>
#include <psapi.h>
#include "rizomuv_localizer/iat_compatibility.h"

namespace rizomuv::localizer {
namespace {

using DrawTextWFn = int (WINAPI*)(HDC, LPCWSTR, int, LPRECT, UINT);
using DrawTextExWFn = int (WINAPI*)(HDC, LPWSTR, int, LPRECT, UINT, LPDRAWTEXTPARAMS);
using TextOutWFn = BOOL (WINAPI*)(HDC, int, int, LPCWSTR, int);
using ExtTextOutWFn = BOOL (WINAPI*)(HDC, int, int, UINT, const RECT*, LPCWSTR, UINT, const INT*);
using GetTextExtentPoint32WFn = BOOL (WINAPI*)(HDC, LPCWSTR, int, LPSIZE);
using GetTextExtentExPointWFn = BOOL (WINAPI*)(HDC, LPCWSTR, int, int, LPINT, LPINT, LPSIZE);

DrawTextWFn g_drawTextW = nullptr;
DrawTextExWFn g_drawTextExW = nullptr;
TextOutWFn g_textOutW = nullptr;
ExtTextOutWFn g_extTextOutW = nullptr;
GetTextExtentPoint32WFn g_getTextExtentPoint32W = nullptr;
GetTextExtentExPointWFn g_getTextExtentExPointW = nullptr;
const TranslationDictionary* g_dictionary = nullptr;
std::atomic<unsigned long long> g_translationHits{0};
std::atomic<bool> g_countStartupHits{true};

bool ShouldLookupTranslation(LPCWSTR text, int length) {
    if (!text || length <= 0) return false;
    const bool driveAbsolutePath = length >= 3 &&
        ((text[0] >= L'A' && text[0] <= L'Z') ||
         (text[0] >= L'a' && text[0] <= L'z')) &&
        text[1] == L':' && (text[2] == L'\\' || text[2] == L'/');
    const bool uncPath = length >= 3 && text[0] == L'\\' && text[1] == L'\\';
    if (driveAbsolutePath || uncPath) return false;

    bool hasLatinLetter = false;
    for (int index = 0; index < length; ++index) {
        const wchar_t character = text[index];
        if ((character >= 0x3400 && character <= 0x9FFF) ||
            (character >= 0xF900 && character <= 0xFAFF))
            return false;
        if ((character >= L'A' && character <= L'Z') ||
            (character >= L'a' && character <= L'z'))
            hasLatinLetter = true;
    }
    return hasLatinLetter;
}

struct TextView { LPCWSTR text; int length; };

TextView Translate(LPCWSTR text, int length) noexcept {
    const TextView original{text, length};
    if (!g_dictionary || !text) return original;
    if (length == -1) length = static_cast<int>(wcsnlen_s(text, 65536));
    if (length <= 0 || length > 65535) return original;
    if (!ShouldLookupTranslation(text, length)) return original;
    const auto* translated = g_dictionary->Find(std::wstring_view(text, static_cast<size_t>(length)));
    if (!translated) return original;
    if (g_countStartupHits.load(std::memory_order_relaxed))
        g_translationHits.fetch_add(1, std::memory_order_relaxed);
    // Immutable storage: no allocation or per-thread growing buffer, and a
    // recursive call cannot invalidate an outer call's translation.
    return {translated->c_str(), static_cast<int>(translated->size())};
}

int WINAPI HookDrawTextW(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format) {
    if (format & DT_MODIFYSTRING) return g_drawTextW(dc, text, count, rect, format);
    const TextView value = Translate(text, count);
    return g_drawTextW(dc, value.text, value.length, rect, format);
}
int WINAPI HookDrawTextExW(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params) {
    if (format & DT_MODIFYSTRING) return g_drawTextExW(dc, text, count, rect, format, params);
    const TextView value = Translate(text, count);
    return g_drawTextExW(dc, const_cast<LPWSTR>(value.text), value.length, rect, format, params);
}
BOOL WINAPI HookTextOutW(HDC dc, int x, int y, LPCWSTR text, int count) {
    if (count <= 0) return g_textOutW(dc, x, y, text, count);
    const TextView value = Translate(text, count);
    return g_textOutW(dc, x, y, value.text, value.length);
}
BOOL WINAPI HookExtTextOutW(HDC dc, int x, int y, UINT options, const RECT* rect,
                            LPCWSTR text, UINT count, const INT* spacing) {
    if ((options & ETO_GLYPH_INDEX) || count > 65535)
        return g_extTextOutW(dc, x, y, options, rect, text, count, spacing);
    const TextView value = Translate(text, static_cast<int>(count));
    // Character spacing is only valid for the original glyph sequence.
    const INT* translatedSpacing = value.text == text ? spacing : nullptr;
    return g_extTextOutW(dc, x, y, options, rect, value.text,
                         static_cast<UINT>(value.length), translatedSpacing);
}
BOOL WINAPI HookGetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size) {
    if (count <= 0) return g_getTextExtentPoint32W(dc, text, count, size);
    const TextView value = Translate(text, count);
    return g_getTextExtentPoint32W(dc, value.text, value.length, size);
}
BOOL WINAPI HookGetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int maxExtent,
                                      LPINT fit, LPINT dx, LPSIZE size) {
    // Per-character arrays and fit indices belong to the original string.
    // Substituting a longer translation here can overrun the caller buffer.
    if (fit || dx || count <= 0) return g_getTextExtentExPointW(dc, text, count, maxExtent, fit, dx, size);
    const TextView value = Translate(text, count);
    return g_getTextExtentExPointW(dc, value.text, value.length, maxExtent, fit, dx, size);
}

bool PatchSlot(void** slot, void* replacement, void* expected) {
    if (!expected || *slot != expected) return false;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) return false;
    const bool changed = InterlockedCompareExchangePointer(slot, replacement, expected) == expected;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    return changed;
}

} // namespace

bool InstallGdiIatHooks(HMODULE targetModule, const TranslationDictionary* dictionary,
                        std::wstring& error) {
    static std::once_flag initialized;
    std::call_once(initialized, [dictionary] {
        g_dictionary = dictionary;
        const HMODULE user = GetModuleHandleW(L"user32.dll");
        const HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
        g_drawTextW = reinterpret_cast<DrawTextWFn>(GetProcAddress(user, "DrawTextW"));
        g_drawTextExW = reinterpret_cast<DrawTextExWFn>(GetProcAddress(user, "DrawTextExW"));
        g_textOutW = reinterpret_cast<TextOutWFn>(GetProcAddress(gdi, "TextOutW"));
        g_extTextOutW = reinterpret_cast<ExtTextOutWFn>(GetProcAddress(gdi, "ExtTextOutW"));
        g_getTextExtentPoint32W = reinterpret_cast<GetTextExtentPoint32WFn>(GetProcAddress(gdi, "GetTextExtentPoint32W"));
        g_getTextExtentExPointW = reinterpret_cast<GetTextExtentExPointWFn>(GetProcAddress(gdi, "GetTextExtentExPointW"));
    });
    struct Api { void* original; void* hook; };
    const Api apis[] = {
        {reinterpret_cast<void*>(g_drawTextW), reinterpret_cast<void*>(HookDrawTextW)},
        {reinterpret_cast<void*>(g_drawTextExW), reinterpret_cast<void*>(HookDrawTextExW)},
        {reinterpret_cast<void*>(g_textOutW), reinterpret_cast<void*>(HookTextOutW)},
        {reinterpret_cast<void*>(g_extTextOutW), reinterpret_cast<void*>(HookExtTextOutW)},
        {reinterpret_cast<void*>(g_getTextExtentPoint32W), reinterpret_cast<void*>(HookGetTextExtentPoint32W)},
        {reinterpret_cast<void*>(g_getTextExtentExPointW), reinterpret_cast<void*>(HookGetTextExtentExPointW)}
    };
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), targetModule, &info, sizeof(info))) {
        error = L"无法读取模块范围"; return false;
    }
    size_t active = 0;
    const size_t installed = VisitImportSlots(static_cast<unsigned char*>(info.lpBaseOfDll), info.SizeOfImage,
        [&](void** slot) {
            for (const auto& api : apis) {
                if (*slot == api.hook) { ++active; return false; }
                if (PatchSlot(slot, api.hook, api.original)) { ++active; return true; }
            }
            return false;
        });
    if (installed) {
        wchar_t path[32768]{};
        GetModuleFileNameW(targetModule, path, 32768);
        RuntimeLog(L"GDI 兼容检测：" + std::wstring(path) + L"，新增入口=" + std::to_wstring(installed));
    }
    if (!active) error = L"未发现可用 GDI 导入，保留原始绘制";
    return active != 0;
}

unsigned long long FinishGdiStartupDiagnostics() {
    g_countStartupHits.store(false, std::memory_order_relaxed);
    return g_translationHits.load(std::memory_order_relaxed);
}

} // namespace rizomuv::localizer
