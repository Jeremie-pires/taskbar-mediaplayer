# Project state
_Updated: 2026-11-20_

## Done
- Player selection: the player that most recently started playing takes over; the shown player stays while it plays or after a pause; Windows' current session breaks ties.
- Sessions published by `msedgewebview2.exe` (game launchers and other apps embedding WebView2) are ignored.
- Removed the notification-area icon. The settings window gained a "Démarrer avec Windows" switch and a "Quitter" button; autostart code moved from `main.cpp` to `settings.cpp`.
- The settings window shows the version (from `CMakeLists.txt`) between its two buttons, for bug reports.
- Version bumped to 2.1.0 (CMake, manifest, Inno Setup default) and released from tag `v2.1.0`.

## Current state
Version 2.1.0 is released: player selection by latest start, WebView2 sessions ignored, no notification-area icon, version shown in the settings window. The strip's right-click menu and the settings window (opened by starting the exe again) are the only entry points. The portable-zip step of `installer/build-installer.ps1` fails when its staging folder holds a file from an earlier run, so local `dist/` output can be stale (the release workflow builds its own).

## Next steps
1. Check that the `v2.1.0` release workflow succeeded and its assets download and install.
2. Make `build-installer.ps1` clear or overwrite its staging folder.
3. Review the SonarQube Cloud findings and fix or dismiss each one.
4. Test Large mode with Spotify, YouTube in a browser and Deezer; test on Windows 11, light theme, 125–150 % scaling.

## Open issues
- Players are told apart by app id: a second tab of the same browser starting playback does not take over from the first.
- A real player built on WebView2 would be hidden too.
- Never run on Windows 11; its taskbar layout may need changes.
- Players that publish no timeline get no progress bar.
- Vertical, right-to-left and secondary-monitor taskbars are not supported.
- The exe and installer are unsigned, so SmartScreen warns on first run.
- `legacy/register.bat` and `unregister.bat` point at a `bin/` folder that no longer exists (reference code only).
- No automated tests.

## Decisions
- 2026-11-19 Releases come from a `vX.Y.Z` tag that must match `CMakeLists.txt`; single `main` branch — small project, one version source.
- 2026-11-19 SonarQube Cloud automatic analysis, not a CI job — no token to manage; slightly less precise for C++, enough for this size.
- 2026-11-19 No forced CMake generator — GitHub's Windows runners moved to Visual Studio 2026, and local builds use 2022.
- 2026-11-20 The most recently started player wins — the playing order Windows reports is registration order, not activity.
- 2026-11-20 `msedgewebview2.exe` sessions are ignored, with no option — they do not name the app behind them and carry normal metadata, so nothing else can filter them.
- 2026-11-20 No notification-area icon; autostart and quit live in the settings window — the strip already has the menu, and relaunching the exe reaches the settings when the strip is hidden.
