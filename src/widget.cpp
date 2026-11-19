#include "widget.h"

#include "app.h"
#include "gdiplus_inc.h"
#include "icons.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Gdiplus;

constexpr wchar_t kClass[] = L"TaskbarMediaPlayerWidget";
constexpr UINT_PTR kScrollTimer = 1;
constexpr UINT kScrollFrameMs = 16;
constexpr int kScrollPauseFrames = 90;  // about 1.5 s still before each loop of a long title
constexpr int kScrollGap = 40;          // between a scrolling title and its next copy
constexpr UINT_PTR kProgressTimer = 2;
constexpr UINT kProgressMs = 500;

enum class Hit { None, Prev, Play, Next, Settings, Seek };

HWND g_wnd, g_taskbar, g_rebar, g_owner;
Settings g_settings;
media::Snapshot g_media;
bool g_light, g_hidden;
Hit g_hover = Hit::None, g_pressed = Hit::None;  // g_pressed: button under the mouse at button down
SIZE g_size{};
int g_lastLeft = INT_MIN;  // client x the strip was last placed at; INT_MIN = not placed
int g_lastDpi;
int g_wheel;               // wheel delta not yet turned into a volume step (touchpads send small deltas)

// Title scrolling, for titles wider than their space.
bool g_scrolling, g_scrollTimerOn;
int g_scroll, g_scrollPause = kScrollPauseFrames, g_titleW;

// Progress bar: redrawn on a timer while it is shown and playing; g_dragFraction follows a seek drag.
bool g_progressShown, g_progressTimerOn;
float g_dragFraction;

// Created on first paint, released in Destroy() (before GdiplusShutdown).
std::unique_ptr<FontFamily> g_textFamily;

int Dpi() {
    UINT d = g_taskbar ? GetDpiForWindow(g_taskbar) : 0;
    return d ? (int)d : 96;
}
int Px(int v) { return MulDiv(v, Dpi(), 96); }
float Pxf(float v) { return v * (float)Dpi() / 96.0f; }

std::unique_ptr<FontFamily> FirstFamily(std::initializer_list<const wchar_t*> names) {
    for (const wchar_t* name : names) {
        auto f = std::make_unique<FontFamily>(name);
        if (f->GetLastStatus() == Ok) return f;
    }
    return std::make_unique<FontFamily>(L"Segoe UI");
}

void LoadFonts() {
    if (!g_textFamily) g_textFamily = FirstFamily({ L"Segoe UI Variable Text", L"Segoe UI" });
}

Color Foreground() {
    switch (g_settings.theme) {
        case Theme::Light: return Color(255, 28, 28, 28);
        case Theme::Dark:
        case Theme::Transparent: return Color(255, 255, 255, 255);
        case Theme::Taskbar: break;
    }
    return g_light ? Color(255, 28, 28, 28) : Color(255, 255, 255, 255);
}

Color WithAlpha(const Color& c, BYTE a) { return Color(a, c.GetR(), c.GetG(), c.GetB()); }

void AddRoundedRect(GraphicsPath& path, const RectF& r, float radius) {
    const float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

// ---- layout -----------------------------------------------------------------------------------

// Strip width as a share of the taskbar's width, kept within bounds given in pixels at 100% scaling.
struct SizeSpec {
    float share;
    int minW, maxW;
};
constexpr SizeSpec kSizes[] = {
    { 0.08f, 120, 220 },  // SizeMode::Compact
    { 0.16f, 220, 420 },  // SizeMode::Classic
    { 0.24f, 300, 600 },  // SizeMode::Large
};

constexpr float kButtonPitch = 34;  // narrowest button cell, in pixels at 100%
constexpr float kEdgePad = 4, kRightPad = 2, kGap = 8;

// Height of capital letters as a share of the font size: Segoe UI's cap height is 1434 of its 2048 units.
constexpr float kCapRatio = 0.70f;

int ButtonCount() {
    return (int)g_settings.showPrev + (int)g_settings.showPlay + (int)g_settings.showNext + (int)g_settings.showSettings;
}

// A taller taskbar (two rows, small icons off) keeps the strip at one-row proportions.
float Band(int h) { return std::min((float)h, Pxf(48)); }
float ArtSize(int h) { return std::round(Band(h) - Pxf(10)); }

int StripWidth(int h) {
    RECT client{};
    GetClientRect(g_taskbar, &client);
    const SizeSpec& s = kSizes[(int)g_settings.size];
    const int w = (int)std::lround((float)(client.right - client.left) * s.share);
    // Never narrower than the artwork and buttons need.
    const float content = Pxf(kEdgePad) + (g_settings.showAlbumArt ? ArtSize(h) + Pxf(kGap) : 0) +
                          (float)ButtonCount() * Pxf(kButtonPitch) + Pxf(kRightPad);
    return std::max(std::clamp(w, Px(s.minW), Px(s.maxW)), (int)std::ceil(content));
}

std::wstring TitleText() {
    if (!g_settings.showTitle) return {};
    if (!g_media.hasMedia) return L"Aucun média";
    return g_media.title.empty() ? L"Sans titre" : g_media.title;
}

std::wstring ArtistText() { return g_settings.showArtist && g_media.hasMedia ? g_media.artist : std::wstring{}; }

struct Layout {
    bool artVisible = false;
    RectF art;
    float pitch = 0;                           // button cell width; a click anywhere in the cell counts
    float hoverW = 0, hoverH = 0;              // rounded hover / pressed plate, centred in the cell
    float prev = -1, play = -1, next = -1, settings = -1;  // button centre x; -1 = not shown
    // Text column; empty in compact mode.
    float textX = 0, textW = 0;
    float titleSize = 0, artistSize = 0;
    bool twoLines = false;                     // title over artist, else "Title · Artist" on one line
    float baseline = 0, lineGap = 0;           // first line's baseline; the artist's is baseline + lineGap
    // Progress bar: large mode, when the player publishes a timeline.
    bool barVisible = false, timesVisible = false;
    float barY = 0;                            // centre line
    float trackX = 0, trackW = 0;
    float timeSize = 0, timeW = 0;             // elapsed / total labels either side of the track
};

Layout ComputeLayout(int w, int h) {
    Layout l;
    const float cy = (float)h / 2;
    l.hoverW = Pxf(32);
    l.hoverH = std::clamp(Band(h) - Pxf(8), Pxf(20), Pxf(32));

    l.artVisible = g_settings.showAlbumArt;
    const float artSize = ArtSize(h);
    l.art = RectF(std::round(Pxf(kEdgePad)), std::round(((float)h - artSize) / 2), artSize, artSize);
    const float contentLeft = l.artVisible ? l.art.GetRight() + Pxf(kGap) : Pxf(kEdgePad);

    // Compact spreads the buttons over the room the artwork leaves; the other modes keep them together.
    const bool compact = g_settings.size == SizeMode::Compact;
    const int buttons = ButtonCount();
    l.pitch = Pxf(kButtonPitch);
    if (compact && buttons > 0) l.pitch = std::max(l.pitch, ((float)w - Pxf(kRightPad) - contentLeft) / (float)buttons);
    float x = (float)w - Pxf(kRightPad) - l.pitch / 2;
    auto place = [&](bool shown, float& centre) {
        if (!shown) return;
        centre = std::round(x);
        x -= l.pitch;
    };
    place(g_settings.showSettings, l.settings);
    place(g_settings.showNext, l.next);
    place(g_settings.showPlay, l.play);
    place(g_settings.showPrev, l.prev);
    if (compact) return l;

    const float buttonsLeft = x + l.pitch / 2;
    l.textX = std::round(contentLeft);
    l.textW = std::max(0.0f, buttonsLeft - Pxf(kGap) - l.textX);
    const bool hasTitle = !TitleText().empty(), hasArtist = !ArtistText().empty();
    l.titleSize = Pxf((float)g_settings.fontSize);
    l.artistSize = Pxf((float)std::max(Settings::kMinFont, g_settings.fontSize - 1));
    l.lineGap = std::round(l.artistSize * 1.3f + Pxf(2));
    l.barVisible = g_settings.size == SizeMode::Large && g_media.hasTimeline && l.textW >= Pxf(48);

    // The block runs from the first line's cap top to its last baseline, plus the bar below it; it is
    // centred as a whole. Two lines need the room for the block and some margin.
    const float barGap = Pxf(7);  // last baseline to the bar's centre line, clear of descenders
    const float below = l.barVisible ? barGap + Pxf(3) : l.artistSize * 0.25f;
    const float twoBlock = l.titleSize * kCapRatio + l.lineGap;
    l.twoLines = hasTitle && hasArtist && twoBlock + below + Pxf(8) <= (float)h;
    const float firstSize = hasTitle ? l.titleSize : l.artistSize;
    const float textBlock = l.twoLines ? twoBlock : hasTitle || hasArtist ? firstSize * kCapRatio : 0;
    const float block = textBlock + (l.barVisible && textBlock > 0 ? barGap : 0);
    l.baseline = std::round(cy - block / 2 + firstSize * kCapRatio);
    if (!l.barVisible) return l;

    l.barY = textBlock > 0 ? (l.twoLines ? l.baseline + l.lineGap : l.baseline) + barGap : std::round(cy);
    l.timeSize = Pxf(9);
    // Room for "h:mm:ss" or "mm:ss": Segoe UI digits are 0.565 em wide, a colon about 0.27 em.
    const int64_t secs = g_media.duration / media::kTicksPerSecond;
    const int digits = secs >= 3600 ? 5 : secs >= 600 ? 4 : 3, colons = secs >= 3600 ? 2 : 1;
    l.timeW = std::ceil(l.timeSize * (0.565f * (float)digits + 0.27f * (float)colons));
    l.timesVisible = l.textW >= Pxf(160);
    const float inset = l.timesVisible ? l.timeW + Pxf(6) : 0;
    l.trackX = l.textX + inset;
    l.trackW = l.textW - 2 * inset;
    return l;
}

Hit HitTest(int x, int y) {
    if (x < 0 || y < 0 || x >= g_size.cx || y >= g_size.cy) return Hit::None;  // captured moves outside the strip
    const Layout l = ComputeLayout(g_size.cx, g_size.cy);
    if (l.barVisible && g_media.canSeek && std::fabs((float)y - l.barY) <= Pxf(6) && (float)x >= l.trackX - Pxf(4) &&
        (float)x <= l.trackX + l.trackW + Pxf(4))
        return Hit::Seek;
    auto on = [&](float cx) { return cx >= 0 && std::fabs((float)x - cx) <= l.pitch / 2; };
    if (on(l.prev)) return Hit::Prev;
    if (on(l.play)) return Hit::Play;
    if (on(l.next)) return Hit::Next;
    if (on(l.settings)) return Hit::Settings;
    return Hit::None;
}

// Where along the track x falls, 0 to 1.
float TrackFraction(int x) {
    const Layout l = ComputeLayout(g_size.cx, g_size.cy);
    return l.trackW > 0 ? std::clamp(((float)x - l.trackX) / l.trackW, 0.0f, 1.0f) : 0.0f;
}

// ---- drawing ----------------------------------------------------------------------------------

void DrawArt(Graphics& g, const Layout& l, const Color& fg) {
    GraphicsPath path;
    AddRoundedRect(path, l.art, Pxf(3));
    if (Bitmap* art = g_media.art.get()) {
        // A texture brush gives the rounded corners smooth edges, which a clip region would not.
        TextureBrush brush(art);
        brush.TranslateTransform(l.art.X, l.art.Y);
        brush.ScaleTransform(l.art.Width / (REAL)art->GetWidth(), l.art.Height / (REAL)art->GetHeight());
        g.FillPath(&brush, &path);
        return;
    }
    SolidBrush plate(WithAlpha(fg, 30));
    g.FillPath(&plate, &path);
    SolidBrush glyph(WithAlpha(fg, 150));
    icons::Fill(g, icons::Icon::Music, l.art.Width * 0.6f, PointF(l.art.X + l.art.Width / 2, l.art.Y + l.art.Height / 2), glyph);
}

void DrawButtons(Graphics& g, const Layout& l, const Color& fg, int h) {
    SolidBrush icon(fg), dimIcon(WithAlpha(fg, 150)), hover(WithAlpha(fg, 28)), pressed(WithAlpha(fg, 16));
    const float cy = (float)h / 2;
    auto button = [&](float cx, Hit hit, icons::Icon glyph, float size, bool dim) {
        if (cx < 0) return;
        const bool down = g_pressed == hit && g_hover == hit;
        if (down || (g_hover == hit && g_pressed == Hit::None)) {
            GraphicsPath plate;
            AddRoundedRect(plate, RectF(cx - l.hoverW / 2, std::round(cy - l.hoverH / 2), l.hoverW, l.hoverH), Pxf(4));
            g.FillPath(down ? &pressed : &hover, &plate);
        }
        const bool quiet = (dim && g_hover != hit) || down;
        icons::Fill(g, glyph, Pxf(size), PointF(cx, cy), quiet ? dimIcon : icon);
    };
    // Sizes are view boxes in pixels at 100%: the shapes fill about three quarters of theirs.
    button(l.prev, Hit::Prev, icons::Icon::Prev, 14, false);
    button(l.play, Hit::Play, g_media.playing ? icons::Icon::Pause : icons::Icon::Play, 17, false);
    button(l.next, Hit::Next, icons::Icon::Next, 14, false);
    button(l.settings, Hit::Settings, icons::Icon::Settings, 14, true);
}

float Ascent(const Font& font) {
    FontFamily family;
    font.GetFamily(&family);
    const INT style = font.GetStyle();
    return font.GetSize() * (float)family.GetCellAscent(style) / (float)family.GetEmHeight(style);
}

// Typographic layout (no GDI+ padding around the text) on one line, ending with an ellipsis when clipped.
void SetLineFormat(StringFormat& f) {
    f.SetFormatFlags(f.GetFormatFlags() | StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    f.SetTrimming(StringTrimmingEllipsisCharacter);
}

struct Run {
    std::wstring text;
    const Font* font;
    const Brush* brush;
};

float RunWidth(Graphics& g, const Run& r, const StringFormat& f) {
    RectF b;
    g.MeasureString(r.text.c_str(), (INT)r.text.size(), r.font, PointF(0, 0), &f, &b);
    return b.Width;
}

float RunsWidth(Graphics& g, const std::vector<Run>& runs, const StringFormat& f) {
    float w = 0;
    for (const Run& r : runs) w += RunWidth(g, r, f);
    return w;
}

// Draws the runs one after the other on a shared baseline.
void DrawRuns(Graphics& g, const std::vector<Run>& runs, const StringFormat& f, float x, float baseline) {
    for (const Run& r : runs) {
        g.DrawString(r.text.c_str(), (INT)r.text.size(), r.font, PointF(x, baseline - Ascent(*r.font)), &f, r.brush);
        x += RunWidth(g, r, f);
    }
}

void DrawText(Graphics& g, const Layout& l, const Color& fg, int h) {
    g_scrolling = false;
    const std::wstring title = TitleText(), artist = ArtistText();
    if (l.textW <= 0 || (title.empty() && artist.empty())) return;

    Font titleFont(g_textFamily.get(), l.titleSize, FontStyleBold, UnitPixel);
    Font artistFont(g_textFamily.get(), l.artistSize, FontStyleRegular, UnitPixel);
    SolidBrush titleBrush(fg), artistBrush(WithAlpha(fg, 165));
    StringFormat format(StringFormat::GenericTypographic());
    SetLineFormat(format);

    std::vector<Run> first;  // the line that scrolls when too long
    if (l.twoLines) {
        first = { { title, &titleFont, &titleBrush } };
    } else {
        // Small taskbar icons leave room for one line only: "Title · Artist" scrolls as a whole.
        if (!title.empty()) first.push_back({ title, &titleFont, &titleBrush });
        if (!title.empty() && !artist.empty()) first.push_back({ L"  ·  ", &artistFont, &artistBrush });
        if (!artist.empty()) first.push_back({ artist, &artistFont, &artistBrush });
    }

    g.SetClip(RectF(l.textX, 0, l.textW, (float)h));
    g_titleW = (int)std::ceil(RunsWidth(g, first, format));
    g_scrolling = (float)g_titleW > l.textW;
    if (g_scrolling) {
        const float x = l.textX - (float)g_scroll;
        DrawRuns(g, first, format, x, l.baseline);
        DrawRuns(g, first, format, x + (float)(g_titleW + Px(kScrollGap)), l.baseline);
    } else {
        DrawRuns(g, first, format, l.textX, l.baseline);
    }
    if (l.twoLines) {
        const float top = l.baseline + l.lineGap - Ascent(artistFont);
        g.DrawString(artist.c_str(), (INT)artist.size(), &artistFont, RectF(l.textX, top, l.textW, (float)h - top), &format,
                     &artistBrush);
    }
    g.ResetClip();
}

std::wstring FormatTime(int64_t ticks) {
    const int64_t s = ticks / media::kTicksPerSecond;
    wchar_t buf[24];
    if (s >= 3600) swprintf_s(buf, L"%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
    else swprintf_s(buf, L"%lld:%02lld", s / 60, s % 60);
    return buf;
}

void DrawProgress(Graphics& g, const Layout& l, const Color& fg) {
    if (!l.barVisible || l.trackW <= 0) return;
    const bool dragging = g_pressed == Hit::Seek;
    const bool hot = dragging || (g_hover == Hit::Seek && g_pressed == Hit::None);
    const int64_t position = dragging ? (int64_t)((double)g_dragFraction * (double)g_media.duration)
                                      : media::Position(g_media, GetTickCount64());
    const float fraction = (float)((double)position / (double)g_media.duration);

    const float t = std::round(hot ? Pxf(5) : Pxf(3));
    const RectF track(l.trackX, std::round(l.barY - t / 2), l.trackW, t);
    GraphicsPath rail, fill;
    AddRoundedRect(rail, track, t / 2);
    AddRoundedRect(fill, RectF(track.X, track.Y, std::max(t, track.Width * fraction), t), t / 2);
    SolidBrush railBrush(WithAlpha(fg, 55)), fillBrush(WithAlpha(fg, hot ? 255 : 210));
    g.FillPath(&railBrush, &rail);
    g.FillPath(&fillBrush, &fill);
    if (hot) {
        const float r = Pxf(5);
        g.FillEllipse(&fillBrush, RectF(track.X + track.Width * fraction - r, l.barY - r, 2 * r, 2 * r));
    }
    if (!l.timesVisible) return;

    Font font(g_textFamily.get(), l.timeSize, FontStyleRegular, UnitPixel);
    StringFormat format(StringFormat::GenericTypographic());
    SetLineFormat(format);
    SolidBrush brush(WithAlpha(fg, 165));
    const float baseline = std::round(l.barY + l.timeSize * kCapRatio / 2);
    const Run elapsed{ FormatTime(position), &font, &brush }, total{ FormatTime(g_media.duration), &font, &brush };
    DrawRuns(g, { elapsed }, format, l.trackX - Pxf(6) - RunWidth(g, elapsed, format), baseline);
    DrawRuns(g, { total }, format, l.trackX + l.trackW + Pxf(6), baseline);
}

void Draw(Graphics& g, int w, int h) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    // Alpha 1, not 0: fully transparent pixels of a layered window let clicks fall through to the taskbar.
    g.Clear(Color(1, 0, 0, 0));
    if (g_settings.theme == Theme::Light || g_settings.theme == Theme::Dark) {
        GraphicsPath plate;
        AddRoundedRect(plate, RectF(Pxf(2), Pxf(4), (float)w - Pxf(4), (float)h - Pxf(8)), Pxf(6));
        SolidBrush brush(g_settings.theme == Theme::Light ? Color(255, 243, 243, 243) : Color(255, 32, 32, 32));
        g.FillPath(&brush, &plate);
    }
    const Color fg = Foreground();
    const Layout l = ComputeLayout(w, h);
    if (l.artVisible) DrawArt(g, l, fg);
    DrawButtons(g, l, fg, h);
    DrawText(g, l, fg, h);
    DrawProgress(g, l, fg);
    g_progressShown = l.barVisible;
}

void SetTimerState(UINT_PTR id, UINT ms, bool want, bool& on) {
    if (want == on) return;
    on = want;
    if (want) SetTimer(g_wnd, id, ms, nullptr);
    else if (g_wnd) KillTimer(g_wnd, id);
}

void UpdateTimers() {
    const bool visible = g_wnd && IsWindowVisible(g_wnd);
    const bool wasScrolling = g_scrollTimerOn;
    SetTimerState(kScrollTimer, kScrollFrameMs, visible && g_scrolling, g_scrollTimerOn);
    if (wasScrolling && !g_scrollTimerOn) {
        g_scroll = 0;
        g_scrollPause = kScrollPauseFrames;
    }
    SetTimerState(kProgressTimer, kProgressMs, visible && g_progressShown && g_media.playing, g_progressTimerOn);
}

// Renders the strip into a premultiplied bitmap and hands it to the layered window.
void Paint() {
    if (!g_wnd) return;
    LoadFonts();
    RECT client{};
    GetClientRect(g_taskbar, &client);
    const int h = client.bottom - client.top;
    if (h <= 0) return;
    const int w = StripWidth(h);
    g_size = { w, h };

    Bitmap canvas(w, h, PixelFormat32bppPARGB);
    if (canvas.GetLastStatus() != Ok) return;
    {
        Graphics g(&canvas);
        Draw(g, w, h);
    }
    BitmapData data{};
    Rect all(0, 0, w, h);
    if (canvas.LockBits(&all, ImageLockModeRead, PixelFormat32bppPARGB, &data) != Ok) return;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    if (HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)) {
        for (int y = 0; y < h; y++)
            memcpy(static_cast<BYTE*>(bits) + (size_t)y * w * 4, static_cast<const BYTE*>(data.Scan0) + (size_t)y * data.Stride,
                   (size_t)w * 4);
        HGDIOBJ old = SelectObject(mem, dib);
        SIZE size{ w, h };
        POINT src{ 0, 0 };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(g_wnd, screen, nullptr, &size, mem, &src, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
        DeleteObject(dib);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    canvas.UnlockBits(&data);
    UpdateTimers();
}

// ---- placement --------------------------------------------------------------------------------

// Windows 10: let Explorer lay the task buttons out over the full width again.
void RestoreTaskButtons() {
    if (!g_rebar || !IsWindow(g_rebar) || !IsWindow(g_taskbar)) return;
    RECT r{};
    GetClientRect(g_taskbar, &r);
    PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
}

// Windows 10: the task buttons live in ReBarWindow32 and would slide under the strip; Explorer widens
// them again on its own layout passes, so this runs on every Reposition().
void NarrowTaskButtons(int left) {
    if (!g_rebar || !IsWindow(g_rebar)) return;
    RECT rr{};
    GetWindowRect(g_rebar, &rr);
    POINT tl{ rr.left, rr.top };
    ScreenToClient(g_taskbar, &tl);
    if (tl.x + (rr.right - rr.left) > left && left > tl.x)
        MoveWindow(g_rebar, tl.x, tl.y, left - tl.x, rr.bottom - rr.top, TRUE);
}

// Other apps can dock their own strip left of the tray the same way (BatteryHub, for one). Their windows
// are taskbar children owned by neither Explorer nor us: move left of any that overlaps where we would go.
int LeftOfOtherStrips(int edge) {
    DWORD explorer = 0;
    GetWindowThreadProcessId(g_taskbar, &explorer);
    const DWORD self = GetCurrentProcessId();
    for (bool moved = true; moved;) {
        moved = false;
        for (HWND c = FindWindowExW(g_taskbar, nullptr, nullptr, nullptr); c; c = FindWindowExW(g_taskbar, c, nullptr, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(c, &pid);
            RECT r{};
            if (c == g_wnd || pid == explorer || pid == self || !IsWindowVisible(c) || !GetWindowRect(c, &r)) continue;
            if (r.right > r.left && r.left < edge && r.right > edge - g_size.cx - Px(2)) {
                edge = r.left;
                moved = true;
            }
        }
    }
    return edge;
}

void Hide() {
    if (g_wnd && IsWindowVisible(g_wnd)) {
        ShowWindow(g_wnd, SW_HIDE);
        RestoreTaskButtons();
    }
    g_lastLeft = INT_MIN;
    UpdateTimers();
}

// ---- input ------------------------------------------------------------------------------------

void SetHover(Hit hit) {
    if (hit == g_hover) return;
    g_hover = hit;
    Paint();
}

void VolumeStep(bool up) {
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = up ? VK_VOLUME_UP : VK_VOLUME_DOWN;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void OnScrollFrame() {
    if (!g_scrolling) return;
    if (g_scrollPause > 0) {
        g_scrollPause--;
        return;
    }
    g_scroll += std::max(1, Px(1));
    if (g_scroll >= g_titleW + Px(kScrollGap)) {
        g_scroll = 0;
        g_scrollPause = kScrollPauseFrames;
    }
    Paint();
}

LRESULT CALLBACK WidgetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            if (g_pressed == Hit::Seek) {
                g_dragFraction = TrackFraction((short)LOWORD(lp));
                Paint();
                return 0;
            }
            SetHover(HitTest((short)LOWORD(lp), (short)HIWORD(lp)));
            return 0;
        }
        case WM_MOUSELEAVE:
            SetHover(Hit::None);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, g_hover != Hit::None ? IDC_HAND : IDC_ARROW));
            return TRUE;
        case WM_LBUTTONDOWN:
            g_pressed = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
            if (g_pressed == Hit::Seek) g_dragFraction = TrackFraction((short)LOWORD(lp));
            if (g_pressed != Hit::None) SetCapture(h);
            Paint();
            return 0;
        case WM_LBUTTONUP: {
            // A click counts only when the button is released over the one it started on.
            const Hit hit = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
            const Hit pressed = g_pressed;
            g_pressed = Hit::None;
            if (GetCapture() == h) ReleaseCapture();
            if (pressed == Hit::Seek) {
                // Released anywhere: seek where the drag ended, and show it before the player confirms.
                g_media.position = (int64_t)((double)g_dragFraction * (double)g_media.duration);
                g_media.stamp = GetTickCount64();
                media::Send(media::Command::Seek, g_media.position);
            } else if (hit == pressed) {
                switch (hit) {
                    case Hit::Prev: media::Send(media::Command::Previous); break;
                    case Hit::Play: media::Send(media::Command::TogglePlayPause); break;
                    case Hit::Next: media::Send(media::Command::Next); break;
                    case Hit::Settings: PostMessageW(g_owner, app::WM_SHOW_SETTINGS, 0, 0); break;
                    case Hit::Seek:
                    case Hit::None: break;
                }
            }
            Paint();
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (g_pressed != Hit::None) {
                g_pressed = Hit::None;
                Paint();
            }
            return 0;
        case WM_RBUTTONUP:
            PostMessageW(g_owner, app::WM_SHOW_MENU, 0, 0);
            return 0;
        case WM_MOUSEWHEEL:
            g_wheel += GET_WHEEL_DELTA_WPARAM(wp);
            for (; g_wheel >= WHEEL_DELTA; g_wheel -= WHEEL_DELTA) VolumeStep(true);
            for (; g_wheel <= -WHEEL_DELTA; g_wheel += WHEEL_DELTA) VolumeStep(false);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_TIMER:
            if (wp == kScrollTimer) OnScrollFrame();
            else if (wp == kProgressTimer) Paint();
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace widget {

bool Create(HINSTANCE inst, HWND owner) {
    Destroy();
    HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!bar || !FindWindowExW(bar, nullptr, L"TrayNotifyWnd", nullptr)) return false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WidgetProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, bar,
                            nullptr, inst, nullptr);
    if (!g_wnd) return false;
    g_taskbar = bar;
    g_owner = owner;
    // Windows 11's taskbar is a XAML island; Windows 10 keeps its task buttons in ReBarWindow32.
    g_rebar = FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)
                  ? nullptr
                  : FindWindowExW(bar, nullptr, L"ReBarWindow32", nullptr);
    g_lastDpi = Dpi();
    g_lastLeft = INT_MIN;
    Paint();
    Reposition();
    return true;
}

void Destroy() {
    if (g_wnd && IsWindow(g_wnd)) {
        if (IsWindowVisible(g_wnd)) RestoreTaskButtons();
        DestroyWindow(g_wnd);
    }
    g_wnd = g_taskbar = g_rebar = nullptr;
    g_hover = g_pressed = Hit::None;
    g_scrollTimerOn = g_scrolling = g_progressTimerOn = g_progressShown = false;
    g_scroll = 0;
    g_scrollPause = kScrollPauseFrames;
    g_textFamily.reset();
}

bool Alive() { return g_wnd && IsWindow(g_wnd) && g_taskbar && IsWindow(g_taskbar); }

void Apply(const Settings& s) {
    g_settings = s;
    g_scroll = 0;
    g_scrollPause = kScrollPauseFrames;
    Paint();
    Reposition();
}

void SetMedia(const media::Snapshot& m) {
    if (m.title != g_media.title) {
        g_scroll = 0;
        g_scrollPause = kScrollPauseFrames;
        // A seek drag started on the previous track must not seek in this one.
        if (g_pressed == Hit::Seek) {
            g_pressed = Hit::None;
            if (GetCapture() == g_wnd) ReleaseCapture();
        }
    }
    g_media = m;
    Paint();
}

void SetLightTheme(bool light) {
    g_light = light;
    Paint();
}

void SetHidden(bool hidden) {
    g_hidden = hidden;
    Reposition();
}

void Reposition() {
    if (!Alive()) return;
    RECT bar{}, client{};
    GetWindowRect(g_taskbar, &bar);
    GetClientRect(g_taskbar, &client);
    HWND tray = FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    const bool vertical = bar.bottom - bar.top > bar.right - bar.left;
    const bool rtl = (GetWindowLongW(g_taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    // Left / right and right-to-left taskbars are not laid out: stay hidden rather than cover something.
    if (g_hidden || vertical || rtl || !tray) {
        Hide();
        return;
    }
    const int height = client.bottom - client.top;
    if (Dpi() != g_lastDpi || height != g_size.cy || StripWidth(height) != g_size.cx) {
        g_lastDpi = Dpi();
        Paint();
    }
    RECT tr{};
    GetWindowRect(tray, &tr);
    int edge = tr.left;
    // Windows 10: News and Interests sits just left of the tray (ShellFeedsTaskbarViewWindow or, on
    // 19045, DynamicContent2). Its width follows the weather text, so it is read on every pass.
    for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
        HWND feeds = FindWindowExW(g_taskbar, nullptr, cls, nullptr);
        RECT fr{};
        if (feeds && IsWindowVisible(feeds) && GetWindowRect(feeds, &fr) && fr.left < edge && fr.right > fr.left)
            edge = fr.left;
    }
    edge = LeftOfOtherStrips(edge);
    POINT p{ edge, bar.top };
    ScreenToClient(g_taskbar, &p);
    const int left = p.x - g_size.cx - Px(2);
    if (left < Px(48)) {  // no room left of the tray: never cover the Start button
        Hide();
        return;
    }
    NarrowTaskButtons(left);
    // Keep above the taskbar's own child windows.
    const bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
    if (!covered && left == g_lastLeft && IsWindowVisible(g_wnd)) return;
    g_lastLeft = left;
    SetWindowPos(g_wnd, HWND_TOP, left, 0, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateTimers();
}

}  // namespace widget
