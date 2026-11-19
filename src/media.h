#pragma once
// Reads what is playing through the Windows media session API (the one behind the volume flyout) and
// sends play / pause / skip commands to the same player. All WinRT calls run on one background thread,
// so the UI thread, which Explorer waits on because the widget is a child of the taskbar, never blocks.
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Gdiplus {
class Bitmap;
}

namespace media {

struct Snapshot {
    bool hasMedia = false;
    bool playing = false;
    std::wstring title, artist;
    std::shared_ptr<Gdiplus::Bitmap> art;  // square, never modified once published; null when the player has none
    // Timeline, in 100 ns ticks from the start of the track. Players that publish none leave hasTimeline false.
    bool hasTimeline = false;
    bool canSeek = false;
    int64_t position = 0;  // as of `stamp`; advances on its own while playing (see Position())
    int64_t duration = 0;
    ULONGLONG stamp = 0;   // GetTickCount64() when `position` was read
};

inline constexpr int64_t kTicksPerSecond = 10'000'000;

// The playback position now, extrapolated from the snapshot: most players only publish it on a change.
inline int64_t Position(const Snapshot& s, ULONGLONG now) {
    int64_t p = s.position;
    if (s.playing && now > s.stamp) p += (int64_t)(now - s.stamp) * (kTicksPerSecond / 1000);
    return p < 0 ? 0 : p > s.duration ? s.duration : p;
}

enum class Command { Previous, TogglePlayPause, Next, Seek };

// Starts the watcher thread. It posts `msg` to `notify` each time the snapshot changes.
void Start(HWND notify, UINT msg);
// Stops the thread and releases the artwork; call before GdiplusShutdown.
void Stop();
Snapshot Current();
// Queued and run on the watcher thread, against the player the widget shows. `position` (ticks from
// the start of the track) is used by Command::Seek only.
void Send(Command cmd, int64_t position = 0);

}  // namespace media
