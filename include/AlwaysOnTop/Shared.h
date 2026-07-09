#pragma once

#include <windows.h>

// Custom system-menu command id (0xF000 range).
constexpr UINT kAlwaysOnTopCommandId = 0xF150;

// Window property marking that our menu item was injected.
constexpr const wchar_t* kMenuInjectedProp = L"AlwaysOnTop.MenuInjected";

// Export names used by the host executable.
constexpr const char* kStartHooksExport = "Aot_Start";
constexpr const char* kStopHooksExport = "Aot_Stop";