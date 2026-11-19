#pragma once
// User settings, stored under HKCU\Software\TaskbarMediaPlayer (same value names as the 1.x DeskBand).

enum class Theme { Taskbar = 0, Light = 1, Dark = 2, Transparent = 3 };
// Compact: artwork and buttons. Classic: adds title and artist. Large: adds a progress bar.
enum class SizeMode { Compact = 0, Classic = 1, Large = 2 };

struct Settings {
    static constexpr int kMinFont = 8, kMaxFont = 24;
    static constexpr int kMaxIdle = 3600;

    SizeMode size = SizeMode::Classic;  // the width follows the taskbar's width (see widget.cpp)
    int fontSize = 11;    // title size, in pixels at 100% scaling
    Theme theme = Theme::Taskbar;
    int idleTimeout = 0;  // seconds without playback before the widget hides; 0 = never
    bool showAlbumArt = true;
    bool showTitle = true;
    bool showArtist = true;
    bool showPrev = true;
    bool showPlay = true;
    bool showNext = true;
    bool showSettings = false;  // gear button on the strip; the menu always offers the settings
    bool hideWhenNoMedia = false;
    bool hideFullscreen = false;
};

// Copies the settings saved under the old name (Music Lounge) once, when none exist under the new one.
void MigrateLegacySettings();
Settings LoadSettings();
void SaveSettings(const Settings& s);
