#include "MusicLoungeBand.h"
#include "resource.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <strsafe.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

static const wchar_t* REG_KEY = L"Software\\MusicLounge";

// ---------------------------------------------------------------------------
// Registry helpers
// ---------------------------------------------------------------------------
static DWORD ReadDword(const wchar_t* name, DWORD def) {
    DWORD v = def, size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, REG_KEY, name, RRF_RT_DWORD, nullptr, &v, &size);
    return v;
}
static void WriteDword(const wchar_t* name, DWORD v) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hk, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
}

void LoadBandSettings(BandSettings& s) {
    s.width           = (int)ReadDword(L"Width",           320);
    s.showAlbumArt    = ReadDword(L"ShowAlbumArt",    1) != 0;
    s.showTitle       = ReadDword(L"ShowTitle",       1) != 0;
    s.showArtist      = ReadDword(L"ShowArtist",      1) != 0;
    s.showPrev        = ReadDword(L"ShowPrev",        1) != 0;
    s.showPlay        = ReadDword(L"ShowPlay",        1) != 0;
    s.showNext        = ReadDword(L"ShowNext",        1) != 0;
    s.hideFullscreen  = ReadDword(L"HideFullscreen",  0) != 0;
    s.hideWhenNoMedia = ReadDword(L"HideWhenNoMedia", 0) != 0;
    s.fontSize        = (int)ReadDword(L"FontSize",   11);
    s.theme           = (int)ReadDword(L"Theme",       0);
    s.idleTimeout     = (int)ReadDword(L"IdleTimeout", 0);
    if (s.width    < 100)                 s.width    = 320;
    if (s.fontSize <   6)                 s.fontSize =  11;
    if (s.theme    < 0 || s.theme > 3)   s.theme    =   0;
}

void SaveBandSettings(const BandSettings& s) {
    WriteDword(L"Width",           (DWORD)s.width);
    WriteDword(L"ShowAlbumArt",    s.showAlbumArt    ? 1 : 0);
    WriteDword(L"ShowTitle",       s.showTitle       ? 1 : 0);
    WriteDword(L"ShowArtist",      s.showArtist      ? 1 : 0);
    WriteDword(L"ShowPrev",        s.showPrev        ? 1 : 0);
    WriteDword(L"ShowPlay",        s.showPlay        ? 1 : 0);
    WriteDword(L"ShowNext",        s.showNext        ? 1 : 0);
    WriteDword(L"HideFullscreen",  s.hideFullscreen  ? 1 : 0);
    WriteDword(L"HideWhenNoMedia", s.hideWhenNoMedia ? 1 : 0);
    WriteDword(L"FontSize",        (DWORD)s.fontSize);
    WriteDword(L"Theme",           (DWORD)s.theme);
    WriteDword(L"IdleTimeout",     (DWORD)s.idleTimeout);
}

// ---------------------------------------------------------------------------
// Settings dialog
// ---------------------------------------------------------------------------
static bool IsDarkMode() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &v, &sz);
    return v == 0;
}

struct DlgCtx {
    BandSettings settings;
    HWND   hBandWnd;
    bool   dark;
    HBRUSH hBg;
    HBRUSH hEdit;
    HWND   hComboTheme;
};

static HWND g_hSettingsDlg = nullptr;

static void ReadControls(HWND hwnd, DlgCtx* ctx) {
    BandSettings& s = ctx->settings;
    BOOL ok; int v;
    v = (int)GetDlgItemInt(hwnd, 101, &ok, FALSE); if (ok && v >= 100) s.width       = v;
    v = (int)GetDlgItemInt(hwnd, 102, &ok, FALSE); if (ok && v >=   6) s.fontSize    = v;
    v = (int)GetDlgItemInt(hwnd, 119, &ok, FALSE); if (ok)             s.idleTimeout = v;
    s.theme    = (int)SendMessageW(ctx->hComboTheme, CB_GETCURSEL, 0, 0);
    auto chk = [&](int id) { return SendDlgItemMessageW(hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    s.showAlbumArt   = chk(110);
    s.showTitle      = chk(111);
    s.showArtist     = chk(112);
    s.showPrev       = chk(113);
    s.showPlay       = chk(114);
    s.showNext       = chk(115);
    s.hideWhenNoMedia= chk(116);
    s.hideFullscreen = chk(117);
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgCtx* ctx = (DlgCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return 0;

    case WM_COMMAND:
        if (!ctx) break;
        if (LOWORD(wParam) == IDOK) {
            ReadControls(hwnd, ctx);
            SaveBandSettings(ctx->settings);
            PostMessageW(ctx->hBandWnd, WM_APP + 1, 0, 0);
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_hSettingsDlg = nullptr;
        if (ctx) {
            if (ctx->hBg)   DeleteObject(ctx->hBg);
            if (ctx->hEdit) DeleteObject(ctx->hEdit);
            delete ctx;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;

    case WM_ERASEBKGND:
        if (ctx && ctx->dark && ctx->hBg) {
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, ctx->hBg);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (ctx && ctx->dark) {
            SetTextColor((HDC)wParam, RGB(240, 240, 240));
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)ctx->hBg;
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (ctx && ctx->dark) {
            SetTextColor((HDC)wParam, RGB(240, 240, 240));
            SetBkColor((HDC)wParam, RGB(65, 65, 65));
            return (LRESULT)ctx->hEdit;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowSettingsDialog(HWND hBandWnd, const BandSettings& s) {
    if (g_hSettingsDlg && IsWindow(g_hSettingsDlg)) {
        SetForegroundWindow(g_hSettingsDlg);
        return;
    }

    static const wchar_t* CLS = L"MusicLoungeSettings";
    static bool s_classReg = false;
    if (!s_classReg) {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = SettingsWndProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = CLS;
        RegisterClassW(&wc);
        s_classReg = true;
    }

    bool dark = IsDarkMode();
    DlgCtx* ctx  = new DlgCtx{};
    ctx->settings = s;
    ctx->hBandWnd = hBandWnd;
    ctx->dark     = dark;
    ctx->hBg      = dark ? CreateSolidBrush(RGB(45, 45, 45)) : nullptr;
    ctx->hEdit    = dark ? CreateSolidBrush(RGB(65, 65, 65)) : nullptr;

    int dlgW = 310;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int dlgX = (scrW - dlgW) / 2, dlgY = scrH / 4;

    // WS_EX_TOPMOST: visible above taskbar; nullptr parent: no cross-thread ownership
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW | WS_EX_TOPMOST,
        CLS, L"Music Lounge — Paramètres",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, 100,
        nullptr, nullptr, g_hInst, ctx
    );
    if (!hDlg) {
        if (ctx->hBg)   DeleteObject(ctx->hBg);
        if (ctx->hEdit) DeleteObject(ctx->hEdit);
        delete ctx;
        return;
    }

    if (dark) {
        BOOL b = TRUE;
        DwmSetWindowAttribute(hDlg, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &b, sizeof(b));
    }

    // -----------------------------------------------------------------------
    // Build controls
    // -----------------------------------------------------------------------
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    auto addCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                       int x, int y, int w, int h, int id) -> HWND {
        HWND hw = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
            x, y, w, h, hDlg, (HMENU)(UINT_PTR)id, g_hInst, nullptr);
        SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
        return hw;
    };
    auto lbl = [&](const wchar_t* t, int y) {
        addCtrl(L"STATIC", t, 0, 12, y, 132, 18, 0);
    };
    auto edt = [&](int id, const wchar_t* v, int y) {
        addCtrl(L"EDIT", v, WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 148, y, 140, 20, id);
    };
    auto chk = [&](int id, const wchar_t* t, bool checked, int y) {
        HWND h = addCtrl(L"BUTTON", t, BS_AUTOCHECKBOX, 12, y, 274, 20, id);
        SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        if (dark) SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    };
    auto sep = [&](int y) {
        addCtrl(L"STATIC", nullptr, SS_ETCHEDHORZ, 12, y, 274, 2, 0);
    };
    auto mkCombo = [&](int id, int y) -> HWND {
        HWND h = addCtrl(L"COMBOBOX", nullptr,
            CBS_DROPDOWNLIST | WS_VSCROLL, 148, y, 140, 120, id);
        if (dark) SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        return h;
    };

    const BandSettings& ss = ctx->settings;
    int y = 14;
    wchar_t buf[32];

    lbl(L"Largeur (px) :", y);
    StringCchPrintfW(buf, 32, L"%d", ss.width);       edt(101, buf, y); y += 26;

    lbl(L"Taille police :", y);
    StringCchPrintfW(buf, 32, L"%d", ss.fontSize);    edt(102, buf, y); y += 26;

    lbl(L"Masquage auto (s) :", y);
    StringCchPrintfW(buf, 32, L"%d", ss.idleTimeout); edt(119, buf, y); y += 26;

    lbl(L"Thème :", y);
    HWND hCTheme = mkCombo(103, y);
    SendMessageW(hCTheme, CB_ADDSTRING, 0, (LPARAM)L"Taskbar");
    SendMessageW(hCTheme, CB_ADDSTRING, 0, (LPARAM)L"Clair");
    SendMessageW(hCTheme, CB_ADDSTRING, 0, (LPARAM)L"Sombre");
    SendMessageW(hCTheme, CB_ADDSTRING, 0, (LPARAM)L"Transparent");
    SendMessageW(hCTheme, CB_SETCURSEL, ss.theme, 0);
    ctx->hComboTheme = hCTheme;
    y += 28;

    sep(y); y += 10;
    chk(110, L"Pochette album",          ss.showAlbumArt,    y); y += 22;
    chk(111, L"Titre",                   ss.showTitle,       y); y += 22;
    chk(112, L"Artiste",                 ss.showArtist,      y); y += 22;
    sep(y); y += 10;
    chk(113, L"Bouton précédent", ss.showPrev,    y); y += 22;
    chk(114, L"Bouton lecture",           ss.showPlay,       y); y += 22;
    chk(115, L"Bouton suivant",           ss.showNext,       y); y += 22;
    sep(y); y += 10;
    chk(116, L"Masquer sans média",  ss.hideWhenNoMedia,y); y += 22;
    chk(117, L"Masquer en plein écran", ss.hideFullscreen, y); y += 28;

    HWND hOk  = addCtrl(L"BUTTON", L"OK",      BS_DEFPUSHBUTTON, 60,  y, 80, 28, IDOK);
    HWND hCan = addCtrl(L"BUTTON", L"Annuler", BS_PUSHBUTTON,    158, y, 80, 28, IDCANCEL);
    if (dark) {
        SetWindowTheme(hOk,  L"DarkMode_Explorer", nullptr);
        SetWindowTheme(hCan, L"DarkMode_Explorer", nullptr);
    }

    SetWindowPos(hDlg, nullptr, 0, 0, dlgW, y + 56, SWP_NOMOVE | SWP_NOZORDER);
    SetForegroundWindow(hDlg);
    g_hSettingsDlg = hDlg;
    // Returns immediately — modeless; settings applied via WM_APP+1
}

void CloseSettingsDialog() {
    if (g_hSettingsDlg && IsWindow(g_hSettingsDlg))
        DestroyWindow(g_hSettingsDlg);
}
