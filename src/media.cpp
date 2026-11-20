#include "media.h"

#include "gdiplus_inc.h"

#include <shcore.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace {

using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using winrt::Windows::Storage::Streams::IRandomAccessStreamWithContentType;

constexpr DWORD kPollMs = 1000;
constexpr DWORD kAfterCommandMs = 150;  // players update their state a moment after a command
constexpr int kArtSize = 128;           // artwork is scaled once to this square, then only drawn
constexpr std::chrono::seconds kCallTimeout{ 3 };
// Limits on what a player publishes: the strip measures the text on every frame, on the thread the
// taskbar waits on, and GDI+ decodes the thumbnail in full.
constexpr size_t kMaxText = 256;
constexpr uint64_t kMaxArtBytes = 16 * 1024 * 1024;

struct Request {
    media::Command cmd;
    int64_t position;
};

std::mutex g_lock;  // guards g_snapshot and g_commands
media::Snapshot g_snapshot;
std::vector<Request> g_commands;
HANDLE g_wake;  // auto-reset: a command is queued or Stop() was called
std::atomic<bool> g_running;
std::thread g_thread;
HWND g_notify;
UINT g_msg;

// Waits for a WinRT call, but not forever: a hung player must not freeze this thread, and with it
// media::Stop() and the app's exit. A timeout throws like any other failed call.
template <typename Operation>
auto Await(Operation const& op) {
    if (op.wait_for(kCallTimeout) == winrt::Windows::Foundation::AsyncStatus::Started) {
        op.Cancel();
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    return op.GetResults();
}

std::wstring Capped(winrt::hstring const& s) {
    size_t n = std::min<size_t>(s.size(), kMaxText);
    if (n < s.size() && n > 0 && IS_HIGH_SURROGATE(s[(uint32_t)n - 1])) n--;  // never split a surrogate pair
    return std::wstring{ s.c_str(), n };
}

// Decodes the player's thumbnail and crops it to a centred square of kArtSize pixels.
std::shared_ptr<Gdiplus::Bitmap> DecodeArt(IRandomAccessStreamWithContentType const& stream) {
    if (!stream || stream.Size() > kMaxArtBytes) return nullptr;
    winrt::com_ptr<IStream> native;
    if (FAILED(CreateStreamOverRandomAccessStream(winrt::get_unknown(stream), IID_PPV_ARGS(native.put())))) return nullptr;
    Gdiplus::Bitmap src(native.get());
    if (src.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const INT w = (INT)src.GetWidth(), h = (INT)src.GetHeight(), side = std::min(w, h);
    if (side <= 0) return nullptr;
    auto art = std::make_shared<Gdiplus::Bitmap>(kArtSize, kArtSize, PixelFormat32bppPARGB);
    if (art->GetLastStatus() != Gdiplus::Ok) return nullptr;
    Gdiplus::Graphics g(art.get());
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.DrawImage(&src, Gdiplus::Rect(0, 0, kArtSize, kArtSize), (w - side) / 2, (h - side) / 2, side, side,
                Gdiplus::UnitPixel);
    return art;
}

// A position that only moved as far as `a` predicts (normal playback) is not a change.
bool SameSnapshot(const media::Snapshot& a, const media::Snapshot& b) {
    const int64_t drift = media::Position(a, b.stamp) - b.position;
    return a.hasMedia == b.hasMedia && a.playing == b.playing && a.title == b.title && a.artist == b.artist &&
           a.art == b.art && a.hasTimeline == b.hasTimeline && a.canSeek == b.canSeek && a.duration == b.duration &&
           drift < media::kTicksPerSecond && drift > -media::kTicksPerSecond;
}

void Publish(media::Snapshot next) {
    {
        std::lock_guard lk(g_lock);
        if (SameSnapshot(g_snapshot, next)) return;
        g_snapshot = std::move(next);
    }
    PostMessageW(g_notify, g_msg, 0, 0);
}

// Lives on the watcher thread only.
class Watcher {
public:
    void Poll() {
        media::Snapshot next;
        try {
            if (!m_manager) m_manager = Await(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            m_session = PickSession();
            if (m_session) ReadSession(next);
            else Forget();
        } catch (winrt::hresult_error const&) {
            // The media service restarted, or the player quit or hung mid-call: start over on the next poll.
            m_manager = nullptr;
            m_session = nullptr;
            m_playing.clear();
            Forget();
            next = {};
        }
        Publish(std::move(next));
    }

    void Execute(const Request& r) {
        if (!m_session) return;
        try {
            switch (r.cmd) {
                case media::Command::Previous: Await(m_session.TrySkipPreviousAsync()); break;
                case media::Command::TogglePlayPause: Await(m_session.TryTogglePlayPauseAsync()); break;
                case media::Command::Next: Await(m_session.TrySkipNextAsync()); break;
                case media::Command::Seek: Await(m_session.TryChangePlaybackPositionAsync(m_start + r.position)); break;
            }
        } catch (winrt::hresult_error const&) {
            m_session = nullptr;  // picked again by the next poll
        }
    }

private:
    // A player that just started playing first, so the latest one takes over; else the player shown
    // before while it plays; else any playing one; else the player shown before, so a pause does not
    // jump to another app; else any. Among several candidates, the one Windows considers current wins.
    // Players are told apart by app id, like m_app.
    GlobalSystemMediaTransportControlsSession PickSession() {
        auto current = m_manager.GetCurrentSession();
        const std::wstring currentApp = current ? std::wstring{ current.SourceAppUserModelId() } : std::wstring{};
        GlobalSystemMediaTransportControlsSession started{ nullptr }, playing{ nullptr }, shown{ nullptr }, any{ nullptr };
        bool shownPlaying = false;
        std::set<std::wstring> nowPlaying;
        for (auto const& s : m_manager.GetSessions()) {
            std::wstring app{ s.SourceAppUserModelId() };
            if (IsAnonymousHost(app)) continue;
            auto info = s.GetPlaybackInfo();
            const bool isPlaying =
                info && info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            const bool isCurrent = app == currentApp;
            if (isPlaying) {
                if (!m_playing.contains(app)) Prefer(started, s, isCurrent);
                Prefer(playing, s, isCurrent);
                nowPlaying.insert(app);
            }
            if (!shown && !m_app.empty() && app == m_app) {
                shown = s;
                shownPlaying = isPlaying;
            }
            Prefer(any, s, isCurrent);
        }
        m_playing = std::move(nowPlaying);
        if (started) return started;
        if (shown && shownPlaying) return shown;
        if (playing) return playing;
        return shown ? shown : any;
    }

    static void Prefer(GlobalSystemMediaTransportControlsSession& slot, GlobalSystemMediaTransportControlsSession const& s,
                       bool isCurrent) {
        if (!slot || isCurrent) slot = s;
    }

    // WebView2 publishes sessions under its own name, not the app's: game launchers and other apps
    // playing background sound. Nothing tells them apart from a player, so they are never shown.
    static bool IsAnonymousHost(std::wstring const& app) {
        return CompareStringOrdinal(app.c_str(), -1, L"msedgewebview2.exe", -1, TRUE) == CSTR_EQUAL;
    }

    void ReadSession(media::Snapshot& next) {
        m_app = m_session.SourceAppUserModelId();
        auto info = m_session.GetPlaybackInfo();
        next.hasMedia = true;
        next.playing = info && info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        ReadTimeline(next, info);
        auto props = Await(m_session.TryGetMediaPropertiesAsync());
        if (!props) return;
        next.title = Capped(props.Title());
        next.artist = Capped(props.Artist());
        std::wstring key = m_app + L'\n' + next.title + L'\n' + next.artist;
        if (key != m_artKey) {
            m_art = nullptr;
            // Some players publish the title before the thumbnail: only a track whose thumbnail was
            // there is marked done, so a missing one is asked for again on the next poll.
            if (auto thumb = props.Thumbnail()) {
                m_artKey = key;
                m_art = DecodeArt(Await(thumb.OpenReadAsync()));
            }
        }
        next.art = m_art;
    }

    void ReadTimeline(media::Snapshot& next, GlobalSystemMediaTransportControlsSessionPlaybackInfo const& info) {
        auto timeline = m_session.GetTimelineProperties();
        if (!timeline) return;
        m_start = timeline.StartTime().count();
        next.duration = timeline.EndTime().count() - m_start;
        if (next.duration <= 0) return;
        next.hasTimeline = true;
        next.canSeek = info && info.Controls() && info.Controls().IsPlaybackPositionEnabled();
        next.position = timeline.Position().count() - m_start;
        // Position is as of LastUpdatedTime, which can be seconds old while playing.
        const auto updated = timeline.LastUpdatedTime();
        if (next.playing && updated.time_since_epoch().count() > 0) {
            const int64_t since = (winrt::clock::now() - updated).count();
            if (since > 0) next.position += since;
        }
        next.position = std::clamp<int64_t>(next.position, 0, next.duration);
        next.stamp = GetTickCount64();
    }

    void Forget() {
        m_app.clear();
        m_artKey.clear();
        m_art = nullptr;
    }

    GlobalSystemMediaTransportControlsSessionManager m_manager{ nullptr };
    GlobalSystemMediaTransportControlsSession m_session{ nullptr };
    std::wstring m_app;     // SourceAppUserModelId of the player shown
    std::wstring m_artKey;  // the track m_art belongs to
    std::set<std::wstring> m_playing;  // app ids of the players that were playing at the last poll
    int64_t m_start = 0;    // timeline StartTime of the player shown: seek positions are relative to it
    std::shared_ptr<Gdiplus::Bitmap> m_art;
};

void Run() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    {
        Watcher watcher;
        DWORD wait = 0;
        while (g_running) {
            WaitForSingleObject(g_wake, wait);
            if (!g_running) break;
            std::vector<Request> commands;
            {
                std::lock_guard lk(g_lock);
                commands.swap(g_commands);
            }
            for (const Request& r : commands) watcher.Execute(r);
            if (!commands.empty()) Sleep(kAfterCommandMs);
            watcher.Poll();
            wait = kPollMs;
        }
    }
    winrt::uninit_apartment();
}

}  // namespace

namespace media {

void Start(HWND notify, UINT msg) {
    if (g_running) return;
    g_notify = notify;
    g_msg = msg;
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_wake) return;
    g_running = true;
    g_thread = std::thread(Run);
}

void Stop() {
    if (!g_running) return;
    g_running = false;
    SetEvent(g_wake);
    g_thread.join();
    CloseHandle(g_wake);
    g_wake = nullptr;
    std::lock_guard lk(g_lock);
    g_snapshot = {};
    g_commands.clear();
}

Snapshot Current() {
    std::lock_guard lk(g_lock);
    return g_snapshot;
}

void Send(Command cmd, int64_t position) {
    if (!g_running) return;
    {
        std::lock_guard lk(g_lock);
        g_commands.push_back({ cmd, position });
    }
    SetEvent(g_wake);
}

}  // namespace media
