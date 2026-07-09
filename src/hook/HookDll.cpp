#include "AlwaysOnTop/Shared.h"

#include <string>

namespace {

HINSTANCE g_instance = nullptr;
HHOOK g_shellHook = nullptr;
HHOOK g_getMessageHook = nullptr;

[[nodiscard]] bool IsTopLevelWindow(HWND hwnd)
{
    return GetParent(hwnd) == nullptr;
}

[[nodiscard]] bool ShouldProcessWindow(HWND hwnd)
{
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }

    if (!IsWindowVisible(hwnd) || !IsTopLevelWindow(hwnd)) {
        return false;
    }

    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) == 0) {
        return false;
    }

    if (wcscmp(className, L"Shell_TrayWnd") == 0 ||
        wcscmp(className, L"Progman") == 0 ||
        wcscmp(className, L"WorkerW") == 0) {
        return false;
    }

    return GetSystemMenu(hwnd, FALSE) != nullptr;
}

[[nodiscard]] bool IsAlwaysOnTop(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

void ToggleAlwaysOnTop(HWND hwnd)
{
    const bool currentlyOnTop = IsAlwaysOnTop(hwnd);
    SetWindowPos(
        hwnd,
        currentlyOnTop ? HWND_NOTOPMOST : HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void UpdateMenuCheckState(HWND hwnd, HMENU menu)
{
    const UINT flags = IsAlwaysOnTop(hwnd) ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(menu, kAlwaysOnTopCommandId, MF_BYCOMMAND | flags);
}

void EnsureAlwaysOnTopMenu(HWND hwnd)
{
    if (!ShouldProcessWindow(hwnd)) {
        return;
    }

    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (menu == nullptr) {
        return;
    }

    if (GetPropW(hwnd, kMenuInjectedProp) == nullptr) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kAlwaysOnTopCommandId, L"Always on &Top");
        SetPropW(hwnd, kMenuInjectedProp, reinterpret_cast<HANDLE>(1));
    }

    UpdateMenuCheckState(hwnd, menu);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
{
    EnsureAlwaysOnTopMenu(hwnd);
    return TRUE;
}

LRESULT CALLBACK ShellHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0) {
        if (code == HSHELL_WINDOWCREATED) {
            EnsureAlwaysOnTopMenu(reinterpret_cast<HWND>(wParam));
        } else if (code == HSHELL_WINDOWDESTROYED) {
            RemovePropW(reinterpret_cast<HWND>(wParam), kMenuInjectedProp);
        }
    }

    return CallNextHookEx(g_shellHook, code, wParam, lParam);
}

LRESULT CALLBACK GetMessageHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && wParam == PM_REMOVE) {
        auto* message = reinterpret_cast<MSG*>(lParam);
        if (message == nullptr) {
            return CallNextHookEx(g_getMessageHook, code, wParam, lParam);
        }

        HWND hwnd = message->hwnd;
        if (message->message == WM_INITMENUPOPUP) {
            HMENU menu = reinterpret_cast<HMENU>(message->wParam);
            HMENU systemMenu = GetSystemMenu(hwnd, FALSE);
            if (systemMenu != nullptr && menu == systemMenu) {
                EnsureAlwaysOnTopMenu(hwnd);
            }
        } else if (message->message == WM_SYSCOMMAND &&
                   (message->wParam & 0xFFF0) == kAlwaysOnTopCommandId) {
            ToggleAlwaysOnTop(hwnd);
            message->message = WM_NULL;
        }
    }

    return CallNextHookEx(g_getMessageHook, code, wParam, lParam);
}

} // namespace

extern "C" __declspec(dllexport) bool Aot_Start()
{
    if (g_instance == nullptr) {
        return false;
    }

    if (g_shellHook == nullptr) {
        g_shellHook = SetWindowsHookExW(WH_SHELL, ShellHookProc, g_instance, 0);
    }

    if (g_getMessageHook == nullptr) {
        g_getMessageHook = SetWindowsHookExW(WH_GETMESSAGE, GetMessageHookProc, g_instance, 0);
    }

    EnumWindows(EnumWindowsProc, 0);
    return g_shellHook != nullptr && g_getMessageHook != nullptr;
}

extern "C" __declspec(dllexport) void Aot_Stop()
{
    if (g_shellHook != nullptr) {
        UnhookWindowsHookEx(g_shellHook);
        g_shellHook = nullptr;
    }

    if (g_getMessageHook != nullptr) {
        UnhookWindowsHookEx(g_getMessageHook);
        g_getMessageHook = nullptr;
    }
}

void Aot_SetInstance(HINSTANCE instance)
{
    g_instance = instance;
}