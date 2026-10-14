#include "rizomuv_localizer/native_menu_localizer.h"

#include "rizomuv_localizer/translation_dictionary.h"
#include "rizomuv_localizer/runtime_log.h"

#include <shellapi.h>

#include <atomic>
#include <climits>

namespace rizomuv::localizer {
namespace {

size_t TranslateMenuTree(HMENU menu, const TranslationDictionary& dictionary, unsigned depth = 0) {
    if (!menu || depth > 32) return 0;
    size_t translatedCount = 0;
    const int count = GetMenuItemCount(menu);
    for (int position = 0; position < count; ++position) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_FTYPE;
        wchar_t buffer[4096]; // GetMenuItemInfo supplies the returned characters.
        info.dwTypeData = buffer;
        info.cch = static_cast<UINT>(std::size(buffer) - 1);
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &info)) continue;
        if (info.hSubMenu) translatedCount += TranslateMenuTree(info.hSubMenu, dictionary, depth + 1);
        if (info.fType & (MFT_OWNERDRAW | MFT_SEPARATOR)) continue;
        if (info.cch >= std::size(buffer) - 1) continue;
        const std::wstring_view source(buffer, info.cch);
        const size_t shortcutOffset = source.find(L'\t');
        const auto label = source.substr(0, shortcutOffset);
        if (const std::wstring* translated = dictionary.Find(label)) {
            if (*translated == label) continue;
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
    }
    return translatedCount;
}

struct MenuContext { DWORD pid; const TranslationDictionary* dictionary; size_t count; };

constexpr wchar_t kAuthorText[] = L"Bilibili 神说要凑数汉化";
constexpr wchar_t kAuthorUrl[] =
    L"https://space.bilibili.com/281243426?spm_id_from=333.1007.0.0";
constexpr wchar_t kGitHubText[] = L"GitHub 仓库";
constexpr wchar_t kGitHubUrl[] = L"https://github.com/iillya/RizomUVChinese";
constexpr COLORREF kLinkTextColor = RGB(102, 170, 255);
constexpr COLORREF kLinkTransparentColor = RGB(1, 2, 3);
constexpr DWORD kLinkTransparentDibPixel = 0x00010203;
constexpr wchar_t const* kTopMenuLabels[] = {
    L"文件", L"编辑", L"选择", L"展开", L"变换", L"排布",
    L"组", L"饰条纹理表", L"窗口", L"脚本", L"帮助"
};
constexpr int kMenuLeftInsetAt45 = 8;
constexpr int kMenuItemPaddingAt45 = 16;
constexpr int kCreditsFineTuneAt45 = 8;
constexpr int kMenuBandHeightAt96Dpi = 27;

std::atomic<bool> g_creditsStarted{false};
std::atomic<bool> g_positionPending{false};
HMODULE g_creditsModule = nullptr;
HWND g_mainWindow = nullptr;
HWND g_authorWindow = nullptr;
HWND g_gitHubWindow = nullptr;
HWINEVENTHOOK g_locationHook = nullptr;
HWINEVENTHOOK g_minimizeHook = nullptr;
HWINEVENTHOOK g_visibilityHook = nullptr;
int g_authorX = INT_MIN;
int g_authorY = INT_MIN;
int g_authorWidth = 0;
int g_gitHubX = INT_MIN;
int g_gitHubWidth = 0;
int g_menuBandHeight = 0;
int g_authorHeight = 0;
int g_menuLayoutWidth = 0;
constexpr UINT_PTR kCreditsStartupRetry = 1;
ULONGLONG g_creditsRetryDeadline = 0;
DWORD g_authorUpdateError = ERROR_SUCCESS;
DWORD g_gitHubUpdateError = ERROR_SUCCESS;

struct MainWindowCandidate { HWND window = nullptr; LONG64 area = 0; };

bool IsMainFrameStyle(LONG_PTR style, LONG_PTR extendedStyle) {
    // Splash screens and floating panels can be the first large visible
    // windows. The credits must belong to the resizable application frame.
    return (style & WS_CHILD) == 0 &&
        (style & WS_CAPTION) == WS_CAPTION && (style & WS_SYSMENU) != 0 &&
        (style & (WS_THICKFRAME | WS_MAXIMIZEBOX)) != 0 &&
        (extendedStyle & WS_EX_TOOLWINDOW) == 0;
}

bool IsFullScreenMainFrame(LONG_PTR style, LONG_PTR extendedStyle,
                           const RECT& bounds, const RECT& monitor,
                           std::wstring_view title) {
    return (style & WS_POPUP) != 0 && (style & WS_CHILD) == 0 &&
        (extendedStyle & WS_EX_TOOLWINDOW) == 0 &&
        EqualRect(&bounds, &monitor) && title.find(L"RizomUV") != title.npos;
}

BOOL CALLBACK FindMainRizomWindow(HWND window, LPARAM value) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return TRUE;
    const LONG width = bounds.right - bounds.left;
    const LONG height = bounds.bottom - bounds.top;
    if (width < 500 || height < 300) return TRUE;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!IsMainFrameStyle(style, extendedStyle)) {
        // Borderless fullscreen has no caption or resize frame. Keep it
        // eligible without accepting ordinary startup or floating windows.
        MONITORINFO monitor{sizeof(MONITORINFO), {}, {}, 0};
        wchar_t title[512]{};
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &monitor) ||
            !EqualRect(&bounds, &monitor.rcMonitor)) return TRUE;
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        if (!IsFullScreenMainFrame(style, extendedStyle, bounds, monitor.rcMonitor, title))
            return TRUE;
    }
    const LONG64 area = static_cast<LONG64>(width) * height;
    auto* candidate = reinterpret_cast<MainWindowCandidate*>(value);
    if (area > candidate->area) {
        candidate->window = window;
        candidate->area = area;
    }
    return TRUE;
}

HFONT CreateLinkFont(int height) {
    return CreateFontW(-MulDiv(20, height, 45), 0, 0, 0, FW_NORMAL,
        FALSE, TRUE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
        L"Microsoft YaHei UI");
}

int MeasureTopMenuLayoutWidth(HDC dc, HFONT font, int height) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    int width = MulDiv(kMenuLeftInsetAt45, height, 45);
    const int itemPadding = MulDiv(kMenuItemPaddingAt45, height, 45);
    for (const wchar_t* label : kTopMenuLabels) {
        SIZE size{};
        if (GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size))
            width += size.cx + itemPadding;
    }
    SelectObject(dc, oldFont);
    return width + MulDiv(kCreditsFineTuneAt45, height, 45);
}

int CalculateTextVerticalOffset(HDC referenceDc, HFONT font,
                                const wchar_t* text, int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(referenceDc, &bitmapInfo, DIB_RGB_COLORS,
                                      &pixels, nullptr, 0);
    HDC memoryDc = CreateCompatibleDC(referenceDc);
    if (!bitmap || !memoryDc || !pixels) {
        if (memoryDc) DeleteDC(memoryDc);
        if (bitmap) DeleteObject(bitmap);
        return 0;
    }
    HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(SelectObject(memoryDc, bitmap));
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(memoryDc, font));
    RECT rect{0, 0, width, height};
    HBRUSH background = CreateSolidBrush(kLinkTransparentColor);
    FillRect(memoryDc, &rect, background);
    DeleteObject(background);
    SetBkMode(memoryDc, TRANSPARENT);
    SetTextColor(memoryDc, kLinkTextColor);
    DrawTextW(memoryDc, text, -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    GdiFlush(); // DIB pixels are read directly below, after queued GDI writes.
    const auto* bitmapPixels = static_cast<const DWORD*>(pixels);
    int inkTop = height;
    int inkBottom = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((bitmapPixels[y * width + x] & 0x00FFFFFF) !=
                kLinkTransparentDibPixel) {
                if (y < inkTop) inkTop = y;
                if (y > inkBottom) inkBottom = y;
            }
        }
    }
    SelectObject(memoryDc, oldFont);
    SelectObject(memoryDc, oldBitmap);
    DeleteDC(memoryDc);
    DeleteObject(bitmap);
    return inkBottom < inkTop ? 0 : (height - 1 - inkTop - inkBottom) / 2;
}

bool RenderLinkWindow(HWND window, const wchar_t* text,
                      int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = screenDc ? CreateDIBSection(
        screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    if (!screenDc || !memoryDc || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (memoryDc) DeleteDC(memoryDc);
        if (screenDc) ReleaseDC(nullptr, screenDc);
        return false;
    }
    HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(SelectObject(memoryDc, bitmap));
    HFONT font = CreateLinkFont(height);
    if (!font) {
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(memoryDc, font));
    RECT textRect{0, 0, width, height};
    HBRUSH background = CreateSolidBrush(kLinkTransparentColor);
    FillRect(memoryDc, &textRect, background);
    DeleteObject(background);
    SetBkMode(memoryDc, TRANSPARENT);
    SetTextColor(memoryDc, kLinkTextColor);
    OffsetRect(&textRect, 0, CalculateTextVerticalOffset(
        memoryDc, font, text, width, height));
    DrawTextW(memoryDc, text, -1, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    GdiFlush();
    auto* bitmapPixels = static_cast<DWORD*>(pixels);
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t index = 0; index < pixelCount; ++index) {
        bitmapPixels[index] = ((bitmapPixels[index] & 0x00FFFFFF) !=
                               kLinkTransparentDibPixel)
            ? (bitmapPixels[index] | 0xFF000000) : 0x01000000;
    }
    POINT destination{x, y};
    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL updated = UpdateLayeredWindow(window, screenDc, &destination,
        &size, memoryDc, &source, 0, &blend, ULW_ALPHA);
    const DWORD updateError = updated ? ERROR_SUCCESS : GetLastError();
    SelectObject(memoryDc, oldFont);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
    // Visibility is committed by the caller only after both surfaces exist.
    // Keep the error observable without logging every window-move event.
    DWORD& lastUpdateError = window == g_authorWindow ? g_authorUpdateError : g_gitHubUpdateError;
    const DWORD reportedError = !updated && !updateError ? ERROR_GEN_FAILURE : updateError;
    if (!updated && reportedError != lastUpdateError)
        RuntimeLog(L"署名链接图层更新失败，错误码：" + std::to_wstring(reportedError));
    lastUpdateError = reportedError;
    return updated != FALSE;
}

void PositionCreditWindows() {
    if (!g_authorWindow || !g_gitHubWindow) return;
    if (!IsWindow(g_mainWindow)) {
        MainWindowCandidate candidate;
        EnumWindows(FindMainRizomWindow, reinterpret_cast<LPARAM>(&candidate));
        g_mainWindow = candidate.window;
    }
    if (!g_mainWindow) {
        ShowWindow(g_authorWindow, SW_HIDE);
        ShowWindow(g_gitHubWindow, SW_HIDE);
        return;
    }
    if (!IsWindowVisible(g_mainWindow) || IsIconic(g_mainWindow)) {
        ShowWindow(g_authorWindow, SW_HIDE);
        ShowWindow(g_gitHubWindow, SW_HIDE);
        return;
    }
    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(g_mainWindow, &client) ||
        !ClientToScreen(g_mainWindow, &origin)) return;
    g_menuBandHeight = MulDiv(kMenuBandHeightAt96Dpi,
        static_cast<int>(GetDpiForWindow(g_mainWindow)), 96);
    const int height = g_menuBandHeight;
    const int margin = MulDiv(8, height, 45);
    const int gap = MulDiv(14, height, 45);
    if (height != g_authorHeight || !g_authorWidth || !g_gitHubWidth ||
        !g_menuLayoutWidth) {
        HDC dc = GetDC(nullptr);
        if (!dc) return;
        HFONT font = CreateLinkFont(height);
        if (!font) {
            ReleaseDC(nullptr, dc);
            return;
        }
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
        SIZE size{};
        GetTextExtentPoint32W(dc, kAuthorText,
            static_cast<int>(std::size(kAuthorText) - 1), &size);
        g_authorWidth = size.cx;
        GetTextExtentPoint32W(dc, kGitHubText,
            static_cast<int>(std::size(kGitHubText) - 1), &size);
        g_gitHubWidth = size.cx;
        SelectObject(dc, oldFont);
        g_menuLayoutWidth = MeasureTopMenuLayoutWidth(dc, font, height);
        DeleteObject(font);
        ReleaseDC(nullptr, dc);
    }
    if (g_authorWidth <= 0) g_authorWidth = MulDiv(210, height, 45);
    if (g_gitHubWidth <= 0) g_gitHubWidth = MulDiv(100, height, 45);
    const int authorX = origin.x + g_menuLayoutWidth;
    const int gitHubX = authorX + g_authorWidth + gap;
    if (gitHubX + g_gitHubWidth + margin > origin.x + client.right) {
        ShowWindow(g_authorWindow, SW_HIDE);
        ShowWindow(g_gitHubWindow, SW_HIDE);
        return;
    }
    const int y = origin.y;
    if (authorX == g_authorX && gitHubX == g_gitHubX && y == g_authorY &&
        height == g_authorHeight && IsWindowVisible(g_authorWindow) &&
        IsWindowVisible(g_gitHubWindow)) return;
    const bool needsRender = height != g_authorHeight ||
                             !IsWindowVisible(g_authorWindow) ||
                             !IsWindowVisible(g_gitHubWindow);
    if (needsRender) {
        const bool authorReady = RenderLinkWindow(g_authorWindow, kAuthorText,
            authorX, y, g_authorWidth, height);
        const bool gitHubReady = RenderLinkWindow(g_gitHubWindow, kGitHubText,
            gitHubX, y, g_gitHubWidth, height);
        if (!authorReady || !gitHubReady) {
            ShowWindow(g_authorWindow, SW_HIDE);
            ShowWindow(g_gitHubWindow, SW_HIDE);
            return;
        }
        ShowWindow(g_authorWindow, SW_SHOWNOACTIVATE);
        ShowWindow(g_gitHubWindow, SW_SHOWNOACTIVATE);
    } else {
        SetWindowPos(g_authorWindow, HWND_TOP, authorX, y, g_authorWidth, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(g_gitHubWindow, HWND_TOP, gitHubX, y, g_gitHubWidth, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    g_authorX = authorX;
    g_gitHubX = gitHubX;
    g_authorY = y;
    g_authorHeight = height;
}

void CALLBACK CreditEventCallback(HWINEVENTHOOK, DWORD, HWND window,
                                   LONG object, LONG child, DWORD, DWORD) {
    // SetWinEventHook already scopes events to this process. Child-control
    // scrolling, caret movement and tooltips cannot move the top-level band.
    if (window != g_mainWindow || child != CHILDID_SELF ||
        (object != OBJID_WINDOW && object != OBJID_CLIENT) || !g_authorWindow) return;
    if (!g_positionPending.exchange(true))
        if (!PostMessageW(g_authorWindow, WM_APP + 1, 0, 0)) g_positionPending.store(false);
}

LRESULT CALLBACK CreditWindowProcedure(HWND window, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_TIMER:
        if (wParam != kCreditsStartupRetry) break;
        PositionCreditWindows();
        if ((IsWindowVisible(g_authorWindow) && IsWindowVisible(g_gitHubWindow)) ||
            GetTickCount64() >= g_creditsRetryDeadline) {
            KillTimer(window, kCreditsStartupRetry);
            if (IsWindowVisible(g_authorWindow) && IsWindowVisible(g_gitHubWindow))
                RuntimeLog(L"Bilibili 署名和 GitHub 链接已绘制并显示");
            else
                RuntimeLog(L"署名链接启动重试已结束，后续随主窗口事件重新定位");
        }
        return 0;
    case WM_APP + 1:
        g_positionPending.store(false);
        PositionCreditWindows();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        ShowWindow(g_authorWindow, SW_HIDE);
        ShowWindow(g_gitHubWindow, SW_HIDE);
        g_menuBandHeight = 0;
        g_menuLayoutWidth = 0;
        PositionCreditWindows();
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONUP:
        ShellExecuteW(nullptr, L"open",
            window == g_authorWindow ? kAuthorUrl : kGitHubUrl,
            nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    case WM_DESTROY:
        if (window == g_authorWindow) PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI RunCreditWindows(void*) {
    for (int pass = 0; pass < 300 && !g_mainWindow; ++pass) {
        MainWindowCandidate candidate;
        EnumWindows(FindMainRizomWindow, reinterpret_cast<LPARAM>(&candidate));
        g_mainWindow = candidate.window;
        if (!g_mainWindow) Sleep(100);
    }
    if (!g_mainWindow) {
        RuntimeLog(L"署名链接未找到可用主窗口，已结束本次启动检测");
        g_creditsStarted.store(false);
        return 1;
    }
    wchar_t className[128]{};
    GetClassNameW(g_mainWindow, className, static_cast<int>(std::size(className)));
    RuntimeLog(L"署名链接已绑定主窗口类：" + std::wstring(className));
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = CreditWindowProcedure;
    windowClass.hInstance = g_creditsModule;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    windowClass.lpszClassName = L"RizomUVChineseAuthorLink";
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        RuntimeLog(L"署名链接窗口类注册失败，错误码：" + std::to_wstring(GetLastError()));
        g_creditsStarted.store(false);
        return 2;
    }
    const DWORD style = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    g_authorWindow = CreateWindowExW(style, windowClass.lpszClassName,
        kAuthorText, WS_POPUP, 0, 0, 1, 1,
        g_mainWindow, nullptr, g_creditsModule, nullptr);
    g_gitHubWindow = CreateWindowExW(style, windowClass.lpszClassName,
        kGitHubText, WS_POPUP, 0, 0, 1, 1,
        g_mainWindow, nullptr, g_creditsModule, nullptr);
    if (!g_authorWindow || !g_gitHubWindow) {
        RuntimeLog(L"署名链接窗口创建失败，错误码：" + std::to_wstring(GetLastError()));
        if (g_authorWindow) DestroyWindow(g_authorWindow);
        if (g_gitHubWindow) DestroyWindow(g_gitHubWindow);
        g_authorWindow = g_gitHubWindow = nullptr;
        g_creditsStarted.store(false);
        return 2;
    }
    g_locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_LOCATIONCHANGE, nullptr, CreditEventCallback,
        GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    g_minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,
        EVENT_SYSTEM_MINIMIZEEND, nullptr, CreditEventCallback,
        GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    g_visibilityHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
        nullptr, CreditEventCallback, GetCurrentProcessId(), 0,
        WINEVENT_OUTOFCONTEXT);
    PositionCreditWindows();
    if (IsWindowVisible(g_authorWindow) && IsWindowVisible(g_gitHubWindow))
        RuntimeLog(L"Bilibili 署名和 GitHub 链接已绘制并显示");
    else {
        // Startup can briefly lack a renderable surface. Retry for five seconds
        // only; successful initialization returns to event-driven positioning.
        g_creditsRetryDeadline = GetTickCount64() + 5000;
        if (!SetTimer(g_authorWindow, kCreditsStartupRetry, 500, nullptr))
            RuntimeLog(L"署名链接启动重试定时器创建失败");
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    for (auto hook : {g_locationHook, g_minimizeHook, g_visibilityHook})
        if (hook) UnhookWinEvent(hook);
    return 0;
}

} // namespace

size_t TranslateNativeMenus(DWORD processId, const TranslationDictionary& dictionary) {
    MenuContext context{processId, &dictionary, 0};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto* context = reinterpret_cast<MenuContext*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != context->pid) return TRUE;
        if (HMENU menu = GetMenu(window)) {
            const size_t changed = TranslateMenuTree(menu, *context->dictionary);
            context->count += changed;
            if (changed) DrawMenuBar(window);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.count;
}

bool StartMenuBarCredits(HMODULE runtimeModule) {
    bool expected = false;
    if (!g_creditsStarted.compare_exchange_strong(expected, true)) return true;
    g_creditsModule = runtimeModule;
    HANDLE thread = CreateThread(nullptr, 0, RunCreditWindows, nullptr, 0, nullptr);
    if (!thread) {
        g_creditsStarted.store(false);
        return false;
    }
    CloseHandle(thread);
    return true;
}

} // namespace rizomuv::localizer
