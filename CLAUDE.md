# Taskbar Media Player
Media player strip on the Windows taskbar (artwork, title, artist, previous / play / next), standalone Win32 EXE.

## Stack
- C++20, Win32, GDI+, C++/WinRT (Windows.Media.Control), MSVC (VS 2022), CMake 3.21+.
- Windows SDK 10.0.26100 or later (provides the C++/WinRT headers). No third-party libraries.

## Commands
- Build: `build.bat` (or `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release`).
- Output: `build\Release\taskbar-mediaplayer.exe`.
- Installer and portable zip: `powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1` (needs Inno Setup 6+), output in `dist\`.
- Release: push a tag `vX.Y.Z` matching `project(... VERSION)` in `CMakeLists.txt`; `.github/workflows/release.yml` publishes it.
- Stop a running instance before rebuilding (the exe is locked): `build\Release\taskbar-mediaplayer.exe --quit`.
- No lint config and no automated tests yet. Warnings are `/W4`: keep the build warning-free (CI builds with warnings as errors).

## Layout
- `src/main.cpp`: hidden owner window, 1 s tick, tray icon, menu, autostart, `TaskbarCreated`, `--quit`.
- `src/widget.cpp`: the strip, a layered child of `Shell_TrayWnd`, drawn with GDI+, placed left of `TrayNotifyWnd`.
- `src/icons.cpp`: Phosphor "fill" icons kept as SVG path data, parsed into GDI+ paths (license in `THIRD-PARTY-NOTICES.md`).
- `src/media.cpp`: GSMTC polling and commands on a dedicated MTA thread.
- `src/settings.cpp`, `src/settings_dialog.cpp`: registry settings (`HKCU\Software\TaskbarMediaPlayer`, migrated once from the old `Software\MusicLounge` key) and the settings window.
- `res/`: manifest (PerMonitorV2, asInvoker) and version resource.
- `legacy/`: old 1.x DeskBand DLL, reference only, not built.

## Conventions
- Code and comments in English; user-facing strings (UI, README) in French.
- Module-level state in anonymous namespaces with `g_` prefixes; public API in a namespace per module (`widget::`, `media::`, `settings_dialog::`).
- Taskbar integration follows the BatteryHub reference project (github.com/krisgfx9/Wireless-Device-Battery-Monitor).
- Include `gdiplus_inc.h`, never `<gdiplus.h>` directly (`NOMINMAX` is defined globally).

## Gotchas
- The widget is a child of Explorer's taskbar, so Explorer waits on our UI thread. Never block it: WinRT `.get()` and UI Automation calls belong on background threads.
- Layered window pixels with alpha 0 let clicks through: clear the background to alpha 1.
- Windows 10: Explorer re-widens `ReBarWindow32` on its own, so it has to be narrowed again on every tick.
- `SHQueryUserNotificationState` reports `QUNS_BUSY` with no full-screen window, so don't use it for full-screen detection.
- Settings value names must stay compatible with 1.x.
- Formerly named Music Lounge: the old registry key and Run value are migrated at startup (`MigrateLegacySettings`, `MigrateLegacyAutostart`).
