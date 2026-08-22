#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int kLauncherResource = 201;
constexpr int kRuntimeResource = 202;
constexpr int kDictionaryResource = 203;
constexpr int kDirectoryEdit = 1001;
constexpr int kBrowseButton = 1002;
constexpr int kInstallButton = 1003;
constexpr int kUninstallButton = 1004;
constexpr wchar_t kTitle[] = L"RizomUV 简体中文补丁";
constexpr wchar_t kPluginFolder[] = L"RizomUVChinese";
constexpr wchar_t kLegacyPluginFolder[] = L"ChineseLocalizer";

struct PayloadEntry {
    const wchar_t* name = nullptr;
    const void* data = nullptr;
    DWORD size = 0;
};

struct WindowState {
    HWND directoryEdit = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
};

int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

std::wstring Environment(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::vector<wchar_t> value(size);
    const DWORD length = GetEnvironmentVariableW(name, value.data(), size);
    return length ? std::wstring(value.data(), length) : std::wstring();
}

std::wstring ControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> value(static_cast<size_t>(length) + 1);
    GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    return value.data();
}

bool IsValidRizomDirectory(const std::filesystem::path& directory) {
    const auto executable = directory / L"rizomuv.exe";
    HANDLE file = CreateFileW(executable.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    bool valid = ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
                 read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE;
    if (valid) {
        LARGE_INTEGER position{};
        position.QuadPart = dos.e_lfanew;
        valid = SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
        DWORD signature = 0;
        IMAGE_FILE_HEADER header{};
        valid = valid && ReadFile(file, &signature, sizeof(signature), &read, nullptr) &&
                signature == IMAGE_NT_SIGNATURE &&
                ReadFile(file, &header, sizeof(header), &read, nullptr) &&
                header.Machine == IMAGE_FILE_MACHINE_AMD64;
    }
    CloseHandle(file);
    return valid;
}

std::filesystem::path DefaultRizomDirectory() {
    const std::wstring programFiles = Environment(L"ProgramFiles");
    if (!programFiles.empty()) {
        const std::filesystem::path standard =
            std::filesystem::path(programFiles) / L"Rizom Lab" / L"RizomUV 2025.0";
        if (IsValidRizomDirectory(standard)) return standard;
        const std::filesystem::path parent = std::filesystem::path(programFiles) / L"Rizom Lab";
        std::error_code error;
        if (std::filesystem::is_directory(parent, error)) {
            for (const auto& item : std::filesystem::directory_iterator(parent, error)) {
                if (item.is_directory() && IsValidRizomDirectory(item.path())) return item.path();
            }
        }
    }
    return {};
}

bool SelectDirectory(HWND owner, std::filesystem::path& result) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) return false;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"请选择包含 rizomuv.exe 的安装目录");
    if (!result.empty()) {
        IShellItem* initial = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(result.c_str(), nullptr, IID_PPV_ARGS(&initial)))) {
            dialog->SetFolder(initial);
            initial->Release();
        }
    }
    bool selected = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
                selected = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

bool LoadPayloadResources(std::vector<PayloadEntry>& entries) {
    const HMODULE module = GetModuleHandleW(nullptr);
    for (const auto& resource : {
             std::pair{kLauncherResource, L"RizomUVChineseLauncher.exe"},
             std::pair{kRuntimeResource, L"RizomUVChineseRuntime.dll"},
             std::pair{kDictionaryResource, L"ui_zh-CN.json"}}) {
        const HRSRC handle = FindResourceW(module, MAKEINTRESOURCEW(resource.first), RT_RCDATA);
        if (!handle) return false;
        const HGLOBAL loaded = LoadResource(module, handle);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        const DWORD size = SizeofResource(module, handle);
        if (!data || !size) return false;
        entries.push_back({resource.second, data, size});
    }
    return entries.size() == 3;
}

bool ExtractPayload(const std::vector<PayloadEntry>& entries,
                    const std::filesystem::path& destination, std::wstring& error) {
    std::error_code filesystemError;
    std::filesystem::create_directories(destination / L"translations", filesystemError);
    if (filesystemError) { error = L"无法创建汉化目录"; return false; }
    bool ok = true;
    for (const auto& entry : entries) {
        std::filesystem::path target = destination / entry.name;
        if (wcscmp(entry.name, L"ui_zh-CN.json") == 0)
            target = destination / L"translations" / L"ui_zh-CN.json";
        HANDLE output = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) { ok = false; break; }
        DWORD written = 0;
        ok = WriteFile(output, entry.data, entry.size, &written, nullptr) && written == entry.size;
        CloseHandle(output);
        if (!ok) break;
    }
    if (!ok) error = L"写入汉化文件失败";
    return ok;
}

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &value))) result = value;
    CoTaskMemFree(value);
    return result;
}

bool CreateShortcut(const std::filesystem::path& shortcut,
                    const std::filesystem::path& launcher,
                    const std::filesystem::path& workingDirectory) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) return false;
    link->SetPath(launcher.c_str());
    link->SetWorkingDirectory(workingDirectory.c_str());
    link->SetDescription(L"通过简体中文显示层补丁启动 RizomUV");
    link->SetIconLocation((workingDirectory / L"rizomuv.exe").c_str(), 0);
    IPersistFile* persist = nullptr;
    const bool ok = SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist))) &&
                    SUCCEEDED(persist->Save(shortcut.c_str(), TRUE));
    if (persist) persist->Release();
    link->Release();
    return ok;
}

std::vector<std::filesystem::path> ShortcutPaths() {
    return {
        KnownFolder(FOLDERID_Desktop) / L"RizomUV 简体中文版.lnk",
        KnownFolder(FOLDERID_Programs) / L"RizomUV 简体中文版.lnk",
    };
}

bool Install(const std::filesystem::path& rizomDirectory, std::wstring& message) {
    std::vector<PayloadEntry> entries;
    if (!LoadPayloadResources(entries)) { message = L"安装包资源不完整。"; return false; }
    const auto pluginDirectory = rizomDirectory / kPluginFolder;
    const auto stagingDirectory = rizomDirectory / L"RizomUVChinese.installing";
    const auto backupDirectory = rizomDirectory / L"RizomUVChinese.previous";
    std::error_code filesystemError;
    std::filesystem::remove_all(stagingDirectory, filesystemError);
    filesystemError.clear();
    std::filesystem::remove_all(backupDirectory, filesystemError);
    if (filesystemError) {
        message = L"无法清理上次安装留下的临时目录。\n请关闭 RizomUV 后重试。";
        return false;
    }
    std::wstring error;
    if (!ExtractPayload(entries, stagingDirectory, error)) {
        std::filesystem::remove_all(stagingDirectory, filesystemError);
        message = error;
        return false;
    }
    for (const auto* required : {L"RizomUVChineseLauncher.exe", L"RizomUVChineseRuntime.dll",
                                  L"translations\\ui_zh-CN.json"}) {
        if (!std::filesystem::is_regular_file(stagingDirectory / required)) {
            std::filesystem::remove_all(stagingDirectory, filesystemError);
            message = L"安装文件自检失败，原汉化未被修改。";
            return false;
        }
    }
    const bool hadPrevious = std::filesystem::exists(pluginDirectory);
    if (hadPrevious) {
        std::filesystem::rename(pluginDirectory, backupDirectory, filesystemError);
        if (filesystemError) {
            std::filesystem::remove_all(stagingDirectory, filesystemError);
            message = L"旧汉化文件正在使用。\n请关闭 RizomUV 后重新安装。";
            return false;
        }
    }
    std::filesystem::rename(stagingDirectory, pluginDirectory, filesystemError);
    if (filesystemError) {
        if (hadPrevious) {
            std::error_code restoreError;
            std::filesystem::rename(backupDirectory, pluginDirectory, restoreError);
        }
        std::filesystem::remove_all(stagingDirectory, filesystemError);
        message = L"安装失败，已恢复安装前的汉化文件。";
        return false;
    }
    if (hadPrevious) std::filesystem::remove_all(backupDirectory, filesystemError);
    // 清理本项目早期测试版使用的旧目录名。
    std::filesystem::remove_all(rizomDirectory / kLegacyPluginFolder, filesystemError);
    const auto launcher = pluginDirectory / L"RizomUVChineseLauncher.exe";
    for (const auto& shortcut : ShortcutPaths()) {
        std::error_code ignored;
        std::filesystem::create_directories(shortcut.parent_path(), ignored);
        if (!CreateShortcut(shortcut, launcher, rizomDirectory)) {
            message = L"汉化文件已安装，但创建快捷方式失败。";
            return false;
        }
    }
    message = L"汉化安装完成。\n\n桌面和开始菜单已创建“RizomUV 简体中文版”快捷方式。\n以后请通过该快捷方式启动。";
    return true;
}

bool Uninstall(const std::filesystem::path& rizomDirectory, std::wstring& message) {
    for (const auto& shortcut : ShortcutPaths()) {
        std::error_code ignored;
        std::filesystem::remove(shortcut, ignored);
    }
    const auto pluginDirectory = rizomDirectory / kPluginFolder;
    std::error_code error;
    std::filesystem::remove_all(pluginDirectory, error);
    if (!error) std::filesystem::remove_all(rizomDirectory / kLegacyPluginFolder, error);
    if (!error) std::filesystem::remove_all(rizomDirectory / L"RizomUVChinese.installing", error);
    if (!error) std::filesystem::remove_all(rizomDirectory / L"RizomUVChinese.previous", error);
    if (error || std::filesystem::exists(pluginDirectory)) {
        message = L"部分文件正在使用，无法完整拆卸。\n请关闭 RizomUV 后重试。";
        return false;
    }
    message = L"汉化已完整拆卸。\n\n原版 RizomUV 文件未被修改。";
    return true;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = reinterpret_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE: {
        state->dpi = GetDpiForWindow(window);
        const auto s = [state](int value) { return Scale(value, state->dpi); };
        state->font = CreateFontW(-MulDiv(10, state->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE,
                                  FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                  L"Microsoft YaHei UI");
        const auto defaultDirectory = DefaultRizomDirectory().wstring();
        HWND label = CreateWindowExW(0, L"STATIC", L"RizomUV 目录",
            WS_CHILD | WS_VISIBLE, s(24), s(20), s(120), s(22),
            window, nullptr, nullptr, nullptr);
        state->directoryEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", defaultDirectory.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, s(24), s(45), s(455), s(31),
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDirectoryEdit)), nullptr, nullptr);
        HWND browse = CreateWindowExW(0, L"BUTTON", L"选择目录",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            s(489), s(44), s(87), s(33), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrowseButton)), nullptr, nullptr);
        HWND install = CreateWindowExW(0, L"BUTTON", L"安装汉化",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            s(324), s(98), s(120), s(36), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInstallButton)), nullptr, nullptr);
        HWND uninstall = CreateWindowExW(0, L"BUTTON", L"拆卸汉化",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            s(456), s(98), s(120), s(36), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUninstallButton)), nullptr, nullptr);
        for (HWND control : {label, state->directoryEdit, browse, install, uninstall})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kBrowseButton) {
            std::filesystem::path selected = ControlText(state->directoryEdit);
            if (SelectDirectory(window, selected)) SetWindowTextW(state->directoryEdit, selected.c_str());
            return 0;
        }
        if (id == kInstallButton || id == kUninstallButton) {
            const std::filesystem::path directory = ControlText(state->directoryEdit);
            if (!IsValidRizomDirectory(directory)) {
                MessageBoxW(window, L"所选目录中没有有效的 x64 rizomuv.exe。\n\n请选择 RizomUV 的实际安装目录。",
                            kTitle, MB_OK | MB_ICONERROR);
                return 0;
            }
            std::wstring result;
            const bool ok = id == kInstallButton ? Install(directory, result) : Uninstall(directory, result);
            MessageBoxW(window, result.c_str(), kTitle, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (state && state->font) DeleteObject(state->font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.lpszClassName = L"RizomUVChineseInstallerWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&windowClass);
    WindowState state;
    const UINT dpi = GetDpiForSystem();
    RECT frame{0, 0, Scale(600, dpi), Scale(155, dpi)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, windowClass.lpszClassName, kTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, nullptr, nullptr, instance, &state);
    if (!window) { CoUninitialize(); return 1; }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
