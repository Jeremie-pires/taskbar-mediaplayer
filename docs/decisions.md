# Decisions

- 2026-11-19 Standalone EXE instead of a DeskBand DLL — no admin rights, an app crash cannot take Explorer down, and DeskBands do not work on Windows 11.
- 2026-11-19 CMake + MSVC instead of MinGW — the media session API needs C++/WinRT headers from the Windows SDK.
- 2026-11-19 Kept the `HKCU\Software\MusicLounge` value names — settings from 1.x carry over.
- 2026-11-19 Full-screen detection compares the foreground window to the taskbar's monitor — `SHQueryUserNotificationState` reported busy when no window was full screen.
- 2026-11-19 Renamed to Taskbar Media Player / taskbar-mediaplayer, with new registry key, Run value, mutex and window classes — clearer name; a one-time migration keeps existing settings and autostart.
- 2026-11-19 Strip width is a share of the taskbar width per size mode, within DPI-scaled bounds — scales with the screen without a fixed pixel value; the old `Width` value is no longer read.
