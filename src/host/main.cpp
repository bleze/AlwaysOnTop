#include "AlwaysOnTop/Shared.h"

#include "resource.h"

#include <shellapi.h>

#include <string>

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kAboutMenuId = 1000;
constexpr UINT kExitMenuId = 1001;
constexpr wchar_t kWindowClassName[] = L"AlwaysOnTopHostWindow";
constexpr wchar_t kMutexName[] = L"Global\\AlwaysOnTop_SingleInstance";
constexpr wchar_t kHookDllName[] = L"AlwaysOnTopHook.dll";

using StartHooksFn = bool (*)();
using StopHooksFn = void (*)();

HINSTANCE g_instance = nullptr;
HWND g_hostWindow = nullptr;
NOTIFYICONDATAW g_trayIcon = {};
HMENU g_trayMenu = nullptr;
HICON g_appIcon = nullptr;
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
        case kExitMenuId:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;

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
    g_trayMenu = CreatePopupMenu();
    if (g_trayMenu == nullptr) {
        return false;
    }

    AppendMenuW(g_trayMenu, MF_STRING, kAboutMenuId, L"&About AlwaysOnTop...");
    AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_trayMenu, MF_STRING, kExitMenuId, L"E&xit");

    g_appIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (g_appIcon == nullptr) {
        g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

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