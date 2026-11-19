#include "settings.h"

#include "app.h"

#include <algorithm>

namespace {

DWORD ReadDword(const wchar_t* name, DWORD fallback) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, app::kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return fallback;
    return v;
}

bool ReadBool(const wchar_t* name, bool fallback) { return ReadDword(name, fallback ? 1 : 0) != 0; }

void WriteDword(HKEY key, const wchar_t* name, DWORD v) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
}

}  // namespace

void MigrateLegacySettings() {
    HKEY existing;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, app::kSettingsKey, 0, KEY_READ, &existing) == ERROR_SUCCESS) {
        RegCloseKey(existing);
        return;
    }
    HKEY legacy;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, app::kLegacySettingsKey, 0, KEY_READ, &legacy) != ERROR_SUCCESS) return;
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, app::kSettingsKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr) ==
        ERROR_SUCCESS) {
        RegCopyTreeW(legacy, nullptr, key);
        RegCloseKey(key);
    }
    RegCloseKey(legacy);
}

Settings LoadSettings() {
    const Settings d;
    Settings s;
    const DWORD size = ReadDword(L"SizeMode", (DWORD)d.size);
    s.size = size <= (DWORD)SizeMode::Large ? (SizeMode)size : d.size;
    s.fontSize = std::clamp((int)ReadDword(L"FontSize", d.fontSize), Settings::kMinFont, Settings::kMaxFont);
    DWORD theme = ReadDword(L"Theme", (DWORD)d.theme);
    s.theme = theme <= (DWORD)Theme::Transparent ? (Theme)theme : d.theme;
    s.idleTimeout = std::clamp((int)ReadDword(L"IdleTimeout", d.idleTimeout), 0, Settings::kMaxIdle);
    s.showAlbumArt = ReadBool(L"ShowAlbumArt", d.showAlbumArt);
    s.showTitle = ReadBool(L"ShowTitle", d.showTitle);
    s.showArtist = ReadBool(L"ShowArtist", d.showArtist);
    s.showPrev = ReadBool(L"ShowPrev", d.showPrev);
    s.showPlay = ReadBool(L"ShowPlay", d.showPlay);
    s.showNext = ReadBool(L"ShowNext", d.showNext);
    s.showSettings = ReadBool(L"ShowSettings", d.showSettings);
    s.hideWhenNoMedia = ReadBool(L"HideWhenNoMedia", d.hideWhenNoMedia);
    s.hideFullscreen = ReadBool(L"HideFullscreen", d.hideFullscreen);
    return s;
}

void SaveSettings(const Settings& s) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, app::kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    WriteDword(key, L"SizeMode", (DWORD)s.size);
    WriteDword(key, L"FontSize", (DWORD)s.fontSize);
    WriteDword(key, L"Theme", (DWORD)s.theme);
    WriteDword(key, L"IdleTimeout", (DWORD)s.idleTimeout);
    WriteDword(key, L"ShowAlbumArt", s.showAlbumArt);
    WriteDword(key, L"ShowTitle", s.showTitle);
    WriteDword(key, L"ShowArtist", s.showArtist);
    WriteDword(key, L"ShowPrev", s.showPrev);
    WriteDword(key, L"ShowPlay", s.showPlay);
    WriteDword(key, L"ShowNext", s.showNext);
    WriteDword(key, L"ShowSettings", s.showSettings);
    WriteDword(key, L"HideWhenNoMedia", s.hideWhenNoMedia);
    WriteDword(key, L"HideFullscreen", s.hideFullscreen);
    RegCloseKey(key);
}
