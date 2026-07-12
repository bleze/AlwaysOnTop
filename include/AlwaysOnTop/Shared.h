#pragma once

#include <windows.h>

// Custom system-menu command ids (0xF000 range).
constexpr UINT kAlwaysOnTopCommandId = 0xF150;
constexpr UINT kHideWindowCommandId = 0xF160;

// Window property marking that our menu item was injected.
constexpr const wchar_t* kMenuInjectedProp = L"AlwaysOnTop.MenuInjected";

// Window property marking that we hid this window; only our own dialog unhides it.
constexpr const wchar_t* kHiddenProp = L"AlwaysOnTop.Hidden";

// Export names used by the host executable.
constexpr const char* kStartHooksExport = "Aot_Start";
constexpr const char* kStopHooksExport = "Aot_Stop";