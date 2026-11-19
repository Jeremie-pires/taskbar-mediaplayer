# Project state
_Updated: 2026-11-19_

## Done
- Replaced the icon font with Phosphor "fill" vector icons (SVG path data parsed into GDI+ paths, `src/icons.cpp`), also used for the tray icon.
- Added a per-user Inno Setup installer and a portable zip (`installer/`), tested on Windows 10: install, autostart, Start menu entry and a clean uninstall.
- Full code review and security audit; fixed: title and artist capped at 256 characters, artwork over 16 MB refused, WinRT calls time out after 3 s, a seek drag is cancelled on a track change, the SVG reader stops on malformed data.
- Hardened the exe: Control Flow Guard, CET, imported DLLs loaded from System32 only.
- Published the public GitHub repository. GitHub Actions: CI (warnings as errors, installer build) is green; release on a `vX.Y.Z` tag; Dependabot for actions. SonarQube Cloud analyses each push on its own.
- Prepared the folder for a public repository: MIT license, third-party notices, `.gitignore`, `.gitattributes`.

## Current state
The app builds without warnings at `/W4`, also with warnings as errors. `installer/build-installer.ps1` builds the exe, then `dist/` gets the installer, the portable zip and `SHA256SUMS.txt`, versioned from `CMakeLists.txt`. The strip, the icons and the settings window were checked by screenshot on Windows 10; the progress bar and the release workflow have not run yet. CI builds with Visual Studio 2026 on the `windows-2025` runner, so no CMake generator is forced.

## Next steps
1. Push the annotated tag `v2.0.0` and check the published release.
2. Review the SonarQube Cloud findings.
3. Test Large mode with Spotify, YouTube in a browser and Deezer: which players show the bar and accept seeking.
4. Test on Windows 11, in the light theme and at 125–150 % scaling.

## Open issues
- Never run on Windows 11; its taskbar layout may need changes.
- Players that publish no timeline get no progress bar.
- Vertical, right-to-left and secondary-monitor taskbars are not supported.
- The exe and installer are unsigned, so SmartScreen warns on first run.
- `legacy/register.bat` and `unregister.bat` point at a `bin/` folder that no longer exists (reference code only).

## Decisions
- 2026-11-19 Progress and seeking use the media session timeline only — one method for every player; no player-specific code.
- 2026-11-19 Settings apply live, with switches drawn over real check boxes — matches Windows 11 Settings while keeping keyboard and screen reader support.
- 2026-11-19 Icons are Phosphor path data drawn by the app — same modern look on Windows 10 and 11, no font dependency.
- 2026-11-19 Per-user installer only, no "all users" option — settings and autostart are per user and no admin rights are needed.
- 2026-11-19 Releases come from a `vX.Y.Z` tag that must match `CMakeLists.txt`; single `main` branch — small project, one version source.
- 2026-11-19 SonarQube Cloud automatic analysis, not a CI job — no token to manage; slightly less precise for C++, enough for this size.
