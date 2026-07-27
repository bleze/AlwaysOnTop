#pragma once

#include <windows.h>

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