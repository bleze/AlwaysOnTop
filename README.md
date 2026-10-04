# AlwaysOnTop

A lightweight Windows tray utility that adds **Always on Top** and **Hide Window** to every window's system menu.

Right-click a window title bar (or its taskbar button, or press Alt+Space) and toggle **Always on Top** with a checkmark showing the current state, or choose **Hide Window** to make it fully invisible. Hidden windows have no title bar to right-click, so they can only be restored from the tray icon's **Manage Windows** dialog.

## How it works

- `AlwaysOnTop.exe` runs in the notification area and loads `AlwaysOnTopHook.dll`.
- The hook DLL installs global `WH_SHELL`, `WH_CALLWNDPROC`, and `WH_GETMESSAGE` hooks.
- When a window's system menu opens, **Always on Top** and **Hide Window** items are injected.
- Selecting **Always on Top** toggles `HWND_TOPMOST` via `SetWindowPos`.
- Selecting **Hide Window** calls `ShowWindow(SW_HIDE)` and tags the window so it shows up in the Manage Windows dialog.
- Optionally (tray menu → **Hidden Windows in Tray**, off by default), each hidden window also gets its own tray icon, using its process's icon and name; left-click it to restore the window, right-click for a menu.
- The tray icon's **Manage Windows** dialog lists every pinned and/or hidden window (with its state) and can unpin/unhide them, individually or all at once.
- The app follows the system light/dark theme setting for its own dialogs.

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

## Releases

Pushing a tag matching `v*.*.*` (e.g. `v0.1.0`) triggers a GitHub Actions
workflow that builds the Release binaries, packages them into an Inno Setup
installer, and publishes both the installer and a portable zip to a GitHub
Release.

## Usage

1. Run `AlwaysOnTop.exe`.
2. Right-click any window title bar.
3. Choose **Always on Top** to pin it, or **Hide Window** to hide it.
4. Right-click the tray icon and choose **Manage Windows...** to see pinned/hidden windows and restore them.
5. Right-click the tray icon and choose **Quit** to stop the utility.

## Limitations

- Elevated windows (Run as administrator) require AlwaysOnTop to run elevated as well.
- Some modern apps (UWP, custom chrome) may not expose a standard system menu.
- Global hooks load a DLL into other processes; some security software may flag this behavior.
- Pinned/hidden state isn't persisted anywhere; it lives only as long as the target window itself does. If you quit AlwaysOnTop.exe while a window is hidden, relaunch the app and use Manage Windows to restore it.

## License

MIT