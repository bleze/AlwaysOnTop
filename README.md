# AlwaysOnTop

A lightweight Windows tray utility that adds **Always on Top** to every window's system menu.

Right-click a window title bar (or its taskbar button, or press Alt+Space) and toggle **Always on Top** with a checkmark showing the current state.

## How it works

- `AlwaysOnTop.exe` runs in the notification area and loads `AlwaysOnTopHook.dll`.
- The hook DLL installs global `WH_SHELL` and `WH_GETMESSAGE` hooks.
- When a window's system menu opens, an **Always on Top** item is injected.
- Selecting the item toggles `HWND_TOPMOST` via `SetWindowPos`.

## Build

Requirements: Windows 10+, CMake 3.25+, MSVC (Visual Studio 2022/2026).

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output:

```
build/Release/AlwaysOnTop.exe
build/Release/AlwaysOnTopHook.dll
```

Both files must stay in the same directory.

## Usage

1. Run `AlwaysOnTop.exe`.
2. Right-click any window title bar.
3. Choose **Always on Top**.
4. Right-click the tray icon and choose **Exit** to stop the utility.

## Limitations

- Elevated windows (Run as administrator) require AlwaysOnTop to run elevated as well.
- Some modern apps (UWP, custom chrome) may not expose a standard system menu.
- Global hooks load a DLL into other processes; some security software may flag this behavior.

## License

MIT