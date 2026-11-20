// Taskbar Media Player: a media player strip on the Windows taskbar.
// One hidden top-level window owns the app: it receives TaskbarCreated (Explorer restarted), the
// widget's clicks and a one-second tick that keeps the widget in place. There is no notification-area
// icon: the strip's menu, and the settings window (opened by starting the exe again while the strip
// is hidden), offer everything.
#include "app.h"
#include "gdiplus_inc.h"
#include "media.h"
#include "settings.h"
#include "settings_dialog.h"
#include "widget.h"

namespace {

constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 1000;
constexpr wchar_t kMutexName[] = L"Local\\TaskbarMediaPlayer";

enum MenuId : UINT { IdPlayPause = 1, IdPrevious, IdNext, IdSettings, IdAutostart, IdExit };

HINSTANCE g_inst;
HWND g_hwnd;
UINT g_taskbarCreatedMsg;
Settings g_settings;
bool g_light;
int g_idleSeconds;  // seconds without playback, counted up to the idle timeout

// ---- theme ------------------------------------------------------------------------------------

// Dark or light popup menus to match the taskbar. uxtheme exports these by ordinal only
// (SetPreferredAppMode = 135, FlushMenuThemes = 136); missing on old builds, then menus stay light.
void ApplyMenuTheme(bool light) {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setMode = ux ? reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135))) : nullptr;
    static auto flush = ux ? reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136))) : nullptr;
    if (setMode) setMode(light ? 3 : 2);  // ForceLight : ForceDark
    if (flush) flush();
}

// ---- app --------------------------------------------------------------------------------------

// True when the foreground window covers the whole monitor the taskbar is on. SHQueryUserNotificationState
// is not used: it reports QUNS_BUSY for windows that are not full screen.
bool FullscreenAppActive() {
    HWND fg = GetForegroundWindow();
    HWND bar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!fg || !bar) return false;
    wchar_t cls[64]{};
    GetClassNameW(fg, cls, ARRAYSIZE(cls));
    for (const wchar_t* shell : { L"Progman", L"WorkerW", L"Shell_TrayWnd" })
        if (!lstrcmpW(cls, shell)) return false;  // the desktop also spans the monitor
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL);
    MONITORINFO mi{ sizeof(mi) };
    RECT r{};
    if (!mon || mon != MonitorFromWindow(bar, MONITOR_DEFAULTTONULL) || !GetMonitorInfoW(mon, &mi) || !GetWindowRect(fg, &r))
        return false;
    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
           r.bottom >= mi.rcMonitor.bottom;
}

// Once a second: (re)attach to the taskbar, apply the hide rules, follow layout changes.
void Tick() {
    if (!widget::Alive() && !widget::Create(g_inst, g_hwnd)) return;  // the taskbar is not there yet
    const media::Snapshot m = media::Current();
    if (m.playing) g_idleSeconds = 0;
    else if (g_idleSeconds < g_settings.idleTimeout) g_idleSeconds++;
    const bool hide = (g_settings.hideWhenNoMedia && !m.hasMedia) ||
                      (g_settings.idleTimeout > 0 && g_idleSeconds >= g_settings.idleTimeout) ||
                      (g_settings.hideFullscreen && FullscreenAppActive());
    widget::SetHidden(hide);
}

void ShowSettings() { settings_dialog::Show(g_inst, g_hwnd, g_settings); }

void ShowMenu() {
    const media::Snapshot m = media::Current();
    HMENU menu = CreatePopupMenu();
    const UINT playback = m.hasMedia ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(menu, playback, IdPlayPause, m.playing ? L"Pause" : L"Lecture");
    AppendMenuW(menu, playback, IdPrevious, L"Précédent");
    AppendMenuW(menu, playback, IdNext, L"Suivant");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdSettings, L"Paramètres…");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : MF_UNCHECKED), IdAutostart, L"Démarrer avec Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdExit, L"Quitter");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);  // otherwise the menu does not close when clicking elsewhere
    const UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case IdPlayPause: media::Send(media::Command::TogglePlayPause); break;
        case IdPrevious: media::Send(media::Command::Previous); break;
        case IdNext: media::Send(media::Command::Next); break;
        case IdSettings: ShowSettings(); break;
        case IdAutostart: SetAutostart(!AutostartEnabled()); break;
        case IdExit: DestroyWindow(g_hwnd); break;
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {  // Explorer restarted: the widget is gone
        widget::Destroy();
        Tick();
        return 0;
    }
    switch (msg) {
        case app::WM_MEDIA_CHANGED:
            widget::SetMedia(media::Current());
            return 0;
        case app::WM_SETTINGS_SAVED:
            g_settings = LoadSettings();
            g_idleSeconds = 0;
            widget::Apply(g_settings);
            Tick();
            return 0;
        case app::WM_SHOW_MENU:
            ShowMenu();
            return 0;
        case app::WM_SHOW_SETTINGS:
            ShowSettings();
            return 0;
        case WM_TIMER:
            if (wp == kTickTimer) Tick();
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet")) {
                const bool light = app::LightTaskbar();
                if (light != g_light) {
                    g_light = light;
                    ApplyMenuTheme(light);
                    widget::SetLightTheme(light);
                }
            }
            return 0;
        case WM_ENDSESSION:  // sign-out or shutdown
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, kTickTimer);
            settings_dialog::Close();
            widget::Destroy();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// `taskbar-mediaplayer.exe --quit` closes a running instance and waits for it (for an installer).
int QuitRunningInstance() {
    HWND w = FindWindowW(app::kWindowClass, nullptr);
    if (!w) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE running = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    if (!running) return 1;
    PostMessageW(w, WM_CLOSE, 0, 0);
    const DWORD result = WaitForSingleObject(running, 10000);
    CloseHandle(running);
    return result == WAIT_OBJECT_0 ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--quit")) return QuitRunningInstance();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Started again while running: open the settings of the running instance.
        if (HWND running = FindWindowW(app::kWindowClass, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(running, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(running, app::WM_SHOW_SETTINGS, 0, 0);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    g_inst = inst;

    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    MigrateLegacySettings();
    MigrateLegacyAutostart();
    g_settings = LoadSettings();
    g_light = app::LightTaskbar();
    ApplyMenuTheme(g_light);
    widget::Apply(g_settings);
    widget::SetLightTheme(g_light);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = app::kWindowClass;
    RegisterClassExW(&wc);
    // A hidden top-level window, not message-only, so it receives the TaskbarCreated broadcast.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, app::kWindowClass, app::kName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                             nullptr);
    if (g_hwnd) {
        media::Start(g_hwnd, app::WM_MEDIA_CHANGED);
        Tick();
        SetTimer(g_hwnd, kTickTimer, kTickMs, nullptr);

        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (settings_dialog::PreTranslate(&m)) continue;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        media::Stop();
    }
    widget::SetMedia({});  // drops the last artwork before GDI+ shuts down
    Gdiplus::GdiplusShutdown(gdiplusToken);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
