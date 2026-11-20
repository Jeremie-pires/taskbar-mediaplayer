# Decisions

- 2026-11-19 Standalone EXE instead of a DeskBand DLL — no admin rights, an app crash cannot take Explorer down, and DeskBands do not work on Windows 11.
- 2026-11-19 CMake + MSVC instead of MinGW — the media session API needs C++/WinRT headers from the Windows SDK.
- 2026-11-19 Kept the `HKCU\Software\MusicLounge` value names — settings from 1.x carry over.
- 2026-11-19 Full-screen detection compares the foreground window to the taskbar's monitor — `SHQueryUserNotificationState` reported busy when no window was full screen.
- 2026-11-19 Renamed to Taskbar Media Player / taskbar-mediaplayer, with new registry key, Run value, mutex and window classes — clearer name; a one-time migration keeps existing settings and autostart.
- 2026-11-19 Strip width is a share of the taskbar width per size mode, within DPI-scaled bounds — scales with the screen without a fixed pixel value; the old `Width` value is no longer read.
- 2026-11-19 Progress and seeking use the media session timeline only — one method for every player; no player-specific code.
- 2026-11-19 Settings apply live, with switches drawn over real check boxes — matches Windows 11 Settings while keeping keyboard and screen reader support.
- 2026-11-19 Icons are Phosphor path data drawn by the app — same modern look on Windows 10 and 11, no font dependency.
- 2026-11-19 Per-user installer only, no "all users" option — settings and autostart are per user and no admin rights are needed.
