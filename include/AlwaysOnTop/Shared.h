#pragma once

#include <windows.h>

#include <string>

// Strips a leading "v"/"V" from a release tag ("v0.4.7" -> "0.4.7"), matching
// the bare PROJECT_VERSION string used to name per-version build outputs.
[[nodiscard]] inline std::wstring StripVersionTagPrefix(std::wstring text)
{
    if (!text.empty() && (text[0] == L'v' || text[0] == L'V')) {
        text.erase(0, 1);
    }
    return text;
}

// Custom system-menu command ids. Must stay below 0xF000 (that range is reserved
// for Windows' own SC_* system commands) and keep their low 4 bits zero, since
// WM_SYSCOMMAND handling masks wParam with 0xFFF0 before comparing.
constexpr UINT kAlwaysOnTopCommandId = 0x1000;
constexpr UINT kHideWindowCommandId = 0x1010;

// Window property marking that we hid this window; only our own dialog unhides it.
constexpr const wchar_t* kHiddenProp = L"AlwaysOnTop.Hidden";

// Export names used by the host executable.
constexpr const char* kStartHooksExport = "Aot_Start";
constexpr const char* kStopHooksExport = "Aot_Stop";
// Unhooks without touching any window state (no restoring hidden windows or
// stripping menu items). Used to release the DLL for an in-place self-update,
// where the same window state should carry over to the relaunched process.
constexpr const char* kUnhookOnlyExport = "Aot_Unhook";