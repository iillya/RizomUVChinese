#include "rizomuv_localizer/gdi_iat_hooks.h"

#include "rizomuv_localizer/runtime_log.h"
#include "rizomuv_localizer/translation_dictionary.h"

#include <winnt.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

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
std::filesystem::path g_missingTextPath;
std::mutex g_missingTextMutex;
std::atomic<bool> g_missingTextDirty{false};
std::atomic<bool> g_missingTextCaptureStarted{false};

struct MissingTextEntry {
    std::wstring text;
    unsigned long long count = 0;
    unsigned int apiMask = 0;
};

std::unordered_map<std::wstring, MissingTextEntry> g_missingTexts;

unsigned int ApiMask(const wchar_t* api) {
    if (wcscmp(api, L"DrawTextExW") == 0) return 1u;
    if (wcscmp(api, L"DrawTextW") == 0) return 2u;
    if (wcscmp(api, L"TextOutW") == 0) return 4u;
    if (wcscmp(api, L"ExtTextOutW") == 0) return 8u;
    if (wcscmp(api, L"GetTextExtentPoint32W") == 0) return 16u;
    if (wcscmp(api, L"GetTextExtentExPointW") == 0) return 32u;
    return 0;
}

bool IsMissingTextCandidate(const std::wstring& text) {
    if (text.size() < 2 || text.size() > 8192) return false;
    bool hasLatinLetter = false;
    for (wchar_t character : text) {
        if ((character >= L'A' && character <= L'Z') ||
            (character >= L'a' && character <= L'z')) hasLatinLetter = true;
        if (character == L'\0') return false;
    }
    return hasLatinLetter;
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), size, nullptr, nullptr);
    return output;
}

std::string JsonEscape(const std::wstring& value) {
    const std::string utf8 = Utf8(value);
    std::string output;
    output.reserve(utf8.size() + 16);
    for (unsigned char character : utf8) {
        switch (character) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                char escaped[7]{};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", character);
                output += escaped;
            } else output.push_back(static_cast<char>(character));
        }
    }
    return output;
}

void CaptureMissingText(const wchar_t* api, const std::wstring& source) {
    if (!g_missingTextCaptureStarted.load(std::memory_order_relaxed) ||
        !IsMissingTextCandidate(source)) return;
    // This runs in RizomUV's drawing path: never stall rendering for telemetry.
    std::unique_lock<std::mutex> lock(g_missingTextMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    auto [position, inserted] = g_missingTexts.try_emplace(source);
    MissingTextEntry& entry = position->second;
    if (inserted) entry.text = source;
    ++entry.count;
    entry.apiMask |= ApiMask(api);
    g_missingTextDirty.store(true, std::memory_order_release);
}

std::string ApiJson(unsigned int mask) {
    static constexpr struct { unsigned int bit; const char* name; } apis[] = {
        {1u, "DrawTextExW"}, {2u, "DrawTextW"}, {4u, "TextOutW"},
        {8u, "ExtTextOutW"}, {16u, "GetTextExtentPoint32W"},
        {32u, "GetTextExtentExPointW"},
    };
    std::string output = "[";
    bool first = true;
    for (const auto& api : apis) {
        if (!(mask & api.bit)) continue;
        if (!first) output += ',';
        output += '"';
        output += api.name;
        output += '"';
        first = false;
    }
    return output + ']';
}

bool WriteMissingTextSnapshot() {
    if (g_missingTextPath.empty()) return false;
    std::vector<MissingTextEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_missingTextMutex);
        snapshot.reserve(g_missingTexts.size());
        for (const auto& item : g_missingTexts) snapshot.push_back(item.second);
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
        return left.text < right.text;
    });
    const std::filesystem::path temporary = g_missingTextPath.wstring() + L".tmp";
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0 || !file) return false;
    bool written = true;
    for (const auto& entry : snapshot) {
        const std::string line = "{\"text\":\"" + JsonEscape(entry.text) +
            "\",\"count\":" + std::to_string(entry.count) +
            ",\"apis\":" + ApiJson(entry.apiMask) + "}\n";
        if (std::fwrite(line.data(), 1, line.size(), file) != line.size()) {
            written = false;
            break;
        }
    }
    if (std::fclose(file) != 0) written = false;
    if (!written || !MoveFileExW(temporary.c_str(), g_missingTextPath.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

DWORD WINAPI MissingTextWriterThread(void*) {
    for (;;) {
        Sleep(3000);
        if (g_missingTextDirty.exchange(false, std::memory_order_acq_rel) &&
            !WriteMissingTextSnapshot())
            g_missingTextDirty.store(true, std::memory_order_release);
    }
}

struct TextView { LPCWSTR text; int length; };

TextView Translate(const wchar_t* api, LPCWSTR text, int length) {
    if (!g_dictionary || !text) return {text, length};
    if (length < 0) length = static_cast<int>(wcsnlen_s(text, 65536));
    if (length <= 0 || length > 65535) return {text, length};
    const std::wstring source(text, text + length);
    const std::wstring* translated = g_dictionary->Find(source);
    if (!translated) {
        CaptureMissingText(api, source);
        return {text, length};
    }
    g_translationHits.fetch_add(1, std::memory_order_relaxed);
    g_translatedText = *translated;
    return {g_translatedText.c_str(), static_cast<int>(g_translatedText.size())};
}

int WINAPI HookDrawTextW(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format) {
    const TextView value = Translate(L"DrawTextW", text, count);
    return g_drawTextW(dc, value.text, value.length, rect, format);
}
int WINAPI HookDrawTextExW(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params) {
    const TextView value = Translate(L"DrawTextExW", text, count);
    return g_drawTextExW(dc, const_cast<LPWSTR>(value.text), value.length, rect, format, params);
}
BOOL WINAPI HookTextOutW(HDC dc, int x, int y, LPCWSTR text, int count) {
    const TextView value = Translate(L"TextOutW", text, count);
    return g_textOutW(dc, x, y, value.text, value.length);
}
BOOL WINAPI HookExtTextOutW(HDC dc, int x, int y, UINT options, const RECT* rect,
                            LPCWSTR text, UINT count, const INT* spacing) {
    const TextView value = Translate(L"ExtTextOutW", text, static_cast<int>(count));
    // Character spacing is only valid for the original glyph sequence.
    const INT* translatedSpacing = value.text == text ? spacing : nullptr;
    return g_extTextOutW(dc, x, y, options, rect, value.text,
                         static_cast<UINT>(value.length), translatedSpacing);
}
BOOL WINAPI HookGetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size) {
    const TextView value = Translate(L"GetTextExtentPoint32W", text, count);
    return g_getTextExtentPoint32W(dc, value.text, value.length, size);
}
BOOL WINAPI HookGetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int maxExtent,
                                      LPINT fit, LPINT dx, LPSIZE size) {
    const TextView value = Translate(L"GetTextExtentExPointW", text, count);
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

bool StartMissingTextCapture(const std::filesystem::path& outputDirectory,
                             std::wstring& error) {
    bool expected = false;
    if (!g_missingTextCaptureStarted.compare_exchange_strong(expected, true)) return true;
    std::error_code filesystemError;
    std::filesystem::create_directories(outputDirectory, filesystemError);
    if (filesystemError) {
        g_missingTextCaptureStarted.store(false);
        error = L"无法创建漏词采集目录";
        return false;
    }
    g_missingTextPath = outputDirectory /
        (L"missing_ui_text_" + std::to_wstring(GetCurrentProcessId()) + L".jsonl");
    HANDLE thread = CreateThread(nullptr, 0, MissingTextWriterThread, nullptr, 0, nullptr);
    if (!thread) {
        g_missingTextCaptureStarted.store(false);
        error = L"无法启动漏词写入线程";
        return false;
    }
    CloseHandle(thread);
    return true;
}

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

size_t GetMissingTextCount() {
    std::lock_guard<std::mutex> lock(g_missingTextMutex);
    return g_missingTexts.size();
}

} // namespace rizomuv::localizer
