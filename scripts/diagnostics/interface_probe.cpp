#include <windows.h>
#include <ole2.h>
#include <UIAutomationClient.h>

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::wstring processName = L"rizomuv.exe";
    std::filesystem::path output = L"rizomuv-ui.json";
    int watchSeconds = 0;
    bool testText = false;
    std::wstring testSource;
    std::wstring testTranslation;
};

struct WindowRecord {
    HWND hwnd{};
    HWND parent{};
    DWORD pid{};
    std::wstring className;
    std::wstring text;
    LONG_PTR style{};
    LONG_PTR exStyle{};
    RECT rect{};
    bool visible{};
    bool enabled{};
    bool unicode{};
    bool getTextResponsive{};
    bool setTextCandidate{};
};

struct MenuRecord {
    HWND owner{};
    int depth{};
    UINT position{};
    UINT id{};
    UINT type{};
    UINT state{};
    std::wstring text;
};

struct AutomationRecord {
    int depth{};
    int controlType{};
    std::wstring name;
    std::wstring automationId;
    std::wstring className;
    RECT rect{};
    bool enabled{};
    bool offscreen{};
};

template <typename T>
class ComHolder {
public:
    ComHolder() = default;
    ~ComHolder() { if (value_) value_->Release(); }
    ComHolder(const ComHolder&) = delete;
    ComHolder& operator=(const ComHolder&) = delete;
    T* get() const { return value_; }
    T** put() { if (value_) { value_->Release(); value_ = nullptr; } return &value_; }
private:
    T* value_{};
};

std::wstring TakeBstr(BSTR value) {
    if (!value) return {};
    std::wstring result(value, SysStringLen(value));
    SysFreeString(value);
    return result;
}

void WalkAutomation(IUIAutomationTreeWalker* walker, IUIAutomationElement* element,
                    int depth, std::vector<AutomationRecord>& output) {
    if (!walker || !element || depth > 48 || output.size() >= 50000) return;
    AutomationRecord record;
    record.depth = depth;
    element->get_CurrentControlType(&record.controlType);
    BSTR value = nullptr;
    if (SUCCEEDED(element->get_CurrentName(&value))) record.name = TakeBstr(value);
    value = nullptr;
    if (SUCCEEDED(element->get_CurrentAutomationId(&value))) record.automationId = TakeBstr(value);
    value = nullptr;
    if (SUCCEEDED(element->get_CurrentClassName(&value))) record.className = TakeBstr(value);
    element->get_CurrentBoundingRectangle(&record.rect);
    BOOL flag = FALSE;
    if (SUCCEEDED(element->get_CurrentIsEnabled(&flag))) record.enabled = flag != FALSE;
    flag = FALSE;
    if (SUCCEEDED(element->get_CurrentIsOffscreen(&flag))) record.offscreen = flag != FALSE;
    output.push_back(std::move(record));

    ComHolder<IUIAutomationElement> child;
    if (FAILED(walker->GetFirstChildElement(element, child.put())) || !child.get()) return;
    IUIAutomationElement* current = child.get();
    current->AddRef();
    while (current && output.size() < 50000) {
        WalkAutomation(walker, current, depth + 1, output);
        ComHolder<IUIAutomationElement> next;
        walker->GetNextSiblingElement(current, next.put());
        current->Release();
        current = next.get();
        if (current) current->AddRef();
    }
}

void CollectAutomation(const std::map<HWND, WindowRecord>& windows,
                       std::vector<AutomationRecord>& output) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return;
    ComHolder<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(automation.put()))) || !automation.get()) {
        if (SUCCEEDED(initialized)) CoUninitialize();
        return;
    }
    ComHolder<IUIAutomationTreeWalker> walker;
    if (FAILED(automation.get()->get_ControlViewWalker(walker.put())) || !walker.get()) {
        if (SUCCEEDED(initialized)) CoUninitialize();
        return;
    }
    for (const auto& [hwnd, item] : windows) {
        if (item.parent || !IsWindow(hwnd)) continue;
        ComHolder<IUIAutomationElement> root;
        if (SUCCEEDED(automation.get()->ElementFromHandle(hwnd, root.put())) && root.get())
            WalkAutomation(walker.get(), root.get(), 0, output);
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}

std::wstring JsonEscape(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.size() + 16);
    for (wchar_t ch : input) {
        switch (ch) {
        case L'\\': out += L"\\\\"; break;
        case L'\"': out += L"\\\""; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (ch < 0x20) {
                wchar_t buffer[7]{};
                swprintf_s(buffer, L"\\u%04x", static_cast<unsigned>(ch));
                out += buffer;
            } else {
                out += ch;
            }
        }
    }
    return out;
}

std::wstring GetClassNameString(HWND hwnd) {
    wchar_t buffer[512]{};
    const int length = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, length) : std::wstring();
}

std::wstring GetTextWithTimeout(HWND hwnd, bool& responsive) {
    responsive = false;
    DWORD_PTR lengthResult = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXTLENGTH, 0, 0,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 150, &lengthResult)) {
        return {};
    }
    const size_t capacity = std::min<size_t>(static_cast<size_t>(lengthResult) + 1, 65536);
    std::vector<wchar_t> buffer(std::max<size_t>(capacity, 2), L'\0');
    DWORD_PTR copied = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXT, static_cast<WPARAM>(buffer.size()),
                             reinterpret_cast<LPARAM>(buffer.data()),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 150, &copied)) {
        return {};
    }
    responsive = true;
    return std::wstring(buffer.data(), std::min<size_t>(copied, buffer.size() - 1));
}

std::wstring BaseNameForProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::vector<wchar_t> path(32768);
    DWORD size = static_cast<DWORD>(path.size());
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
        result = std::filesystem::path(std::wstring(path.data(), size)).filename().wstring();
    }
    CloseHandle(process);
    return result;
}

bool EqualsInsensitive(std::wstring left, std::wstring right) {
    std::transform(left.begin(), left.end(), left.begin(), towlower);
    std::transform(right.begin(), right.end(), right.begin(), towlower);
    return left == right;
}

std::set<DWORD> FindTargetPids(const std::wstring& processName) {
    struct Context { const std::wstring* name; std::set<DWORD>* pids; } context{&processName, nullptr};
    std::set<DWORD> pids;
    context.pids = &pids;
    EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
        auto* ctx = reinterpret_cast<Context*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid && EqualsInsensitive(BaseNameForProcess(pid), *ctx->name)) ctx->pids->insert(pid);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return pids;
}

void CollectMenu(HWND owner, HMENU menu, int depth, std::vector<MenuRecord>& output) {
    if (!menu || depth > 16) return;
    const int count = GetMenuItemCount(menu);
    for (int position = 0; position < count; ++position) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        wchar_t text[4096]{};
        info.dwTypeData = text;
        info.cch = static_cast<UINT>(std::size(text) - 1);
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &info)) continue;
        output.push_back({owner, depth, static_cast<UINT>(position), info.wID,
                          info.fType, info.fState, std::wstring(text, info.cch)});
        if (info.hSubMenu) CollectMenu(owner, info.hSubMenu, depth + 1, output);
    }
}

void CollectWindows(const std::set<DWORD>& pids, std::vector<WindowRecord>& windows,
                    std::vector<MenuRecord>& menus) {
    struct Context {
        const std::set<DWORD>* pids;
        std::vector<WindowRecord>* windows;
        std::vector<MenuRecord>* menus;
    } context{&pids, &windows, &menus};

    auto collect = [](HWND hwnd, LPARAM value) -> BOOL {
        auto* ctx = reinterpret_cast<Context*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!ctx->pids->count(pid)) return TRUE;
        bool responsive = false;
        WindowRecord record;
        record.hwnd = hwnd;
        record.parent = GetParent(hwnd);
        record.pid = pid;
        record.className = GetClassNameString(hwnd);
        record.text = GetTextWithTimeout(hwnd, responsive);
        record.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        record.exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &record.rect);
        record.visible = IsWindowVisible(hwnd) != FALSE;
        record.enabled = IsWindowEnabled(hwnd) != FALSE;
        record.unicode = IsWindowUnicode(hwnd) != FALSE;
        record.getTextResponsive = responsive;
        record.setTextCandidate = responsive && !record.text.empty();
        ctx->windows->push_back(std::move(record));
        if (HMENU menu = GetMenu(hwnd)) CollectMenu(hwnd, menu, 0, *ctx->menus);
        return TRUE;
    };

    EnumWindows(collect, reinterpret_cast<LPARAM>(&context));
    for (size_t index = 0; index < windows.size(); ++index) {
        if (windows[index].parent != nullptr) continue;
        EnumChildWindows(windows[index].hwnd, collect, reinterpret_cast<LPARAM>(&context));
    }
}

void MergeUnique(const std::vector<WindowRecord>& source, std::map<HWND, WindowRecord>& target) {
    for (const auto& item : source) target[item.hwnd] = item;
}

void MergeUnique(const std::vector<MenuRecord>& source, std::map<std::wstring, MenuRecord>& target) {
    for (const auto& item : source) {
        const std::wstring key = std::to_wstring(reinterpret_cast<uintptr_t>(item.owner)) + L":" +
            std::to_wstring(item.depth) + L":" + std::to_wstring(item.position) + L":" + item.text;
        target[key] = item;
    }
}

bool TemporaryReplace(const std::map<HWND, WindowRecord>& windows,
                      const std::wstring& source, const std::wstring& translation) {
    for (const auto& [hwnd, record] : windows) {
        if (record.text != source || !IsWindow(hwnd)) continue;
        DWORD_PTR ignored = 0;
        const LRESULT changed = SendMessageTimeoutW(
            hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(translation.c_str()),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &ignored);
        if (!changed) return false;
        std::wcout << L"Temporarily changed HWND 0x" << std::hex
                   << reinterpret_cast<uintptr_t>(hwnd) << std::dec << L" for 3 seconds.\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
        SendMessageTimeoutW(hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(source.c_str()),
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &ignored);
        return true;
    }
    return false;
}

bool TemporaryReplaceMenu(const std::map<std::wstring, MenuRecord>& menus,
                          const std::wstring& source, const std::wstring& translation) {
    for (const auto& [key, record] : menus) {
        (void)key;
        if (record.text != source || !IsWindow(record.owner)) continue;
        HMENU menu = GetMenu(record.owner);
        for (int depth = 0; menu && depth < record.depth; ++depth) {
            menu = GetSubMenu(menu, static_cast<int>(record.position));
        }
        // The report stores the position at each level but not the complete path;
        // top-level items are therefore the safe candidates for this first probe.
        if (!menu || record.depth != 0) continue;
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        info.dwTypeData = const_cast<wchar_t*>(translation.c_str());
        if (!SetMenuItemInfoW(menu, record.position, TRUE, &info)) continue;
        DrawMenuBar(record.owner);
        std::wcout << L"Temporarily changed menu text for 3 seconds.\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
        info.dwTypeData = const_cast<wchar_t*>(source.c_str());
        SetMenuItemInfoW(menu, record.position, TRUE, &info);
        DrawMenuBar(record.owner);
        return true;
    }
    return false;
}

void WriteJson(const std::filesystem::path& path, const std::set<DWORD>& pids,
               const std::map<HWND, WindowRecord>& windows,
               const std::map<std::wstring, MenuRecord>& menus,
               const std::vector<AutomationRecord>& automation) {
    std::wofstream out(path, std::ios::binary | std::ios::trunc);
    out.imbue(std::locale(std::locale::classic(), new std::codecvt_utf8_utf16<wchar_t>));
    out << L"{\n  \"schema\": \"rizomuv-ui-probe-v1\",\n  \"processIds\": [";
    bool first = true;
    for (DWORD pid : pids) { if (!first) out << L", "; first = false; out << pid; }
    out << L"],\n  \"summary\": {\n    \"windows\": " << windows.size()
        << L",\n    \"textWindows\": "
        << std::count_if(windows.begin(), windows.end(), [](const auto& pair) { return !pair.second.text.empty(); })
        << L",\n    \"menuItems\": " << menus.size()
        << L",\n    \"automationElements\": " << automation.size()
        << L"\n  },\n  \"windows\": [\n";
    first = true;
    for (const auto& [hwnd, item] : windows) {
        if (!first) out << L",\n";
        first = false;
        out << L"    {\"hwnd\": \"0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd)
            << L"\", \"parent\": \"0x" << reinterpret_cast<uintptr_t>(item.parent) << std::dec
            << L"\", \"pid\": " << item.pid
            << L", \"class\": \"" << JsonEscape(item.className)
            << L"\", \"text\": \"" << JsonEscape(item.text)
            << L"\", \"visible\": " << (item.visible ? L"true" : L"false")
            << L", \"enabled\": " << (item.enabled ? L"true" : L"false")
            << L", \"unicode\": " << (item.unicode ? L"true" : L"false")
            << L", \"wmGetText\": " << (item.getTextResponsive ? L"true" : L"false")
            << L", \"setTextCandidate\": " << (item.setTextCandidate ? L"true" : L"false")
            << L", \"rect\": [" << item.rect.left << L", " << item.rect.top << L", "
            << item.rect.right << L", " << item.rect.bottom << L"]}";
    }
    out << L"\n  ],\n  \"menus\": [\n";
    first = true;
    for (const auto& [key, item] : menus) {
        (void)key;
        if (!first) out << L",\n";
        first = false;
        out << L"    {\"owner\": \"0x" << std::hex << reinterpret_cast<uintptr_t>(item.owner)
            << std::dec << L"\", \"depth\": " << item.depth << L", \"position\": "
            << item.position << L", \"id\": " << item.id << L", \"text\": \""
            << JsonEscape(item.text) << L"\"}";
    }
    out << L"\n  ],\n  \"automation\": [\n";
    first = true;
    for (const auto& item : automation) {
        if (!first) out << L",\n";
        first = false;
        out << L"    {\"depth\": " << item.depth << L", \"controlType\": "
            << item.controlType << L", \"class\": \"" << JsonEscape(item.className)
            << L"\", \"automationId\": \"" << JsonEscape(item.automationId)
            << L"\", \"name\": \"" << JsonEscape(item.name)
            << L"\", \"enabled\": " << (item.enabled ? L"true" : L"false")
            << L", \"offscreen\": " << (item.offscreen ? L"true" : L"false")
            << L", \"rect\": [" << item.rect.left << L", " << item.rect.top << L", "
            << item.rect.right << L", " << item.rect.bottom << L"]}";
    }
    out << L"\n  ]\n}\n";
}

void PrintUsage() {
    std::wcout << L"RizomUVUiProbe [--process rizomuv.exe] [--output report.json] [--watch seconds]\n"
                  L"                [--test-text \"English\" \"中文\"]\n";
}

bool ParseOptions(int argc, wchar_t** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--process" && i + 1 < argc) options.processName = argv[++i];
        else if (arg == L"--output" && i + 1 < argc) options.output = argv[++i];
        else if (arg == L"--watch" && i + 1 < argc) options.watchSeconds = std::max(0, _wtoi(argv[++i]));
        else if (arg == L"--test-text" && i + 2 < argc) {
            options.testText = true;
            options.testSource = argv[++i];
            options.testTranslation = argv[++i];
        } else if (arg == L"--help" || arg == L"-h") { PrintUsage(); return false; }
        else { std::wcerr << L"Unknown or incomplete argument: " << arg << L"\n"; return false; }
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    Options options;
    if (!ParseOptions(argc, argv, options)) return argc > 1 ? 2 : 0;

    const std::set<DWORD> pids = FindTargetPids(options.processName);
    if (pids.empty()) {
        std::wcerr << L"No running process named " << options.processName << L" was found.\n";
        return 3;
    }

    std::map<HWND, WindowRecord> windows;
    std::map<std::wstring, MenuRecord> menus;
    const int passes = options.watchSeconds > 0 ? options.watchSeconds * 4 + 1 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<WindowRecord> currentWindows;
        std::vector<MenuRecord> currentMenus;
        CollectWindows(pids, currentWindows, currentMenus);
        MergeUnique(currentWindows, windows);
        MergeUnique(currentMenus, menus);
        if (pass + 1 < passes) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::vector<AutomationRecord> automation;
    CollectAutomation(windows, automation);
    WriteJson(options.output, pids, windows, menus, automation);
    std::wcout << L"Captured " << windows.size() << L" windows/controls and "
               << menus.size() << L" menu items and " << automation.size()
               << L" automation elements. Report: " << options.output.wstring() << L"\n";

    if (options.testText) {
        bool changed = TemporaryReplace(windows, options.testSource, options.testTranslation);
        if (!changed) changed = TemporaryReplaceMenu(menus, options.testSource, options.testTranslation);
        std::wcout << (changed ? L"Temporary replacement succeeded and was restored.\n"
                               : L"No exact, replaceable text target was found.\n");
        return changed ? 0 : 4;
    }
    return 0;
}
