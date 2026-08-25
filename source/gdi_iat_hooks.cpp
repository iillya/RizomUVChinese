#include "rizomuv_localizer/gdi_iat_hooks.h"

#include "rizomuv_localizer/runtime_log.h"
#include "rizomuv_localizer/translation_dictionary.h"

#include <winnt.h>

#include <atomic>

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
thread_local std::wstring g_translatedText;
std::atomic<unsigned long long> g_translationHits{0};

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

TextView Translate(LPCWSTR text, int length) {
    if (!g_dictionary || !text) return {text, length};
    if (length < 0) length = static_cast<int>(wcsnlen_s(text, 65536));
    if (length <= 0 || length > 65535) return {text, length};
    if (!ShouldLookupTranslation(text, length)) return {text, length};
    const std::wstring source(text, text + length);
    const std::wstring* translated = g_dictionary->Find(source);
    if (!translated) return {text, length};
    g_translationHits.fetch_add(1, std::memory_order_relaxed);
    g_translatedText = *translated;
    return {g_translatedText.c_str(), static_cast<int>(g_translatedText.size())};
}

int WINAPI HookDrawTextW(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format) {
    const TextView value = Translate(text, count);
    return g_drawTextW(dc, value.text, value.length, rect, format);
}
int WINAPI HookDrawTextExW(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params) {
    const TextView value = Translate(text, count);
    return g_drawTextExW(dc, const_cast<LPWSTR>(value.text), value.length, rect, format, params);
}
BOOL WINAPI HookTextOutW(HDC dc, int x, int y, LPCWSTR text, int count) {
    const TextView value = Translate(text, count);
    return g_textOutW(dc, x, y, value.text, value.length);
}
BOOL WINAPI HookExtTextOutW(HDC dc, int x, int y, UINT options, const RECT* rect,
                            LPCWSTR text, UINT count, const INT* spacing) {
    const TextView value = Translate(text, static_cast<int>(count));
    // Character spacing is only valid for the original glyph sequence.
    const INT* translatedSpacing = value.text == text ? spacing : nullptr;
    return g_extTextOutW(dc, x, y, options, rect, value.text,
                         static_cast<UINT>(value.length), translatedSpacing);
}
BOOL WINAPI HookGetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size) {
    const TextView value = Translate(text, count);
    return g_getTextExtentPoint32W(dc, value.text, value.length, size);
}
BOOL WINAPI HookGetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int maxExtent,
                                      LPINT fit, LPINT dx, LPSIZE size) {
    const TextView value = Translate(text, count);
    return g_getTextExtentExPointW(dc, value.text, value.length, maxExtent, fit, dx, size);
}

bool PatchSlot(void** slot, void* replacement, void** original) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) return false;
    if (!*original) *original = *slot;
    *slot = replacement;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

} // namespace

bool InstallGdiIatHooks(HMODULE targetModule, const TranslationDictionary* dictionary,
                        std::wstring& error) {
    g_dictionary = dictionary;
    auto* base = reinterpret_cast<unsigned char*>(targetModule);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) { error = L"目标模块不是有效 PE"; return false; }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { error = L"目标模块 PE 头无效"; return false; }
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) { error = L"目标模块没有导入表"; return false; }

    size_t installed = 0;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* dll = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(dll, "user32.dll") != 0 && _stricmp(dll, "gdi32.dll") != 0) continue;
        auto* names = descriptor->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk)
            : reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const char* name = reinterpret_cast<const char*>(import->Name);
            void** slot = reinterpret_cast<void**>(&slots->u1.Function);
            bool patched = false;
            if (strcmp(name, "DrawTextW") == 0) patched = PatchSlot(slot, reinterpret_cast<void*>(HookDrawTextW), reinterpret_cast<void**>(&g_drawTextW));
            else if (strcmp(name, "DrawTextExW") == 0) patched = PatchSlot(slot, reinterpret_cast<void*>(HookDrawTextExW), reinterpret_cast<void**>(&g_drawTextExW));
            else if (strcmp(name, "TextOutW") == 0) patched = PatchSlot(slot, reinterpret_cast<void*>(HookTextOutW), reinterpret_cast<void**>(&g_textOutW));
            else if (strcmp(name, "ExtTextOutW") == 0) patched = PatchSlot(slot, reinterpret_cast<void*>(HookExtTextOutW), reinterpret_cast<void**>(&g_extTextOutW));
            else if (strcmp(name, "GetTextExtentPoint32W") == 0) patched = PatchSlot(slot, reinterpret_cast<void*>(HookGetTextExtentPoint32W), reinterpret_cast<void**>(&g_getTextExtentPoint32W));
            else if (strcmp(name, "GetTextExtentExPointW") == 0) patched = PatchSlot(slot, reinterpret_cast<void*>(HookGetTextExtentExPointW), reinterpret_cast<void**>(&g_getTextExtentExPointW));
            if (patched) ++installed;
        }
    }
    if (!g_getTextExtentPoint32W || !g_extTextOutW) {
        error = L"缺少已验证的 GDI 测量或绘制入口";
        return false;
    }
    RuntimeLog(L"已安装 GDI IAT Hook：" + std::to_wstring(installed) + L" 个入口");
    return true;
}

unsigned long long GetGdiTranslationHitCount() {
    return g_translationHits.load(std::memory_order_relaxed);
}

} // namespace rizomuv::localizer
