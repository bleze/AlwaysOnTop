#include "AlwaysOnTop/Shared.h"

#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kAboutMenuId = 1000;
constexpr UINT kQuitMenuId = 1001;
constexpr UINT kManageMenuId = 1002;
constexpr wchar_t kWindowClassName[] = L"AlwaysOnTopHostWindow";
constexpr wchar_t kMutexName[] = L"Global\\AlwaysOnTop_SingleInstance";
constexpr wchar_t kHookDllName[] = L"AlwaysOnTopHook.dll";
constexpr int kMenuIconSize = 16;
constexpr int kMenuItemHeight = 22;
constexpr wchar_t kAboutMenuText[] = L"&About AlwaysOnTop...";
constexpr wchar_t kManageMenuText[] = L"&Manage Windows...";
constexpr wchar_t kQuitMenuText[] = L"&Quit";

using StartHooksFn = bool (*)();
using StopHooksFn = void (*)();

HINSTANCE g_instance = nullptr;
HWND g_hostWindow = nullptr;
NOTIFYICONDATAW g_trayIcon = {};
HMENU g_trayMenu = nullptr;
HICON g_appIcon = nullptr;
HICON g_quitIcon = nullptr;
HICON g_manageIcon = nullptr;
HMODULE g_hookModule = nullptr;
StartHooksFn g_startHooks = nullptr;
StopHooksFn g_stopHooks = nullptr;

INT_PTR CALLBACK AboutDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ManageDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);

[[nodiscard]] std::wstring GetExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }

    path.resize(length);
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }

    return path.substr(0, slash);
}

[[nodiscard]] bool LoadHookModule()
{
    const std::wstring directory = GetExecutableDirectory();
    if (directory.empty()) {
        return false;
    }

    const std::wstring dllPath = directory + L"\\" + kHookDllName;
    g_hookModule = LoadLibraryW(dllPath.c_str());
    if (g_hookModule == nullptr) {
        return false;
    }

    g_startHooks = reinterpret_cast<StartHooksFn>(
        GetProcAddress(g_hookModule, kStartHooksExport));
    g_stopHooks = reinterpret_cast<StopHooksFn>(
        GetProcAddress(g_hookModule, kStopHooksExport));

    return g_startHooks != nullptr && g_stopHooks != nullptr;
}

void UnloadHookModule()
{
    if (g_stopHooks != nullptr) {
        g_stopHooks();
        g_stopHooks = nullptr;
    }

    if (g_hookModule != nullptr) {
        FreeLibrary(g_hookModule);
        g_hookModule = nullptr;
    }

    g_startHooks = nullptr;
}

template <typename DrawGlyphFn>
[[nodiscard]] HICON CreateGlyphIcon(int size, COLORREF color, DrawGlyphFn drawGlyph)
{
    HDC screenDc = GetDC(nullptr);
    HDC colorDc = CreateCompatibleDC(screenDc);
    HDC maskDc = CreateCompatibleDC(screenDc);

    HBITMAP colorBitmap = CreateCompatibleBitmap(screenDc, size, size);
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
    ReleaseDC(nullptr, screenDc);

    HBITMAP oldColorBitmap = static_cast<HBITMAP>(SelectObject(colorDc, colorBitmap));
    HBITMAP oldMaskBitmap = static_cast<HBITMAP>(SelectObject(maskDc, maskBitmap));

    RECT full = {0, 0, size, size};
    FillRect(maskDc, &full, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FillRect(colorDc, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    HPEN colorPen = CreatePen(PS_SOLID, 2, color);
    HPEN maskPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HPEN oldColorPen = static_cast<HPEN>(SelectObject(colorDc, colorPen));
    HPEN oldMaskPen = static_cast<HPEN>(SelectObject(maskDc, maskPen));

    drawGlyph(colorDc, size);
    drawGlyph(maskDc, size);

    SelectObject(colorDc, oldColorPen);
    SelectObject(maskDc, oldMaskPen);
    DeleteObject(colorPen);
    DeleteObject(maskPen);

    SelectObject(colorDc, oldColorBitmap);
    SelectObject(maskDc, oldMaskBitmap);
    DeleteDC(colorDc);
    DeleteDC(maskDc);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;

    HICON icon = CreateIconIndirect(&iconInfo);

    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);

    return icon;
}

[[nodiscard]] HICON CreateQuitIcon(int size)
{
    return CreateGlyphIcon(size, RGB(196, 43, 28), [](HDC dc, int s) {
        const int inset = s / 4;
        MoveToEx(dc, inset, inset, nullptr);
        LineTo(dc, s - inset, s - inset);
        MoveToEx(dc, s - inset, inset, nullptr);
        LineTo(dc, inset, s - inset);
    });
}

[[nodiscard]] HICON CreateManageIcon(int size)
{
    return CreateGlyphIcon(size, RGB(60, 110, 200), [](HDC dc, int s) {
        const int left = s / 5;
        const int right = s - s / 5;
        for (int y : {s / 4, s / 2, (s * 3) / 4}) {
            MoveToEx(dc, left, y, nullptr);
            LineTo(dc, right, y);
        }
    });
}

void DrawMenuIconAndText(const DRAWITEMSTRUCT& item, HICON icon, const wchar_t* text)
{
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    HBRUSH background = CreateSolidBrush(GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);

    const int iconX = item.rcItem.left + 6;
    const int iconY = item.rcItem.top +
        (item.rcItem.bottom - item.rcItem.top - kMenuIconSize) / 2;
    if (icon != nullptr) {
        DrawIconEx(item.hDC, iconX, iconY, icon, kMenuIconSize, kMenuIconSize, 0, nullptr, DI_NORMAL);
    }

    RECT textRect = item.rcItem;
    textRect.left += 6 + kMenuIconSize + 8;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
    HFONT oldFont = static_cast<HFONT>(
        SelectObject(item.hDC, GetStockObject(DEFAULT_GUI_FONT)));
    DrawTextW(item.hDC, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    SelectObject(item.hDC, oldFont);
}

void ShowStartupError()
{
    MessageBoxW(
        nullptr,
        L"Failed to load AlwaysOnTopHook.dll or install system hooks.\n\n"
        L"Make sure AlwaysOnTopHook.dll is in the same folder as AlwaysOnTop.exe.",
        L"AlwaysOnTop",
        MB_ICONERROR | MB_OK);
}

void ShowAboutDialog()
{
    DialogBoxW(g_instance, MAKEINTRESOURCEW(IDD_ABOUT), g_hostWindow, AboutDialogProc);
}

void ShowManageDialog()
{
    DialogBoxW(g_instance, MAKEINTRESOURCEW(IDD_MANAGE), g_hostWindow, ManageDialogProc);
}

INT_PTR CALLBACK AboutDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM)
{
    switch (message) {
    case WM_INITDIALOG:
        SendDlgItemMessageW(
            dialog,
            IDC_ABOUT_TITLE,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
            TRUE);
        SetWindowPos(
            dialog,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

[[nodiscard]] bool IsExcludedWindowClass(HWND hwnd)
{
    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) == 0) {
        return true;
    }

    return wcscmp(className, L"Shell_TrayWnd") == 0 ||
        wcscmp(className, L"Progman") == 0 ||
        wcscmp(className, L"WorkerW") == 0;
}

[[nodiscard]] bool IsPinned(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

[[nodiscard]] bool IsHidden(HWND hwnd)
{
    return GetPropW(hwnd, kHiddenProp) != nullptr;
}

[[nodiscard]] std::wstring BuildStateText(bool pinned, bool hidden)
{
    if (pinned && hidden) {
        return L"Pinned + Hidden";
    }
    if (pinned) {
        return L"Pinned";
    }
    if (hidden) {
        return L"Hidden";
    }
    return L"";
}

BOOL CALLBACK CollectManagedWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* windows = reinterpret_cast<std::vector<HWND>*>(lParam);

    if (GetParent(hwnd) != nullptr || IsExcludedWindowClass(hwnd)) {
        return TRUE;
    }

    if (IsHidden(hwnd)) {
        windows->push_back(hwnd);
        return TRUE;
    }

    if (!IsPinned(hwnd) || !IsWindowVisible(hwnd) || GetSystemMenu(hwnd, FALSE) == nullptr) {
        return TRUE;
    }

    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) == 0) {
        return TRUE;
    }

    windows->push_back(hwnd);
    return TRUE;
}

void PopulateManageList(HWND dialog)
{
    HWND listView = GetDlgItem(dialog, IDC_MANAGE_LIST);
    ListView_DeleteAllItems(listView);

    std::vector<HWND> windows;
    EnumWindows(CollectManagedWindowsProc, reinterpret_cast<LPARAM>(&windows));

    int index = 0;
    for (HWND hwnd : windows) {
        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));

        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = index;
        item.pszText = title;
        item.lParam = reinterpret_cast<LPARAM>(hwnd);
        const int insertedIndex = ListView_InsertItem(listView, &item);
        if (insertedIndex < 0) {
            continue;
        }

        std::wstring state = BuildStateText(IsPinned(hwnd), IsHidden(hwnd));
        ListView_SetItemText(listView, insertedIndex, 1, state.data());
        ++index;
    }
}

void RestoreWindow(HWND hwnd)
{
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    if (IsPinned(hwnd)) {
        SetWindowPos(
            hwnd,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if (IsHidden(hwnd)) {
        ShowWindow(hwnd, SW_SHOW);
        RemovePropW(hwnd, kHiddenProp);
    }
}

void RestoreSelectedWindows(HWND dialog)
{
    HWND listView = GetDlgItem(dialog, IDC_MANAGE_LIST);

    int index = -1;
    while ((index = ListView_GetNextItem(listView, index, LVNI_SELECTED)) != -1) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = index;
        ListView_GetItem(listView, &item);
        RestoreWindow(reinterpret_cast<HWND>(item.lParam));
    }

    PopulateManageList(dialog);
}

void RestoreAllWindows(HWND dialog)
{
    HWND listView = GetDlgItem(dialog, IDC_MANAGE_LIST);
    const int count = ListView_GetItemCount(listView);

    for (int index = 0; index < count; ++index) {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = index;
        ListView_GetItem(listView, &item);
        RestoreWindow(reinterpret_cast<HWND>(item.lParam));
    }

    PopulateManageList(dialog);
}

void SetUpManageListColumns(HWND dialog)
{
    HWND listView = GetDlgItem(dialog, IDC_MANAGE_LIST);
    ListView_SetExtendedListViewStyle(listView, LVS_EX_FULLROWSELECT);

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH;

    column.pszText = const_cast<LPWSTR>(L"Window");
    column.cx = 180;
    ListView_InsertColumn(listView, 0, &column);

    column.pszText = const_cast<LPWSTR>(L"State");
    column.cx = 90;
    ListView_InsertColumn(listView, 1, &column);
}

INT_PTR CALLBACK ManageDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM)
{
    switch (message) {
    case WM_INITDIALOG:
        SetWindowPos(
            dialog,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetUpManageListColumns(dialog);
        PopulateManageList(dialog);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_MANAGE_RESTORE_SELECTED:
            RestoreSelectedWindows(dialog);
            return TRUE;
        case IDC_MANAGE_RESTORE_ALL:
            RestoreAllWindows(dialog);
            return TRUE;
        case IDC_MANAGE_REFRESH:
            PopulateManageList(dialog);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(dialog, IDOK);
            return TRUE;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

void RemoveTrayIcon()
{
    if (g_hostWindow != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
    }
}

void ShowTrayMenu()
{
    POINT cursor = {};
    GetCursorPos(&cursor);

    SetForegroundWindow(g_hostWindow);
    TrackPopupMenu(
        g_trayMenu,
        TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
        cursor.x,
        cursor.y,
        0,
        g_hostWindow,
        nullptr);
    PostMessageW(g_hostWindow, WM_NULL, 0, 0);
}

LRESULT CALLBACK HostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case kTrayCallbackMessage:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kAboutMenuId:
            ShowAboutDialog();
            return 0;
        case kManageMenuId:
            ShowManageDialog();
            return 0;
        case kQuitMenuId:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure != nullptr && measure->CtlType == ODT_MENU) {
            measure->itemWidth = 180;
            measure->itemHeight = kMenuItemHeight;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem != nullptr && drawItem->CtlType == ODT_MENU) {
            switch (drawItem->itemID) {
            case kManageMenuId:
                DrawMenuIconAndText(*drawItem, g_manageIcon, kManageMenuText);
                return TRUE;
            case kAboutMenuId:
                DrawMenuIconAndText(*drawItem, g_appIcon, kAboutMenuText);
                return TRUE;
            case kQuitMenuId:
                DrawMenuIconAndText(*drawItem, g_quitIcon, kQuitMenuText);
                return TRUE;
            default:
                break;
            }
        }
        break;
    }

    case WM_DESTROY:
        RemoveTrayIcon();
        if (g_trayMenu != nullptr) {
            DestroyMenu(g_trayMenu);
            g_trayMenu = nullptr;
        }
        if (g_appIcon != nullptr) {
            DestroyIcon(g_appIcon);
            g_appIcon = nullptr;
        }
        if (g_quitIcon != nullptr) {
            DestroyIcon(g_quitIcon);
            g_quitIcon = nullptr;
        }
        if (g_manageIcon != nullptr) {
            DestroyIcon(g_manageIcon);
            g_manageIcon = nullptr;
        }
        UnloadHookModule();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

[[nodiscard]] bool RegisterHostWindowClass()
{
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = HostWindowProc;
    windowClass.hInstance = g_instance;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    return RegisterClassW(&windowClass) != 0;
}

[[nodiscard]] bool CreateHostWindow()
{
    g_hostWindow = CreateWindowExW(
        0,
        kWindowClassName,
        L"AlwaysOnTop Host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        g_instance,
        nullptr);

    return g_hostWindow != nullptr;
}

[[nodiscard]] bool CreateTrayIcon()
{
    g_appIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (g_appIcon == nullptr) {
        g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    g_quitIcon = CreateQuitIcon(kMenuIconSize);
    g_manageIcon = CreateManageIcon(kMenuIconSize);

    g_trayMenu = CreatePopupMenu();
    if (g_trayMenu == nullptr) {
        return false;
    }

    AppendMenuW(g_trayMenu, MF_OWNERDRAW, kManageMenuId, nullptr);
    AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_trayMenu, MF_OWNERDRAW, kAboutMenuId, nullptr);
    AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_trayMenu, MF_OWNERDRAW, kQuitMenuId, nullptr);

    g_trayIcon = {};
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = g_hostWindow;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = kTrayCallbackMessage;
    g_trayIcon.hIcon = g_appIcon;
    wcscpy_s(g_trayIcon.szTip, L"AlwaysOnTop - right-click title bars to pin or hide windows");

    return Shell_NotifyIconW(NIM_ADD, &g_trayIcon) != FALSE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    g_instance = instance;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(
            nullptr,
            L"AlwaysOnTop is already running.",
            L"AlwaysOnTop",
            MB_ICONINFORMATION | MB_OK);
        if (mutex != nullptr) {
            CloseHandle(mutex);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&commonControls);

    if (!RegisterHostWindowClass() || !CreateHostWindow() || !CreateTrayIcon()) {
        CloseHandle(mutex);
        return 1;
    }

    if (!LoadHookModule() || !g_startHooks()) {
        ShowStartupError();
        DestroyWindow(g_hostWindow);
        CloseHandle(mutex);
        return 1;
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
