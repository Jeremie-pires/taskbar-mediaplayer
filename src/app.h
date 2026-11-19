#pragma once
// Names and window messages shared by the app's modules.
#include <windows.h>

namespace app {

inline constexpr wchar_t kName[] = L"Taskbar Media Player";
inline constexpr wchar_t kWindowClass[] = L"TaskbarMediaPlayerWnd";
inline constexpr wchar_t kSettingsKey[] = L"Software\\TaskbarMediaPlayer";
inline constexpr wchar_t kLegacySettingsKey[] = L"Software\\MusicLounge";  // used before the rename

// Posted to the main window.
inline constexpr UINT WM_MEDIA_CHANGED = WM_APP + 1;   // media thread: a new snapshot is available
inline constexpr UINT WM_SETTINGS_SAVED = WM_APP + 2;  // settings dialog: new values are in the registry
inline constexpr UINT WM_SHOW_MENU = WM_APP + 3;       // widget: right click
inline constexpr UINT WM_SHOW_SETTINGS = WM_APP + 4;   // widget: settings button
inline constexpr UINT WM_TRAY = WM_APP + 5;            // notification-area icon callback

// True when the taskbar uses the light theme (Settings > Personalization > Colors).
inline bool LightTaskbar() {
    DWORD v = 0, size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS &&
           v != 0;
}

// True when apps (and so our dialogs) use the dark theme.
inline bool DarkApps() {
    DWORD v = 1, size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

}  // namespace app
