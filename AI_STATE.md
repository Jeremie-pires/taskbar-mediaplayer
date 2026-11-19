# Project state
_Updated: 2026-11-19_

## Done
- Replaced the icon font with Phosphor "fill" vector icons (SVG path data parsed into GDI+ paths, `src/icons.cpp`), also used for the tray icon.
- Added a per-user Inno Setup installer and a portable zip (`installer/`), tested on Windows 10: install, autostart, Start menu entry and a clean uninstall.
- Full code review and security audit; fixed: title and artist capped at 256 characters, artwork over 16 MB refused, WinRT calls time out after 3 s, a seek drag is cancelled on a track change, the SVG reader stops on malformed data.
- Hardened the exe: Control Flow Guard, CET, imported DLLs loaded from System32 only.
- Added GitHub Actions: CI (warnings as errors, installer build, SonarQube Cloud), release on a `vX.Y.Z` tag, Dependabot for actions.
- Prepared the folder for a public repository: MIT license, third-party notices, `.gitignore`, `.gitattributes`.

## Current state
The app builds without warnings at `/W4`, also with warnings as errors. `installer/build-installer.ps1` builds the exe, then `dist/` gets the installer, the portable zip and `SHA256SUMS.txt`, versioned from `CMakeLists.txt`. The strip, the icons and the settings window were checked by screenshot on Windows 10; the progress bar and the CI workflows have not run yet.

## Next steps
1. Create the GitHub repository and push `main`.
2. Set up SonarQube Cloud: create the project (key in `sonar-project.properties`), turn off Automatic Analysis, add the `SONAR_TOKEN` secret.
3. Check the first CI run, then tag `v2.0.0` to test the release workflow.
4. Test Large mode with Spotify, YouTube in a browser and Deezer: which players show the bar and accept seeking.
5. Test on Windows 11, in the light theme and at 125–150 % scaling.

## Open issues
- Never run on Windows 11; its taskbar layout may need changes.
- Players that publish no timeline get no progress bar.
- Vertical, right-to-left and secondary-monitor taskbars are not supported.
- The exe and installer are unsigned, so SmartScreen warns on first run.
- `legacy/register.bat` and `unregister.bat` point at a `bin/` folder that no longer exists (reference code only).

## Decisions
- 2026-11-19 Strip width is a share of the taskbar width per size mode, within DPI-scaled bounds — scales with the screen without a fixed pixel value; the old `Width` value is no longer read.
- 2026-11-19 Progress and seeking use the media session timeline only — one method for every player; no player-specific code.
- 2026-11-19 Settings apply live, with switches drawn over real check boxes — matches Windows 11 Settings while keeping keyboard and screen reader support.
- 2026-11-19 Icons are Phosphor path data drawn by the app — same modern look on Windows 10 and 11, no font dependency.
- 2026-11-19 Per-user installer only, no "all users" option — settings and autostart are per user and no admin rights are needed.
- 2026-11-19 Releases come from a `vX.Y.Z` tag that must match `CMakeLists.txt`; single `main` branch — small project, one version source.
