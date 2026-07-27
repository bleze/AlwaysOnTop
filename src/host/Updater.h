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
    std::wstring version;
    std::wstring message;
};

// Checks GitHub for a newer release on a background thread. If one is found,
// downloads the installer and launches it silently (the installer closes and
// restarts this app itself via Restart Manager). Posts exactly one
// kUpdateResultMessage back to notifyWindow describing the outcome.
void CheckForUpdatesAsync(HWND notifyWindow, bool manual);
