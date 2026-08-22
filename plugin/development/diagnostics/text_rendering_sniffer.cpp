#include <windows.h>
#include <winnt.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

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

HMODULE g_module = nullptr;
std::atomic<bool> g_running{true};
std::atomic<unsigned long long> g_captureUntil{0};
std::mutex g_logMutex;
std::set<std::wstring> g_seen;
thread_local bool g_insideHook = false;

unsigned long long TickNow() { return GetTickCount64(); }

std::wstring ModuleDirectory() {
    std::vector<wchar_t> path(32768);
    const DWORD size = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
    std::wstring result(path.data(), size);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string output(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        output.data(), size, nullptr, nullptr);
    return output;
}

std::string JsonEscape(const std::string& text) {
    std::string output;
    for (unsigned char ch : text) {
        switch (ch) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch >= 0x20) output += static_cast<char>(ch);
        }
    }
    return output;
}

void Capture(const wchar_t* api, LPCWSTR text, int length) {
    if (g_insideHook || !text || TickNow() > g_captureUntil.load(std::memory_order_relaxed)) return;
    g_insideHook = true;
    if (length < 0) length = static_cast<int>(wcsnlen_s(text, 16384));
    length = (length < 0) ? 0 : std::min(length, 16384);
    std::wstring value(text, text + length);
    while (!value.empty() && (value.back() == L'\0' || value.back() == L'\r' || value.back() == L'\n'))
        value.pop_back();
    bool printable = false;
    for (wchar_t ch : value) if (iswalpha(ch)) { printable = true; break; }
    if (printable) {
        const std::wstring key = std::wstring(api) + L"\n" + value;
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_seen.insert(key).second) {
            const std::wstring path = ModuleDirectory() + L"\\RizomUV_text_sniffer.jsonl";
            FILE* file = nullptr;
            if (_wfopen_s(&file, path.c_str(), L"ab") == 0 && file) {
                const std::string api8 = JsonEscape(Utf8(api));
                const std::string text8 = JsonEscape(Utf8(value));
                std::fprintf(file, "{\"api\":\"%s\",\"text\":\"%s\"}\n", api8.c_str(), text8.c_str());
                std::fclose(file);
            }
        }
    }
    g_insideHook = false;
}

int WINAPI HookDrawTextW(HDC dc, LPCWSTR text, int count, LPRECT rect, UINT format) {
    Capture(L"DrawTextW", text, count);
    return g_drawTextW(dc, text, count, rect, format);
}
int WINAPI HookDrawTextExW(HDC dc, LPWSTR text, int count, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params) {
    Capture(L"DrawTextExW", text, count);
    return g_drawTextExW(dc, text, count, rect, format, params);
}
BOOL WINAPI HookTextOutW(HDC dc, int x, int y, LPCWSTR text, int count) {
    Capture(L"TextOutW", text, count);
    return g_textOutW(dc, x, y, text, count);
}
BOOL WINAPI HookExtTextOutW(HDC dc, int x, int y, UINT options, const RECT* rect,
                            LPCWSTR text, UINT count, const INT* spacing) {
    Capture(L"ExtTextOutW", text, static_cast<int>(count));
    return g_extTextOutW(dc, x, y, options, rect, text, count, spacing);
}
BOOL WINAPI HookGetTextExtentPoint32W(HDC dc, LPCWSTR text, int count, LPSIZE size) {
    Capture(L"GetTextExtentPoint32W", text, count);
    return g_getTextExtentPoint32W(dc, text, count, size);
}
BOOL WINAPI HookGetTextExtentExPointW(HDC dc, LPCWSTR text, int count, int maxExtent,
                                      LPINT fit, LPINT dx, LPSIZE size) {
    Capture(L"GetTextExtentExPointW", text, count);
    return g_getTextExtentExPointW(dc, text, count, maxExtent, fit, dx, size);
}

bool MatchDll(const char* name, const char* expected) {
    return name && _stricmp(name, expected) == 0;
}

void PatchSlot(void** slot, void* replacement, void** original) {
    if (!slot || !replacement) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return;
    if (!*original) *original = *slot;
    *slot = replacement;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
}

void PatchImports(HMODULE module) {
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const char* dll = reinterpret_cast<const char*>(base + descriptor->Name);
        if (!MatchDll(dll, "user32.dll") && !MatchDll(dll, "gdi32.dll")) continue;
        auto* names = descriptor->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk)
            : reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const char* name = reinterpret_cast<const char*>(import->Name);
            void** slot = reinterpret_cast<void**>(&slots->u1.Function);
            if (strcmp(name, "DrawTextW") == 0) PatchSlot(slot, reinterpret_cast<void*>(HookDrawTextW), reinterpret_cast<void**>(&g_drawTextW));
            else if (strcmp(name, "DrawTextExW") == 0) PatchSlot(slot, reinterpret_cast<void*>(HookDrawTextExW), reinterpret_cast<void**>(&g_drawTextExW));
            else if (strcmp(name, "TextOutW") == 0) PatchSlot(slot, reinterpret_cast<void*>(HookTextOutW), reinterpret_cast<void**>(&g_textOutW));
            else if (strcmp(name, "ExtTextOutW") == 0) PatchSlot(slot, reinterpret_cast<void*>(HookExtTextOutW), reinterpret_cast<void**>(&g_extTextOutW));
            else if (strcmp(name, "GetTextExtentPoint32W") == 0) PatchSlot(slot, reinterpret_cast<void*>(HookGetTextExtentPoint32W), reinterpret_cast<void**>(&g_getTextExtentPoint32W));
            else if (strcmp(name, "GetTextExtentExPointW") == 0) PatchSlot(slot, reinterpret_cast<void*>(HookGetTextExtentExPointW), reinterpret_cast<void**>(&g_getTextExtentExPointW));
        }
    }
}

DWORD WINAPI Worker(void*) {
    PatchImports(GetModuleHandleW(nullptr));
    // Capture initial UI construction as well as later user-triggered F12 windows.
    g_captureUntil.store(TickNow() + 5000);
    bool previousF12 = false;
    while (g_running.load()) {
        const bool f12 = (GetAsyncKeyState(VK_F12) & 0x8000) != 0 &&
                         (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                         (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (f12 && !previousF12) {
            {
                std::lock_guard<std::mutex> lock(g_logMutex);
                g_seen.clear();
            }
            g_captureUntil.store(TickNow() + 3000);
            MessageBeep(MB_OK);
        }
        previousF12 = f12;
        Sleep(25);
    }
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        if (HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running.store(false);
    }
    return TRUE;
}
