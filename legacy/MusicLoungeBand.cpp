#include "MusicLoungeBand.h"
#include <dwmapi.h>
#include <shcore.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <commctrl.h>
#include <new>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;
using namespace std;
using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

extern const CLSID CLSID_MusicLoungeBand;

static ULONG_PTR g_gdiplusToken    = 0;
static int       g_gdiplusRefCount = 0;
static CRITICAL_SECTION g_gdiplusCS;

const wchar_t* MusicLoungeBand::BAND_CLASS    = L"MusicLounge_DeskBand";
const wchar_t* MusicLoungeBand::OVERLAY_CLASS = L"MusicLounge_Overlay";
bool           MusicLoungeBand::s_bandClassRegistered    = false;
bool           MusicLoungeBand::s_overlayClassRegistered = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool IsSystemLightMode() {
    DWORD val = 0, size = sizeof(val);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_DWORD, nullptr, &val, &size);
    return val != 0;
}

static void AddRoundedRect(GraphicsPath& path, int x, int y, int w, int h, int r) {
    int d = r * 2;
    path.AddArc(x,         y,         d, d, 180, 90);
    path.AddArc(x + w - d, y,         d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    path.AddArc(x,         y + h - d, d, d,  90, 90);
    path.CloseFigure();
}

static Bitmap* StreamToBitmap(IRandomAccessStreamWithContentType const& stream) {
    if (!stream) return nullptr;
    IStream* ns = nullptr;
    if (SUCCEEDED(CreateStreamOverRandomAccessStream(
            reinterpret_cast<IUnknown*>(winrt::get_abi(stream)), IID_PPV_ARGS(&ns)))) {
        Bitmap* bmp = Bitmap::FromStream(ns);
        ns->Release();
        if (bmp && bmp->GetLastStatus() == Ok) return bmp;
        delete bmp;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
MusicLoungeBand::MusicLoungeBand() {
    InterlockedIncrement(&g_cDllRef);
    LoadBandSettings(m_settings);

    if (g_gdiplusRefCount++ == 0) {
        InitializeCriticalSection(&g_gdiplusCS);
        GdiplusStartupInput gsi;
        GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);
    }

    if (!s_bandClassRegistered) {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = BandWndProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = BAND_CLASS;
        RegisterClassW(&wc);
        s_bandClassRegistered = true;
    }
    if (!s_overlayClassRegistered) {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = OverlayWndProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_HAND);
        wc.lpszClassName = OVERLAY_CLASS;
        RegisterClassW(&wc);
        s_overlayClassRegistered = true;
    }
}

MusicLoungeBand::~MusicLoungeBand() {
    StopMediaThread();
    if (m_pSite) { m_pSite->Release(); m_pSite = nullptr; }

    if (--g_gdiplusRefCount == 0) {
        GdiplusShutdown(g_gdiplusToken);
        DeleteCriticalSection(&g_gdiplusCS);
    }
    InterlockedDecrement(&g_cDllRef);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------
HRESULT MusicLoungeBand::QueryInterface(REFIID riid, void** ppv) {
    static const QITAB qit[] = {
        QITABENT(MusicLoungeBand, IDeskBand),
        QITABENT(MusicLoungeBand, IDockingWindow),
        QITABENT(MusicLoungeBand, IOleWindow),
        QITABENT(MusicLoungeBand, IObjectWithSite),
        QITABENT(MusicLoungeBand, IPersistStream),
        QITABENT(MusicLoungeBand, IPersist),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}
ULONG MusicLoungeBand::AddRef()  { return InterlockedIncrement(&m_ref); }
ULONG MusicLoungeBand::Release() {
    long r = InterlockedDecrement(&m_ref); if (!r) delete this; return r;
}

// ---------------------------------------------------------------------------
// IOleWindow
// ---------------------------------------------------------------------------
HRESULT MusicLoungeBand::GetWindow(HWND* phwnd) {
    if (!phwnd) return E_POINTER;
    *phwnd = m_hWnd; return m_hWnd ? S_OK : E_FAIL;
}

// ---------------------------------------------------------------------------
// IDockingWindow
// ---------------------------------------------------------------------------
HRESULT MusicLoungeBand::ShowDW(BOOL fShow) {
    if (m_hOverlay) ShowWindow(m_hOverlay, fShow ? SW_SHOW : SW_HIDE);
    return S_OK;
}

HRESULT MusicLoungeBand::CloseDW(DWORD) {
    CloseSettingsDialog();
    StopMediaThread();
    if (m_hOverlay && IsWindow(m_hOverlay)) {
        DestroyWindow(m_hOverlay);
        m_hOverlay = nullptr;
    }
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDeskBand — advertises 1px width; overlay does the actual visual work
// ---------------------------------------------------------------------------
HRESULT MusicLoungeBand::GetBandInfo(DWORD dwBandID, DWORD /*dwViewMode*/, DESKBANDINFO* pdbi) {
    if (!pdbi) return E_POINTER;
    m_dwBandID = dwBandID;

    if (pdbi->dwMask & DBIM_MINSIZE)  { pdbi->ptMinSize.x = 1;  pdbi->ptMinSize.y = 40; }
    if (pdbi->dwMask & DBIM_MAXSIZE)  { pdbi->ptMaxSize.x = 1;  pdbi->ptMaxSize.y = -1; }
    if (pdbi->dwMask & DBIM_INTEGRAL) { pdbi->ptIntegral.x = 1; pdbi->ptIntegral.y = 1; }
    if (pdbi->dwMask & DBIM_ACTUAL)   { pdbi->ptActual.x  = 1;  pdbi->ptActual.y  = 40; }
    if (pdbi->dwMask & DBIM_TITLE)    { pdbi->wszTitle[0] = L'\0'; }
    if (pdbi->dwMask & DBIM_MODEFLAGS) {
        pdbi->dwModeFlags = DBIMF_VARIABLEHEIGHT | DBIMF_NOGRIPPER | DBIMF_NOMARGINS;
    }
    if (pdbi->dwMask & DBIM_BKCOLOR)  { pdbi->dwMask &= ~DBIM_BKCOLOR; }
    return S_OK;
}

// ---------------------------------------------------------------------------
// IObjectWithSite
// ---------------------------------------------------------------------------
HRESULT MusicLoungeBand::SetSite(IUnknown* pUnkSite) {
    if (m_pSite) { m_pSite->Release(); m_pSite = nullptr; }
    if (m_hOverlay && IsWindow(m_hOverlay)) { DestroyWindow(m_hOverlay); m_hOverlay = nullptr; }
    if (m_hWnd && IsWindow(m_hWnd))         { DestroyWindow(m_hWnd);     m_hWnd     = nullptr; }
    StopMediaThread();

    if (!pUnkSite) return S_OK;

    IOleWindow* pOleWnd = nullptr;
    HRESULT hr = pUnkSite->QueryInterface(IID_IOleWindow, (void**)&pOleWnd);
    if (FAILED(hr)) return hr;
    pOleWnd->GetWindow(&m_hParent);
    pOleWnd->Release();

    m_pSite = pUnkSite;
    m_pSite->AddRef();

    CreateBandWindow(m_hParent);
    return S_OK;
}

HRESULT MusicLoungeBand::GetSite(REFIID riid, void** ppvSite) {
    if (!m_pSite) { *ppvSite = nullptr; return E_FAIL; }
    return m_pSite->QueryInterface(riid, ppvSite);
}

// ---------------------------------------------------------------------------
// IPersist
// ---------------------------------------------------------------------------
HRESULT MusicLoungeBand::GetClassID(CLSID* pClassID) {
    *pClassID = CLSID_MusicLoungeBand; return S_OK;
}

// ---------------------------------------------------------------------------
// Window creation
// ---------------------------------------------------------------------------
void MusicLoungeBand::CreateBandWindow(HWND hParent) {
    // 1px invisible placeholder window in the rebar — just for COM lifecycle/message pump
    m_hWnd = CreateWindowExW(
        0, BAND_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        hParent, nullptr, g_hInst, this
    );
    if (m_hWnd) {
        StartMediaThread();
        SetTimer(m_hWnd, 1001, 500, nullptr);
        CreateOverlayWindow();
    }
}

void MusicLoungeBand::CreateOverlayWindow() {
    HWND hShell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hShell) return;

    RECT rcShell;
    GetWindowRect(hShell, &rcShell);
    int taskbarH = rcShell.bottom - rcShell.top;

    m_hOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE,
        OVERLAY_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, m_settings.width, taskbarH,
        hShell, nullptr, g_hInst, this
    );

    if (m_hOverlay) {
        PositionOverlay();
        RepaintOverlay();
    }
}

// ---------------------------------------------------------------------------
// Overlay positioning — just left of TrayNotifyWnd
// ---------------------------------------------------------------------------
void MusicLoungeBand::PositionOverlay() {
    if (!m_hOverlay || !IsWindow(m_hOverlay)) return;
    HWND hShell = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hShell) return;
    HWND hTray = FindWindowExW(hShell, nullptr, L"TrayNotifyWnd", nullptr);
    if (!hTray) return;

    RECT rcShell, rcTray;
    GetWindowRect(hShell, &rcShell);
    GetWindowRect(hTray,  &rcTray);

    int taskbarH = rcShell.bottom - rcShell.top;
    int overlayX = rcTray.left - rcShell.left - m_settings.width;
    if (overlayX < 0) overlayX = 0;

    SetWindowPos(m_hOverlay, HWND_TOP, overlayX, 0, m_settings.width, taskbarH,
                 SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// Overlay painting — per-pixel alpha via UpdateLayeredWindow
// ---------------------------------------------------------------------------
void MusicLoungeBand::RepaintOverlay() {
    if (!m_hOverlay || !IsWindow(m_hOverlay)) return;
    RECT rc; GetClientRect(m_hOverlay, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    // 32-bit DIB so GDI+ writes alpha channel correctly
    BITMAPINFOHEADER bih = {};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = w;
    bih.biHeight      = -h;   // top-down
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hDib = CreateDIBSection(nullptr, (BITMAPINFO*)&bih, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hDib) return;

    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hDib);

    DrawContent(hdcMem, w, h);

    // Premultiply alpha (GDI+ writes straight alpha; UpdateLayeredWindow needs premultiplied)
    if (pvBits) {
        BYTE* p = (BYTE*)pvBits;
        for (int i = 0; i < w * h; i++, p += 4) {
            BYTE a = p[3];
            if (a == 255 || a == 0) { if (a == 0) p[0] = p[1] = p[2] = 0; continue; }
            p[0] = (BYTE)((p[0] * a) / 255);
            p[1] = (BYTE)((p[1] * a) / 255);
            p[2] = (BYTE)((p[2] * a) / 255);
        }
    }

    POINT ptSrc = {0, 0};
    SIZE  szWnd = {w, h};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC hdcScreen = GetDC(nullptr);
    UpdateLayeredWindow(m_hOverlay, hdcScreen, nullptr, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);

    SelectObject(hdcMem, hOld);
    DeleteObject(hDib);
    DeleteDC(hdcMem);

    // Manage scroll timer
    if (m_isScrolling && !m_scroll1002Active) {
        SetTimer(m_hWnd, 1002, 16, nullptr);
        m_scroll1002Active = true;
    } else if (!m_isScrolling && m_scroll1002Active) {
        KillTimer(m_hWnd, 1002);
        m_scroll1002Active = false;
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
LayoutInfo MusicLoungeBand::ComputeLayout(int w, int h) const {
    LayoutInfo l{};
    l.circleR = 14.0f;
    l.iconW   =  9.0f;
    l.iconH   = 13.0f;
    const float gap  = 32.0f;
    const int artPad = 6;
    const int bMar   = 10;
    const float tMar = 12.0f;

    l.artVisible = m_settings.showAlbumArt;
    l.artSize    = h - 12;
    l.artY       = 6;
    l.artX       = artPad;
    l.prevX = l.playX = l.nextX = l.settingsX = -1.0f;

    float cur = (float)(w - artPad);
    l.settingsX = cur - l.circleR;
    cur -= gap;

    if (m_settings.showNext) { l.nextX = cur - l.circleR; cur -= gap; }
    if (m_settings.showPlay) { l.playX = cur - l.circleR; cur -= gap; }
    if (m_settings.showPrev) { l.prevX = cur - l.circleR; cur -= gap; }

    float btnLeft = cur + gap - l.circleR * 2;
    int artEnd = artPad + (l.artVisible ? l.artSize + bMar : bMar / 2);
    l.textX    = artEnd;
    l.textMaxW = (int)(btnLeft - tMar) - l.textX;
    if (l.textMaxW < 0) l.textMaxW = 0;
    return l;
}

// ---------------------------------------------------------------------------
// Rendering — draws into a 32bpp DC (alpha channel preserved)
// ---------------------------------------------------------------------------
void MusicLoungeBand::DrawContent(HDC hdc, int w, int h) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    // Background: opaque solid for Clair/Sombre, fully transparent for Taskbar/Transparent
    // Transparent bg means the real taskbar (e.g. user's frosted glass) shows through
    switch (m_settings.theme) {
    case 1:  g.Clear(Color(255, 243, 243, 243)); break; // Clair: white opaque
    case 2:  g.Clear(Color(255,  30,  30,  30)); break; // Sombre: dark opaque
    default: g.Clear(Color(  0,   0,   0,   0)); break; // Taskbar/Transparent: see-through
    }

    Color mainColor;
    switch (m_settings.theme) {
    case 1:  mainColor = Color(255,  30,  30,  30); break; // Clair: dark text
    case 2:  mainColor = Color(255, 255, 255, 255); break; // Sombre: white
    case 3:  mainColor = Color(255, 255, 255, 255); break; // Transparent: white
    default: mainColor = IsSystemLightMode()
                 ? Color(255,  30,  30,  30)
                 : Color(255, 255, 255, 255);
             break;
    }

    // Snapshot media state
    wstring title, artist;
    bool isPlaying = false;
    Bitmap* art = nullptr;
    {
        lock_guard<mutex> lk(m_media.lock);
        title     = m_settings.showTitle  ? m_media.title  : L"";
        artist    = m_settings.showArtist ? m_media.artist : L"";
        isPlaying = m_media.isPlaying;
        if (m_media.albumArt) {
            art = m_media.albumArt->Clone(
                Rect(0, 0, (INT)m_media.albumArt->GetWidth(), (INT)m_media.albumArt->GetHeight()),
                m_media.albumArt->GetPixelFormat());
        }
    }

    LayoutInfo L = ComputeLayout(w, h);
    int cy = h / 2;

    // 1. Album art
    if (L.artVisible) {
        GraphicsPath artPath;
        AddRoundedRect(artPath, L.artX, L.artY, L.artSize, L.artSize, 6);
        if (art) {
            g.SetClip(&artPath);
            g.DrawImage(art, L.artX, L.artY, L.artSize, L.artSize);
            g.ResetClip();
            delete art; art = nullptr;
        } else {
            SolidBrush ph(Color(60, 128, 128, 128));
            g.FillPath(&ph, &artPath);
        }
    } else if (art) { delete art; art = nullptr; }

    // 2. Buttons — Segoe MDL2 Assets glyphs
    SolidBrush iconBrush(mainColor);
    SolidBrush hoverBrush(Color(255, mainColor.GetRed(), mainColor.GetGreen(), mainColor.GetBlue()));
    SolidBrush bgBrush(Color(50, mainColor.GetRed(), mainColor.GetGreen(), mainColor.GetBlue()));

    FontFamily mdl2(L"Segoe MDL2 Assets");
    float iconEm = L.circleR * 1.05f;
    Font iconFont(&mdl2, iconEm, FontStyleRegular, UnitPixel);

    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);

    auto drawBtn = [&](float cx, int state, wchar_t ch) {
        if (cx < 0) return;
        if (m_hoverState == state)
            g.FillEllipse(&bgBrush, cx - L.circleR, (float)cy - L.circleR,
                          L.circleR * 2, L.circleR * 2);
        SolidBrush& br = (m_hoverState == state) ? hoverBrush : iconBrush;
        RectF rect(cx - L.circleR, (float)cy - L.circleR, L.circleR * 2, L.circleR * 2);
        g.DrawString(&ch, 1, &iconFont, rect, &sf, &br);
    };

    drawBtn(L.prevX,     1, L'');
    drawBtn(L.playX,     2, isPlaying ? L'' : L'');
    drawBtn(L.nextX,     3, L'');
    drawBtn(L.settingsX, 4, L'');

    // 3. Title + Artist (stacked, scrolling)
    if (L.textMaxW > 0) {
        FontFamily ff1(L"Segoe UI Variable Display");
        FontFamily ff2(L"Segoe UI");
        const FontFamily* ff = ff1.IsAvailable() ? &ff1 : &ff2;
        Font titleFont(ff, (REAL)m_settings.fontSize, FontStyleBold, UnitPixel);
        Font artistFont(ff, max(8.0f, (REAL)m_settings.fontSize - 1.0f), FontStyleRegular, UnitPixel);

        RectF mRect(0, 0, 2000, 100), tBound, aBound;
        g.MeasureString(title.c_str(),  -1, &titleFont,  mRect, &tBound);
        g.MeasureString(artist.c_str(), -1, &artistFont, mRect, &aBound);

        float tH   = title.empty()  ? 0.0f : tBound.Height;
        float aH   = artist.empty() ? 0.0f : aBound.Height;
        float gap2 = (!title.empty() && !artist.empty()) ? 1.0f : 0.0f;
        float curY = ((float)h - tH - gap2 - aH) / 2.0f;

        Region clip(Rect(L.textX, 0, L.textMaxW, h));
        g.SetClip(&clip);
        SolidBrush textBrush(mainColor);

        if (!title.empty()) {
            m_textWidth = (int)tBound.Width;
            if (m_textWidth > L.textMaxW) {
                m_isScrolling = true;
                float dx = (float)(L.textX - m_scrollOffset);
                g.DrawString(title.c_str(), -1, &titleFont, PointF(dx, curY), &textBrush);
                if (dx + m_textWidth < L.textX + L.textMaxW)
                    g.DrawString(title.c_str(), -1, &titleFont, PointF(dx + m_textWidth + 40, curY), &textBrush);
            } else {
                m_isScrolling = false; m_scrollOffset = 0;
                g.DrawString(title.c_str(), -1, &titleFont, PointF((float)L.textX, curY), &textBrush);
            }
            curY += tH + gap2;
        } else { m_isScrolling = false; m_scrollOffset = 0; }

        if (!artist.empty()) {
            SolidBrush dimBrush(Color(180, mainColor.GetRed(), mainColor.GetGreen(), mainColor.GetBlue()));
            g.DrawString(artist.c_str(), -1, &artistFont, PointF((float)L.textX, curY), &dimBrush);
        }
        g.ResetClip();
    }
}

// ---------------------------------------------------------------------------
// Media thread
// ---------------------------------------------------------------------------
void MusicLoungeBand::StartMediaThread() {
    m_running = true;
    m_mediaThread = thread([this]() {
        winrt::init_apartment();
        while (m_running) {
            UpdateMediaInfo();
            for (int i = 0; i < 10 && m_running; i++) Sleep(100);
        }
        m_sessionMgr = nullptr;
        winrt::uninit_apartment();
    });
}

void MusicLoungeBand::StopMediaThread() {
    m_running = false;
    if (m_mediaThread.joinable()) m_mediaThread.join();
}

void MusicLoungeBand::UpdateMediaInfo() {
    try {
        if (!m_sessionMgr)
            m_sessionMgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!m_sessionMgr) return;

        GlobalSystemMediaTransportControlsSession session = nullptr;
        auto sessions = m_sessionMgr.GetSessions();
        uint32_t count = sessions.Size();
        for (uint32_t i = 0; i < count; i++) {
            auto s  = sessions.GetAt(i);
            auto pb = s.GetPlaybackInfo();
            if (pb && pb.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                session = s; break;
            }
        }
        if (!session) session = m_sessionMgr.GetCurrentSession();

        if (session) {
            auto props = session.TryGetMediaPropertiesAsync().get();
            auto info  = session.GetPlaybackInfo();
            lock_guard<mutex> lk(m_media.lock);
            wstring newTitle = props.Title().c_str();
            if (newTitle != m_media.title || !m_media.albumArt) {
                delete m_media.albumArt; m_media.albumArt = nullptr;
                auto thumb = props.Thumbnail();
                if (thumb) {
                    auto stream = thumb.OpenReadAsync().get();
                    m_media.albumArt = StreamToBitmap(stream);
                }
            }
            m_media.title     = newTitle;
            m_media.artist    = props.Artist().c_str();
            m_media.isPlaying = (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            m_media.hasMedia  = true;
        } else {
            lock_guard<mutex> lk(m_media.lock);
            m_media.hasMedia = false;
            m_media.title    = L"No media";
            m_media.artist   = L"";
            delete m_media.albumArt; m_media.albumArt = nullptr;
        }
    } catch (...) {
        lock_guard<mutex> lk(m_media.lock);
        m_media.hasMedia = false;
    }
    // Notify UI thread to repaint
    if (m_hWnd && IsWindow(m_hWnd))
        PostMessage(m_hWnd, WM_APP + 2, 0, 0);
}

void MusicLoungeBand::SendMediaCmd(int cmd) {
    try {
        if (!m_sessionMgr) return;
        auto session = m_sessionMgr.GetCurrentSession();
        if (!session) return;
        if (cmd == 1) session.TrySkipPreviousAsync();
        else if (cmd == 2) session.TryTogglePlayPauseAsync();
        else if (cmd == 3) session.TrySkipNextAsync();
    } catch (...) {}
}

// ---------------------------------------------------------------------------
// Band window proc (1px placeholder — handles timers and WM_APP messages)
// ---------------------------------------------------------------------------
LRESULT CALLBACK MusicLoungeBand::BandWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MusicLoungeBand* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<MusicLoungeBand*>(
            reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MusicLoungeBand*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleBandMessage(hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MusicLoungeBand::HandleBandMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1001) {
            // Reposition overlay if taskbar moved, update visibility
            PositionOverlay();

            bool shouldHide = false;
            bool playing = false;
            {
                lock_guard<mutex> lk(m_media.lock);
                if (m_settings.hideWhenNoMedia && !m_media.hasMedia) shouldHide = true;
                playing = m_media.isPlaying;
            }
            if (m_settings.idleTimeout > 0) {
                if (playing) { m_idleCounter = 0; m_hiddenByIdle = false; }
                else if (++m_idleCounter >= m_settings.idleTimeout) m_hiddenByIdle = true;
            }
            if (m_hiddenByIdle) shouldHide = true;
            if (m_hOverlay) ShowWindow(m_hOverlay, shouldHide ? SW_HIDE : SW_SHOW);

        } else if (wParam == 1002) {
            // Scroll frame
            if (m_isScrolling) {
                if (m_scrollWait > 0) { m_scrollWait--; }
                else {
                    if (++m_scrollOffset > m_textWidth + 40) { m_scrollOffset = 0; m_scrollWait = 60; }
                    RepaintOverlay();
                }
            }
        }
        return 0;

    case WM_SETTINGCHANGE:
        RepaintOverlay();
        return 0;

    case WM_APP + 1: {
        // Settings saved — reload and apply
        LoadBandSettings(m_settings);
        PositionOverlay();
        RepaintOverlay();
        return 0;
    }

    case WM_APP + 2:
        // Media state updated — repaint
        RepaintOverlay();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1001);
        KillTimer(hwnd, 1002);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Overlay window proc (visual window on Shell_TrayWnd — handles mouse input)
// ---------------------------------------------------------------------------
LRESULT CALLBACK MusicLoungeBand::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MusicLoungeBand* self = nullptr;
    if (msg == WM_CREATE) {
        self = reinterpret_cast<MusicLoungeBand*>(
            reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MusicLoungeBand*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleOverlayMessage(hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MusicLoungeBand::HandleOverlayMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        // UpdateLayeredWindow manages the surface; just acknowledge the paint message
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        LayoutInfo L = ComputeLayout(rc.right, rc.bottom);
        int ns = 0;
        if (y > 3 && y < rc.bottom - 3) {
            auto hit = [&](float cx) { return cx >= 0 && x >= cx - L.circleR && x <= cx + L.circleR; };
            if      (hit(L.prevX))     ns = 1;
            else if (hit(L.playX))     ns = 2;
            else if (hit(L.nextX))     ns = 3;
            else if (hit(L.settingsX)) ns = 4;
        }
        if (ns != m_hoverState) { m_hoverState = ns; RepaintOverlay(); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hoverState = 0; RepaintOverlay(); return 0;

    case WM_LBUTTONUP:
        if (m_hoverState >= 1 && m_hoverState <= 3) SendMediaCmd(m_hoverState);
        else if (m_hoverState == 4) ShowSettingsDialog(m_hWnd, m_settings);
        return 0;

    case WM_MOUSEWHEEL: {
        short d = GET_WHEEL_DELTA_WPARAM(wParam);
        keybd_event(d > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, 0, 0);
        keybd_event(d > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0, KEYEVENTF_KEYUP, 0);
        return 0;
    }

    case WM_DESTROY:
        m_hOverlay = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
