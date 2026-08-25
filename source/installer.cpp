#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>
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
constexpr wchar_t kPluginFolder[] = L"ChineseLauncher";

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

std::vector<DWORD> RizomUVProcessIds(
    const std::filesystem::path& rizomDirectory) {
    std::vector<DWORD> processIds;
    std::error_code pathError;
    const std::filesystem::path targetExecutable = std::filesystem::weakly_canonical(
        rizomDirectory / L"rizomuv.exe", pathError);
    if (pathError) return processIds;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processIds;
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) {
        do {
            if (process.th32ProcessID == GetCurrentProcessId() ||
                _wcsicmp(process.szExeFile, L"rizomuv.exe") != 0)
                continue;
            HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                               FALSE, process.th32ProcessID);
            if (!processHandle) continue;
            std::vector<wchar_t> executablePath(32768);
            DWORD length = static_cast<DWORD>(executablePath.size());
            const bool pathRead = QueryFullProcessImageNameW(
                processHandle, 0, executablePath.data(), &length) != FALSE;
            CloseHandle(processHandle);
            if (pathRead && _wcsicmp(
                    std::wstring(executablePath.data(), length).c_str(),
                    targetExecutable.c_str()) == 0)
                processIds.push_back(process.th32ProcessID);
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);
    return processIds;
}

bool IsRizomUVRunning(const std::filesystem::path& rizomDirectory) {
    return !RizomUVProcessIds(rizomDirectory).empty();
}

bool TerminateRizomUVProcesses(const std::filesystem::path& rizomDirectory) {
    bool success = true;
    for (DWORD processId : RizomUVProcessIds(rizomDirectory)) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                     FALSE, processId);
        if (!process) {
            success = false;
            continue;
        }
        if (!TerminateProcess(process, 0) ||
            WaitForSingleObject(process, 5000) != WAIT_OBJECT_0)
            success = false;
        CloseHandle(process);
    }
    return success && !IsRizomUVRunning(rizomDirectory);
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
             std::pair{kDictionaryResource, L"dictionary_zh.json"}}) {
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
    std::filesystem::create_directories(destination, filesystemError);
    if (filesystemError) { error = L"无法创建汉化目录"; return false; }
    bool ok = true;
    for (const auto& entry : entries) {
        const std::filesystem::path target = destination / entry.name;
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

// ---------------------------------------------------------------------------
// Interactive-user shortcut placement
// ---------------------------------------------------------------------------
// The installer runs elevated, so the process identity is an administrator
// account and NOT necessarily the person who will actually launch the app.
// Creating shortcuts against the elevated token puts them in the wrong user's
// Desktop / Start Menu (the user then reports "no shortcut was created").
// Instead, resolve the interactive console session (the explorer.exe owner of
// the current session) and place shortcuts in *that* user's real folders,
// honouring any Desktop / Start Menu redirection (e.g. OneDrive).

// Shared PUBLIC (interactive) folders are always writable from the elevated
// context; per-user folders must be resolved from the interactive user's SID.
std::wstring InteractiveSessionSid() {
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return L"";
    std::wstring found;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0) continue;
            DWORD processSession = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &processSession) ||
                processSession != sessionId) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                        FALSE, entry.th32ProcessID);
            if (!process) continue;
            HANDLE token = nullptr;
            if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
                DWORD required = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &required);
                std::vector<unsigned char> buffer(required);
                if (required > 0 && GetTokenInformation(
                        token, TokenUser, buffer.data(), required, &required)) {
                    TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
                    LPWSTR sidString = nullptr;
                    if (ConvertSidToStringSidW(user->User.Sid, &sidString)) {
                        found = sidString;
                        LocalFree(sidString);
                    }
                }
                CloseHandle(token);
            }
            CloseHandle(process);
            if (!found.empty()) break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

// Read the interactive user's real folder path from HKEY_USERS\<SID>, which
// reflects Desktop / Start Menu redirection (e.g. OneDrive) and expands any
// %USERPROFILE% variable. Returns empty if the SID has no usable entry.
std::filesystem::path InteractiveUserFolder(REFKNOWNFOLDERID id) {
    const std::wstring sid = InteractiveSessionSid();
    if (sid.empty()) return std::filesystem::path();
    const wchar_t* valueName = nullptr;
    if (id == FOLDERID_Desktop) valueName = L"Desktop";
    else if (id == FOLDERID_Programs) valueName = L"Programs";
    if (!valueName) return std::filesystem::path();

    HKEY hive = nullptr;
    if (RegOpenKeyExW(HKEY_USERS, sid.c_str(), 0, KEY_READ, &hive) != ERROR_SUCCESS)
        return std::filesystem::path();
    HKEY shellFolders = nullptr;
    if (RegOpenKeyExW(hive,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
            0, KEY_READ, &shellFolders) != ERROR_SUCCESS) {
        RegCloseKey(hive);
        return std::filesystem::path();
    }
    wchar_t raw[2048] = {};
    DWORD size = sizeof(raw);
    DWORD type = 0;
    std::wstring value;
    if (RegQueryValueExW(shellFolders, valueName, nullptr, &type,
                         reinterpret_cast<BYTE*>(raw), &size) == ERROR_SUCCESS)
        value = raw;
    RegCloseKey(shellFolders);
    RegCloseKey(hive);
    if (value.empty()) return std::filesystem::path();

    std::vector<wchar_t> expanded(8192);
    const DWORD length = ExpandEnvironmentStringsW(
        value.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
    if (length && length <= expanded.size())
        return std::filesystem::path(std::wstring(expanded.data(), length - 1));
    return std::filesystem::path(value);
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

bool GrantCurrentUserPluginAccess(const std::filesystem::path& directory) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    std::vector<unsigned char> tokenInformation(required);
    const bool tokenRead = required > 0 && GetTokenInformation(
        token, TokenUser, tokenInformation.data(), required, &required) != FALSE;
    CloseHandle(token);
    if (!tokenRead) return false;

    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(tokenInformation.data());
    PACL currentDacl = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    const DWORD securityRead = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(directory.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &currentDacl, nullptr,
        &securityDescriptor);
    if (securityRead != ERROR_SUCCESS) return false;

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE |
                                  FILE_GENERIC_EXECUTE | DELETE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(tokenUser->User.Sid);

    PACL updatedDacl = nullptr;
    const DWORD aclCreated = SetEntriesInAclW(1, &access, currentDacl, &updatedDacl);
    bool applied = false;
    if (aclCreated == ERROR_SUCCESS) {
        applied = SetNamedSecurityInfoW(
            const_cast<wchar_t*>(directory.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, updatedDacl,
            nullptr) == ERROR_SUCCESS;
    }
    if (updatedDacl) LocalFree(updatedDacl);
    LocalFree(securityDescriptor);
    return applied;
}

// The installer runs elevated, so the shortcut it writes is owned by the
// elevated account. Grant the target user full control on the .lnk so a normal
// user can always open it; otherwise Windows reports that the item referenced
// by the shortcut cannot be accessed / no permission.
bool GrantShortcutAccess(const std::filesystem::path& shortcut) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    std::vector<unsigned char> tokenInfo(required);
    const bool tokenRead = required > 0 && GetTokenInformation(
        token, TokenUser, tokenInfo.data(), required, &required) != FALSE;
    CloseHandle(token);
    if (!tokenRead) return false;
    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(tokenInfo.data());

    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    PACL currentDacl = nullptr;
    const DWORD securityRead = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(shortcut.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &currentDacl, nullptr,
        &securityDescriptor);
    if (securityRead != ERROR_SUCCESS) return false;

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL | WRITE_DAC | WRITE_OWNER | DELETE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(tokenUser->User.Sid);

    PACL updatedDacl = nullptr;
    const DWORD aclCreated = SetEntriesInAclW(1, &access, currentDacl, &updatedDacl);
    bool applied = false;
    if (aclCreated == ERROR_SUCCESS) {
        // DACL grant is what makes the shortcut openable; setting the owner is
        // cosmetic and may fail without SeRestorePrivilege, so apply it
        // independently and never let it block the access grant.
        applied = SetNamedSecurityInfoW(
            const_cast<wchar_t*>(shortcut.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, updatedDacl,
            nullptr) == ERROR_SUCCESS;
        SetNamedSecurityInfoW(const_cast<wchar_t*>(shortcut.c_str()), SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION,
                              static_cast<PSID>(tokenUser->User.Sid),
                              nullptr, nullptr, nullptr);
    }
    if (updatedDacl) LocalFree(updatedDacl);
    LocalFree(securityDescriptor);
    return applied;
}

std::vector<std::filesystem::path> ShortcutPaths() {
    std::vector<std::filesystem::path> paths;
    // Prefer the interactive user's real folders so shortcuts land on the
    // account that will actually run the app, even under elevation and even
    // when Desktop/Start Menu are redirected (OneDrive). Fall back to the
    // process's own known folders if the interactive user cannot be resolved.
    std::filesystem::path desktop = InteractiveUserFolder(FOLDERID_Desktop);
    std::filesystem::path programs = InteractiveUserFolder(FOLDERID_Programs);
    if (desktop.empty()) desktop = KnownFolder(FOLDERID_Desktop);
    if (programs.empty()) programs = KnownFolder(FOLDERID_Programs);
    if (!desktop.empty()) paths.push_back(desktop / L"RizomUV 简体中文版.lnk");
    if (!programs.empty()) paths.push_back(programs / L"RizomUV 简体中文版.lnk");
    return paths;
}

bool Install(const std::filesystem::path& rizomDirectory, std::wstring& message) {
    if (IsRizomUVRunning(rizomDirectory)) {
        message = L"RizomUV 正在运行。\n请先关闭 RizomUV，再安装汉化。";
        return false;
    }
    std::vector<PayloadEntry> entries;
    if (!LoadPayloadResources(entries)) { message = L"安装包资源不完整。"; return false; }
    const auto pluginDirectory = rizomDirectory / kPluginFolder;
    const auto stagingDirectory = rizomDirectory / L"ChineseLauncher.installing";
    const auto backupDirectory = rizomDirectory / L"ChineseLauncher.previous";
    std::error_code filesystemError;
    if (!std::filesystem::exists(pluginDirectory) &&
        std::filesystem::exists(backupDirectory)) {
        std::filesystem::rename(backupDirectory, pluginDirectory, filesystemError);
        if (filesystemError) {
            message = L"检测到上次更新留下的备份，但无法自动恢复。";
            return false;
        }
    }
    std::filesystem::remove_all(stagingDirectory, filesystemError);
    if (filesystemError) {
        message = L"无法清理上次安装留下的临时目录。";
        return false;
    }
    std::filesystem::remove_all(backupDirectory, filesystemError);
    if (filesystemError) {
        message = L"无法清理上次安装留下的备份目录。";
        return false;
    }
    std::wstring error;
    if (!ExtractPayload(entries, stagingDirectory, error)) {
        std::filesystem::remove_all(stagingDirectory, filesystemError);
        message = error;
        return false;
    }
    for (const auto* required : {L"RizomUVChineseLauncher.exe", L"RizomUVChineseRuntime.dll",
                                  L"dictionary_zh.json"}) {
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
    if (!GrantCurrentUserPluginAccess(pluginDirectory)) {
        std::error_code rollbackError;
        std::filesystem::remove_all(pluginDirectory, rollbackError);
        if (hadPrevious && !rollbackError)
            std::filesystem::rename(backupDirectory, pluginDirectory, rollbackError);
        message = rollbackError
            ? L"无法设置漏词探测文件的写入权限，且自动回滚失败。"
            : L"无法设置漏词探测文件的写入权限，已恢复安装前状态。";
        return false;
    }
    if (hadPrevious) std::filesystem::remove_all(backupDirectory, filesystemError);
    // 清理本项目早期版本使用的目录名。
    for (const auto* legacy : {L"RizomUVChinese", L"ChineseLocalizer"})
        std::filesystem::remove_all(rizomDirectory / legacy, filesystemError);
    const auto launcher = pluginDirectory / L"RizomUVChineseLauncher.exe";
    for (const auto& shortcut : ShortcutPaths()) {
        std::error_code ignored;
        std::filesystem::create_directories(shortcut.parent_path(), ignored);
        if (!CreateShortcut(shortcut, launcher, rizomDirectory)) {
            message = L"汉化文件已安装，但创建快捷方式失败。";
            return false;
        }
        GrantShortcutAccess(shortcut);
    }
    message = L"汉化安装完成。\n\n桌面和开始菜单已创建“RizomUV 简体中文版”快捷方式。\n以后请通过该快捷方式启动。";
    return true;
}

bool Uninstall(const std::filesystem::path& rizomDirectory, std::wstring& message) {
    if (IsRizomUVRunning(rizomDirectory)) {
        message = L"RizomUV 正在运行。\n请先关闭 RizomUV，再拆卸汉化。";
        return false;
    }
    for (const auto& shortcut : ShortcutPaths()) {
        std::error_code ignored;
        std::filesystem::remove(shortcut, ignored);
    }
    const auto pluginDirectory = rizomDirectory / kPluginFolder;
    std::error_code error;
    std::filesystem::remove_all(pluginDirectory, error);
    if (!error) std::filesystem::remove_all(rizomDirectory / L"RizomUVChinese", error);
    if (!error) std::filesystem::remove_all(rizomDirectory / L"ChineseLocalizer", error);
    if (!error) std::filesystem::remove_all(rizomDirectory / L"ChineseLauncher.installing", error);
    if (!error) std::filesystem::remove_all(rizomDirectory / L"ChineseLauncher.previous", error);
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
            const std::vector<DWORD> runningProcesses = RizomUVProcessIds(directory);
            if (!runningProcesses.empty()) {
                std::wstring prompt = L"检测到 RizomUV 仍在后台运行（PID ";
                for (size_t index = 0; index < runningProcesses.size(); ++index) {
                    if (index) prompt += L", ";
                    prompt += std::to_wstring(runningProcesses[index]);
                }
                prompt += L"）。\n\n是否强制结束后台进程后继续？\n"
                          L"未保存的 RizomUV 数据可能丢失。";
                if (MessageBoxW(window, prompt.c_str(), kTitle,
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                    return 0;
                if (!TerminateRizomUVProcesses(directory)) {
                    MessageBoxW(window, L"无法结束后台 RizomUV 进程。\n请在任务管理器中结束后重试。",
                                kTitle, MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            std::wstring result;
            const bool ok = id == kInstallButton ? Install(directory, result) : Uninstall(directory, result);
            MessageBoxW(window, result.c_str(), kTitle, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            if (ok) DestroyWindow(window);
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
