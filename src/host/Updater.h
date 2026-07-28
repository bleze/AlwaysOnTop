#pragma once

#include <windows.h>

#include <string>

// Posted to the notify window with lParam pointing to a heap-allocated
// UpdateResult; the receiver takes ownership and must delete it.
constexpr UINT kUpdateResultMessage = WM_APP + 2;

struct UpdateResult
{
    bool manual = false;
    bool hasError = false;
    bool upToDate = false;
    bool readyToInstall = false;
    std::wstring version;
    std::wstring message;
    std::wstring stagedExePath;
    std::wstring stagedDllPath;
};

// Checks GitHub for a newer release on a background thread. If one is found,
// downloads the plain exe/dll assets (not the installer) to temp files. The
// hook DLL is injected into essentially every window-owning process on the
// desktop via the global hooks, so an installer that relies on closing every
// app holding it open is unreliable; the caller is expected to swap the
// staged files into place itself (see stagedExePath/stagedDllPath) and
// relaunch. Posts exactly one kUpdateResultMessage back to notifyWindow
// describing the outcome.
void CheckForUpdatesAsync(HWND notifyWindow, bool manual);
