#pragma once
#include <windows.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>

// WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

extern HINSTANCE g_hInst;
extern long      g_cDllRef;

// ---------------------------------------------------------------------------
// Settings (stored in HKCU\Software\MusicLounge)
// ---------------------------------------------------------------------------
struct BandSettings {
    int  width           = 320;
    bool showAlbumArt    = true;
    bool showTitle       = true;
    bool showArtist      = true;
    bool showPrev        = true;
    bool showPlay        = true;
    bool showNext        = true;
    bool hideFullscreen  = false;
    bool hideWhenNoMedia = false;
    int  fontSize        = 11;
    int  theme           = 0;  // 0=Taskbar 1=Clair 2=Sombre 3=Transparent
    int  idleTimeout     = 0;
};

void LoadBandSettings(BandSettings& s);
void SaveBandSettings(const BandSettings& s);
void ShowSettingsDialog(HWND hBandWnd, const BandSettings& s);
void CloseSettingsDialog();

// ---------------------------------------------------------------------------
// Media state (shared between band and background thread)
// ---------------------------------------------------------------------------
struct MediaState {
    std::wstring title  = L"No media";
    std::wstring artist = L"";
    bool isPlaying = false;
    bool hasMedia  = false;
    Gdiplus::Bitmap* albumArt = nullptr;
    std::mutex lock;
};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
struct LayoutInfo {
    int   artX, artY, artSize;
    bool  artVisible;
    float prevX, playX, nextX, settingsX;
    float circleR, iconW, iconH;
    int   textX, textMaxW;
};

// ---------------------------------------------------------------------------
// IDeskBand implementation
// ---------------------------------------------------------------------------
class MusicLoungeBand
    : public IDeskBand
    , public IObjectWithSite
    , public IPersistStream
{
public:
    MusicLoungeBand();
    ~MusicLoungeBand();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef()  override;
    ULONG   STDMETHODCALLTYPE Release() override;

    // IOleWindow
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* phwnd) override;
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // IDockingWindow
    HRESULT STDMETHODCALLTYPE ShowDW(BOOL fShow) override;
    HRESULT STDMETHODCALLTYPE CloseDW(DWORD) override;
    HRESULT STDMETHODCALLTYPE ResizeBorderDW(LPCRECT, IUnknown*, BOOL) override { return E_NOTIMPL; }

    // IDeskBand
    HRESULT STDMETHODCALLTYPE GetBandInfo(DWORD dwBandID, DWORD dwViewMode, DESKBANDINFO* pdbi) override;

    // IObjectWithSite
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pUnkSite) override;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) override;

    // IPersistStream
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* pClassID) override;
    HRESULT STDMETHODCALLTYPE IsDirty() override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Load(IStream*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Save(IStream*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* p) override { p->QuadPart = 0; return S_OK; }

private:
    static LRESULT CALLBACK BandWndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleBandMessage(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleOverlayMessage(HWND, UINT, WPARAM, LPARAM);

    void CreateBandWindow(HWND hParent);
    void CreateOverlayWindow();
    void PositionOverlay();
    void RepaintOverlay();
    void DrawContent(HDC hdc, int w, int h);
    LayoutInfo ComputeLayout(int w, int h) const;
    void StartMediaThread();
    void StopMediaThread();
    void UpdateMediaInfo();
    void SendMediaCmd(int cmd);

    long      m_ref      = 1;
    IUnknown* m_pSite    = nullptr;
    HWND      m_hWnd     = nullptr;   // 1px DeskBand placeholder (COM lifecycle)
    HWND      m_hOverlay = nullptr;   // Layered overlay on Shell_TrayWnd
    HWND      m_hParent  = nullptr;
    DWORD     m_dwBandID = 0;

    BandSettings m_settings;
    MediaState   m_media;

    std::thread  m_mediaThread;
    std::atomic<bool> m_running{ false };

    int  m_scrollOffset   = 0;
    int  m_textWidth      = 0;
    bool m_isScrolling    = false;
    int  m_scrollWait     = 60;
    int  m_hoverState     = 0;
    bool m_scroll1002Active = false;

    int  m_idleCounter    = 0;
    bool m_hiddenByIdle   = false;

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_sessionMgr{ nullptr };

    static const wchar_t* BAND_CLASS;
    static const wchar_t* OVERLAY_CLASS;
    static bool           s_bandClassRegistered;
    static bool           s_overlayClassRegistered;
};
