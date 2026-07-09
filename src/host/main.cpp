#include "AlwaysOnTop/Shared.h"

#include "resource.h"

#include <shellapi.h>

#include <string>

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kAboutMenuId = 1000;
constexpr UINT kQuitMenuId = 1001;
constexpr wchar_t kWindowClassName[] = L"AlwaysOnTopHostWindow";
constexpr wchar_t kMutexName[] = L"Global\\AlwaysOnTop_SingleInstance";
constexpr wchar_t kHookDllName[] = L"AlwaysOnTopHook.dll";
constexpr int kMenuIconSize = 16;
constexpr int kMenuItemHeight = 22;
constexpr wchar_t kAboutMenuText[] = L"&About AlwaysOnTop...";
constexpr wchar_t kQuitMenuText[] = L"&Quit";

using StartHooksFn = bool (*)();
using StopHooksFn = void (*)();

HINSTANCE g_instance = nullptr;
HWND g_hostWindow = nullptr;
NOTIFYICONDATAW g_trayIcon = {};
HMENU g_trayMenu = nullptr;
HICON g_appIcon = nullptr;
HICON g_quitIcon = nullptr;
HMODULE g_hookModule = nullptr;
StartHooksFn g_startHooks = nullptr;
StopHooksFn g_stopHooks = nullptr;

INT_PTR CALLBACK AboutDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);

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

[[nodiscard]] HICON CreateQuitIcon(int size)
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

    HPEN colorPen = CreatePen(PS_SOLID, 2, RGB(196, 43, 28));
    HPEN maskPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HPEN oldColorPen = static_cast<HPEN>(SelectObject(colorDc, colorPen));
    HPEN oldMaskPen = static_cast<HPEN>(SelectObject(maskDc, maskPen));

    const int inset = size / 4;
    MoveToEx(colorDc, inset, inset, nullptr);
    LineTo(colorDc, size - inset, size - inset);
    MoveToEx(colorDc, size - inset, inset, nullptr);
    LineTo(colorDc, inset, size - inset);

    MoveToEx(maskDc, inset, inset, nullptr);
    LineTo(maskDc, size - inset, size - inset);
    MoveToEx(maskDc, size - inset, inset, nullptr);
    LineTo(maskDc, inset, size - inset);

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

    g_trayMenu = CreatePopupMenu();
    if (g_trayMenu == nullptr) {
        return false;
    }

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
    wcscpy_s(g_trayIcon.szTip, L"AlwaysOnTop - right-click title bars to pin windows");

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