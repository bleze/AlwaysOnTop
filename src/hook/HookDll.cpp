#include "AlwaysOnTop/Shared.h"

#include <string>

namespace {

HINSTANCE g_instance = nullptr;
HHOOK g_shellHook = nullptr;
HHOOK g_getMessageHook = nullptr;
HHOOK g_callWndProcRetHook = nullptr;

[[nodiscard]] HWND ResolveRootWindow(HWND hwnd)
{
    if (hwnd == nullptr) {
        return nullptr;
    }

    const HWND root = GetAncestor(hwnd, GA_ROOT);
    return root != nullptr ? root : hwnd;
}

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
    const UINT checkFlags = IsAlwaysOnTop(hwnd) ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(menu, kAlwaysOnTopCommandId, MF_BYCOMMAND | checkFlags);
}

void HideWindow(HWND hwnd)
{
    ShowWindow(hwnd, SW_HIDE);
    SetPropW(hwnd, kHiddenProp, reinterpret_cast<HANDLE>(1));
}

[[nodiscard]] int FindMenuItemPosition(HMENU menu, UINT commandId)
{
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        if (GetMenuItemID(menu, i) == static_cast<int>(commandId)) {
            return i;
        }
    }
    return -1;
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

    // The menu itself is the source of truth: some hosts (Chromium-based apps in
    // particular) rebuild their native system menu between opens, silently dropping
    // our items, or splice their own items in between ours without removing either
    // id. Re-check both ids and their relative position on every call instead of
    // trusting a window property, and if either is missing or they're no longer
    // adjacent, drop any stray leftover before re-appending the pair together so
    // they always stay next to each other.
    const int alwaysOnTopPos = FindMenuItemPosition(menu, kAlwaysOnTopCommandId);
    const int hideWindowPos = FindMenuItemPosition(menu, kHideWindowCommandId);
    const bool hasAlwaysOnTop = alwaysOnTopPos != -1;
    const bool hasHideWindow = hideWindowPos != -1;
    const bool isAdjacent =
        hasAlwaysOnTop && hasHideWindow && hideWindowPos == alwaysOnTopPos + 1;

    if (!hasAlwaysOnTop || !hasHideWindow || !isAdjacent) {
        if (hasAlwaysOnTop) {
            DeleteMenu(menu, kAlwaysOnTopCommandId, MF_BYCOMMAND);
        }
        if (hasHideWindow) {
            DeleteMenu(menu, kHideWindowCommandId, MF_BYCOMMAND);
        }

        // Deleting the items above can leave a stray separator we added on a
        // previous pass (some hosts rebuild the menu without removing it).
        // Don't stack a second one on top of it.
        const int countAfterDelete = GetMenuItemCount(menu);
        const UINT lastState = countAfterDelete > 0
            ? GetMenuState(menu, countAfterDelete - 1, MF_BYPOSITION)
            : static_cast<UINT>(-1);
        const bool lastIsSeparator =
            lastState != static_cast<UINT>(-1) && (lastState & MF_SEPARATOR) != 0;

        if (!lastIsSeparator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(
            menu,
            MF_STRING | MF_UNCHECKED,
            kAlwaysOnTopCommandId,
            L"&Always on Top");
        AppendMenuW(
            menu,
            MF_STRING,
            kHideWindowCommandId,
            L"Hide &Window");
    }

    UpdateMenuCheckState(hwnd, menu);
}

void RefreshSystemMenu(HWND hwnd, HMENU menu)
{
    hwnd = ResolveRootWindow(hwnd);
    if (hwnd == nullptr) {
        return;
    }

    HMENU systemMenu = GetSystemMenu(hwnd, FALSE);
    if (systemMenu == nullptr || menu != systemMenu) {
        return;
    }

    EnsureAlwaysOnTopMenu(hwnd);
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
            RemovePropW(reinterpret_cast<HWND>(wParam), kHiddenProp);
        }
    }

    return CallNextHookEx(g_shellHook, code, wParam, lParam);
}

LRESULT CALLBACK CallWndProcRetHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0) {
        const auto* callInfo = reinterpret_cast<CWPRETSTRUCT*>(lParam);
        // WH_CALLWNDPROCRET fires after the target window's own wndproc has
        // handled the message, unlike WH_CALLWNDPROC/WH_GETMESSAGE which fire
        // before. Chromium hosts (Brave in particular) rebuild dynamic entries
        // of their own (e.g. "Reopen closed tab") in their WM_INITMENU handler;
        // touching the menu ahead of that rebuild raced it and left it
        // confused about where its own items were from one open to the next,
        // producing duplicates. Waiting until after it's done removes us from
        // that race entirely - we always add our pair last, onto its final,
        // already-settled menu.
        if (callInfo != nullptr &&
            (callInfo->message == WM_INITMENU ||
             (callInfo->message == WM_INITMENUPOPUP && HIWORD(callInfo->lParam) == 0))) {
            RefreshSystemMenu(
                callInfo->hwnd,
                reinterpret_cast<HMENU>(callInfo->wParam));
        }
    }

    return CallNextHookEx(g_callWndProcRetHook, code, wParam, lParam);
}

LRESULT CALLBACK GetMessageHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && wParam == PM_REMOVE) {
        auto* message = reinterpret_cast<MSG*>(lParam);
        if (message == nullptr) {
            return CallNextHookEx(g_getMessageHook, code, wParam, lParam);
        }

        HWND hwnd = ResolveRootWindow(message->hwnd);
        if (message->message == WM_SYSCOMMAND &&
            (message->wParam & 0xFFF0) == kAlwaysOnTopCommandId) {
            ToggleAlwaysOnTop(hwnd);

            HMENU menu = GetSystemMenu(hwnd, FALSE);
            if (menu != nullptr) {
                UpdateMenuCheckState(hwnd, menu);
            }

            message->message = WM_NULL;
        } else if (message->message == WM_SYSCOMMAND &&
                   (message->wParam & 0xFFF0) == kHideWindowCommandId) {
            HideWindow(hwnd);
            message->message = WM_NULL;
        }
    }

    return CallNextHookEx(g_getMessageHook, code, wParam, lParam);
}

void RemoveAlwaysOnTopMenu(HWND hwnd)
{
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (menu == nullptr) {
        return;
    }

    // Drop the separator we added immediately before our own item, if it's
    // still there, then remove both items by id regardless of position.
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        if (GetMenuItemID(menu, i) == kAlwaysOnTopCommandId) {
            if (i > 0) {
                const UINT prevState = GetMenuState(menu, i - 1, MF_BYPOSITION);
                if (prevState != static_cast<UINT>(-1) && (prevState & MF_SEPARATOR) != 0) {
                    RemoveMenu(menu, i - 1, MF_BYPOSITION);
                }
            }
            break;
        }
    }

    RemoveMenu(menu, kAlwaysOnTopCommandId, MF_BYCOMMAND);
    RemoveMenu(menu, kHideWindowCommandId, MF_BYCOMMAND);
}

void RestoreHiddenWindow(HWND hwnd)
{
    if (GetPropW(hwnd, kHiddenProp) != nullptr) {
        ShowWindow(hwnd, SW_SHOW);
        RemovePropW(hwnd, kHiddenProp);
    }
}

BOOL CALLBACK CleanupWindowsProc(HWND hwnd, LPARAM)
{
    RemoveAlwaysOnTopMenu(hwnd);
    RestoreHiddenWindow(hwnd);
    return TRUE;
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

    if (g_callWndProcRetHook == nullptr) {
        g_callWndProcRetHook =
            SetWindowsHookExW(WH_CALLWNDPROCRET, CallWndProcRetHookProc, g_instance, 0);
    }

    if (g_getMessageHook == nullptr) {
        g_getMessageHook = SetWindowsHookExW(WH_GETMESSAGE, GetMessageHookProc, g_instance, 0);
    }

    EnumWindows(EnumWindowsProc, 0);
    return g_shellHook != nullptr &&
           g_callWndProcRetHook != nullptr &&
           g_getMessageHook != nullptr;
}

extern "C" __declspec(dllexport) void Aot_Unhook()
{
    if (g_shellHook != nullptr) {
        UnhookWindowsHookEx(g_shellHook);
        g_shellHook = nullptr;
    }

    if (g_callWndProcRetHook != nullptr) {
        UnhookWindowsHookEx(g_callWndProcRetHook);
        g_callWndProcRetHook = nullptr;
    }

    if (g_getMessageHook != nullptr) {
        UnhookWindowsHookEx(g_getMessageHook);
        g_getMessageHook = nullptr;
    }
}

extern "C" __declspec(dllexport) void Aot_Stop()
{
    Aot_Unhook();
    EnumWindows(CleanupWindowsProc, 0);
}

void Aot_SetInstance(HINSTANCE instance)
{
    g_instance = instance;
}