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

void HideWindow(HWND hwnd, bool inTray)
{
    ShowWindow(hwnd, SW_HIDE);
    SetPropW(hwnd, kHiddenProp, reinterpret_cast<HANDLE>(1));
    if (inTray) {
        SetPropW(hwnd, kHiddenInTrayProp, reinterpret_cast<HANDLE>(1));
    } else {
        RemovePropW(hwnd, kHiddenInTrayProp);
    }

    // Let the host know so it can add (or drop) a tray icon for the window.
    HWND host = FindWindowW(kHostWindowClassName, nullptr);
    const UINT message = RegisterWindowMessageW(kHiddenStateChangedMessageName);
    if (host != nullptr && message != 0) {
        PostMessageW(host, message, 0, 0);
    }
}

// Window property marking that we added the separator immediately before our
// own items ourselves, rather than reusing one the host already had there.
constexpr const wchar_t* kOwnsLeadingSeparatorProp = L"AlwaysOnTop.OwnsLeadingSeparator";

// Removes our pair and the trailing separator we always add right after it
// (always safe to remove - nothing else could have put a separator there).
// The leading separator is only removed if kOwnsLeadingSeparatorProp says we
// added it ourselves last time; if we instead reused one the host already
// had, it's left untouched, so we never permanently delete something the
// host still relies on once our own items are gone. Safe to call when our
// items are absent.
void StripAlwaysOnTopItems(HWND hwnd, HMENU menu)
{
    // Trailing separator first, by position relative to HideInTray, before
    // anything else shifts indices around.
    for (int i = 0, count = GetMenuItemCount(menu); i < count; ++i) {
        if (GetMenuItemID(menu, i) == kHideInTrayCommandId) {
            if (i + 1 < count) {
                const UINT nextState = GetMenuState(menu, i + 1, MF_BYPOSITION);
                if (nextState != static_cast<UINT>(-1) && (nextState & MF_SEPARATOR) != 0) {
                    RemoveMenu(menu, i + 1, MF_BYPOSITION);
                }
            }
            break;
        }
    }

    if (GetPropW(hwnd, kOwnsLeadingSeparatorProp) != nullptr) {
        for (int i = 0, count = GetMenuItemCount(menu); i < count; ++i) {
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
    }

    RemoveMenu(menu, kAlwaysOnTopCommandId, MF_BYCOMMAND);
    RemoveMenu(menu, kHideWindowCommandId, MF_BYCOMMAND);
    RemoveMenu(menu, kHideInTrayCommandId, MF_BYCOMMAND);
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

    // The menu itself is the source of truth: some hosts (Chromium-based apps
    // in particular) rebuild their native system menu between opens, silently
    // dropping our items or splicing their own in between ours. Captured logs
    // show Brave's rebuild always inserts its dynamic block at "one position
    // before whatever item is currently last" - so appending our pair at the
    // true end (making one of them the last item) guarantees Brave's next
    // rebuild lands its insert *inside* our pair, no matter how we time our
    // own fix-up relative to that rebuild. Inserting our pair immediately
    // before whatever the host's own last item is (e.g. "Close") instead
    // keeps that item last, so Brave's insert keeps landing before it - a
    // slot that was never ours to begin with.
    StripAlwaysOnTopItems(hwnd, menu);

    const int count = GetMenuItemCount(menu);
    int insertPos = count > 0 ? count - 1 : 0;

    // If the host already has a separator right where we're about to land,
    // reuse it rather than stacking a second one - but leave it in place
    // rather than deleting and replacing it: it may be one the host still
    // relies on once our own items are gone again (e.g. the separator
    // before "Close"), so only a separator we actually add ourselves is
    // ever safe to remove later. Track which case this was via a window
    // property so StripAlwaysOnTopItems knows whether it's safe to remove.
    const bool hostAlreadyHasSeparator =
        insertPos > 0 && (GetMenuState(menu, insertPos - 1, MF_BYPOSITION) & MF_SEPARATOR) != 0;
    if (hostAlreadyHasSeparator) {
        RemovePropW(hwnd, kOwnsLeadingSeparatorProp);
    } else {
        InsertMenuW(menu, insertPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        ++insertPos;
        SetPropW(hwnd, kOwnsLeadingSeparatorProp, reinterpret_cast<HANDLE>(1));
    }

    InsertMenuW(
        menu,
        insertPos,
        MF_BYPOSITION | MF_STRING,
        kAlwaysOnTopCommandId,
        L"&Always on Top");
    InsertMenuW(
        menu,
        insertPos + 1,
        MF_BYPOSITION | MF_STRING,
        kHideWindowCommandId,
        L"Hide &Window");
    InsertMenuW(
        menu,
        insertPos + 2,
        MF_BYPOSITION | MF_STRING,
        kHideInTrayCommandId,
        L"Hide in &Tray");
    InsertMenuW(menu, insertPos + 3, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

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

LRESULT CALLBACK ShellHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0) {
        if (code == HSHELL_WINDOWDESTROYED) {
            RemovePropW(reinterpret_cast<HWND>(wParam), kHiddenProp);
            RemovePropW(reinterpret_cast<HWND>(wParam), kHiddenInTrayProp);
            RemovePropW(reinterpret_cast<HWND>(wParam), kOwnsLeadingSeparatorProp);
        }
    }

    return CallNextHookEx(g_shellHook, code, wParam, lParam);
}

LRESULT CALLBACK CallWndProcRetHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0) {
        const auto* callInfo = reinterpret_cast<CWPRETSTRUCT*>(lParam);

        // WM_INITMENUPOPUP is sent after WM_INITMENU, right before the popup is
        // actually shown - the last point in the sequence, and (per observed
        // Chromium behavior) where hosts like Brave do their own dynamic-item
        // rebuild (e.g. "Reopen closed tab"), not in WM_INITMENU. Reacting
        // here, via WH_CALLWNDPROCRET (fires after the target wndproc has
        // already handled the message), means the host has always finished its
        // own rebuild by the time we touch the menu, so we're never the one
        // racing it into duplicating its own items.
        //
        // MSDN documents HIWORD(lParam) as nonzero specifically for the
        // window/system menu, but captured logs show Brave's real system-menu
        // popup reports HIWORD == 0 - its custom title bar likely drives
        // TrackPopupMenu itself rather than going through the normal
        // SC_MOUSEMENU path Windows expects that flag to reflect. Skip the
        // flag entirely and let RefreshSystemMenu's own identity check (is
        // this popup literally GetSystemMenu(hwnd, FALSE)?) decide instead -
        // that's accurate regardless of how the host triggered the popup, and
        // it also means we re-fix on every open rather than only once at
        // window creation, so we keep catching up with any later rebuild.
        if (callInfo != nullptr && callInfo->message == WM_INITMENUPOPUP) {
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
            HideWindow(hwnd, false);
            message->message = WM_NULL;
        } else if (message->message == WM_SYSCOMMAND &&
                   (message->wParam & 0xFFF0) == kHideInTrayCommandId) {
            HideWindow(hwnd, true);
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

    StripAlwaysOnTopItems(hwnd, menu);
    RemovePropW(hwnd, kOwnsLeadingSeparatorProp);
}

void RestoreHiddenWindow(HWND hwnd)
{
    if (GetPropW(hwnd, kHiddenProp) != nullptr) {
        ShowWindow(hwnd, SW_SHOW);
        RemovePropW(hwnd, kHiddenProp);
        RemovePropW(hwnd, kHiddenInTrayProp);
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

    // Deliberately not populated eagerly here (or on window creation): for
    // Chromium hosts, doing so before the user has ever opened the menu
    // races their own lazy-built dynamic content (e.g. Brave's "Reopen closed
    // tab" section), which doesn't exist yet at this point. Our insert then
    // consumes the plain window's still-generic trailing separator for its
    // own boundary, leaving nothing where the host's own content later
    // expects one already there. EnsureAlwaysOnTopMenu only ever runs from
    // WM_INITMENUPOPUP (see RefreshSystemMenu), by which point the host has
    // always finished building whatever it's going to build for this open.
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