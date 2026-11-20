#include "settings_dialog.h"

#include "app.h"
#include "gdiplus_inc.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace Gdiplus;

constexpr wchar_t kClass[] = L"TaskbarMediaPlayerSettings";
constexpr DWORD kStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
constexpr DWORD kExStyle = WS_EX_DLGMODALFRAME;

// Layout, in pixels at 100% scaling: two columns of cards, in the style of Windows 11 Settings.
constexpr int kMargin = 20, kColW = 330, kGutter = 16, kHeaderH = 36, kRowH = 44, kRowGap = 4, kPadX = 14;
constexpr int kComboW = 150, kSegW = 76, kSegH = 28, kSwitchW = 40, kSwitchH = 20, kButtonW = 110, kButtonH = 32;
constexpr int kRadius = 4, kFontPx = 14;

enum : int {
    IdSizeCompact = 101,  // radio group, in SizeMode order
    IdSizeClassic,
    IdSizeLarge,
    IdTheme,
    IdFont,
    IdIdle,
    IdArt = 110,
    IdTitle,
    IdArtist,
    IdPrev,
    IdPlay,
    IdNext,
    IdSettingsButton,
    IdHideNoMedia,
    IdHideFullscreen,
    IdAutostart,  // not part of Settings: written to the Run key at once
    IdQuit = 130,
};

constexpr int kIdleChoices[] = { 0, 10, 30, 60, 300, 600, 1800, 3600 };  // seconds

#define STR2(x) L## #x
#define STR(x) STR2(x)
constexpr wchar_t kVersion[] = L"Version " STR(APP_VERSION_MAJOR) L"." STR(APP_VERSION_MINOR) L"." STR(APP_VERSION_PATCH);

struct Palette {
    COLORREF page, card, cardHot, control, controlHot, text, subtle, accent, onAccent;
};

// A card the window paints itself, with its label; the control on it (`id`) dims the label when disabled.
struct Card {
    RECT rect;
    std::wstring label;
    int id;
};

struct Ctx {
    Settings settings;
    HWND owner = nullptr;
    HINSTANCE inst = nullptr;
    bool dark = false;
    int dpi = 96;
    Palette pal{};
    HFONT font = nullptr, header = nullptr;
    HBRUSH page = nullptr, field = nullptr;
    std::vector<std::pair<RECT, std::wstring>> headers;
    std::vector<Card> cards;
    RECT version{};
};

HWND g_dlg;

int S(const Ctx& c, int v) { return MulDiv(v, c.dpi, 96); }

RECT Scaled(const Ctx& c, int x, int y, int w, int h) { return { S(c, x), S(c, y), S(c, x + w), S(c, y + h) }; }

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    auto ch = [&](BYTE x, BYTE y) { return (BYTE)std::lround((float)x + ((float)y - (float)x) * t); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

Color ToColor(COLORREF c, BYTE alpha = 255) { return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)); }

// The Windows accent colour (stored as 0xAABBGGRR, so its low three bytes are a COLORREF).
COLORREF AccentColor() {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &v,
                     &size) != ERROR_SUCCESS)
        return RGB(0, 120, 212);
    return v & 0xFFFFFF;
}

Palette MakePalette(bool dark) {
    const COLORREF accent = AccentColor();
    if (dark)
        return { RGB(32, 32, 32),    RGB(43, 43, 43),    RGB(50, 50, 50),       RGB(55, 55, 55), RGB(64, 64, 64),
                 RGB(255, 255, 255), RGB(197, 197, 197), Mix(accent, RGB(255, 255, 255), 0.35f), RGB(0, 0, 0) };
    return { RGB(243, 243, 243), RGB(251, 251, 251), RGB(246, 246, 246),  RGB(232, 232, 232), RGB(222, 222, 222),
             RGB(26, 26, 26),    RGB(96, 96, 96),    Mix(accent, RGB(0, 0, 0), 0.1f), RGB(255, 255, 255) };
}

int CALLBACK FoundFont(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool FontInstalled(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FoundFont, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

// The UI font at kFontPx: Segoe UI Variable on Windows 11, the system message font elsewhere.
HFONT MakeFont(const Ctx& c, int weight) {
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)c.dpi);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = -S(c, kFontPx);
    lf.lfWeight = weight;
    static const bool variable = FontInstalled(L"Segoe UI Variable Text");
    if (variable) wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Text");
    return CreateFontIndirectW(&lf);
}

// Rounded rectangle; `left` / `right` choose which side's corners are rounded.
void AddRoundRect(GraphicsPath& p, const RectF& r, float radius, bool left = true, bool right = true) {
    const float d = radius * 2;
    if (left) p.AddArc(r.X, r.Y, d, d, 180, 90);
    else p.AddLine(r.X, r.Y, r.X, r.Y);
    if (right) {
        p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
        p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    } else {
        p.AddLine(r.GetRight(), r.Y, r.GetRight(), r.GetBottom());
    }
    if (left) p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    else p.AddLine(r.X, r.GetBottom(), r.X, r.GetBottom());
    p.CloseFigure();
}

RectF ToRectF(const RECT& r) {
    return RectF((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}

void FillRound(Graphics& g, const RECT& r, float radius, COLORREF color, bool left = true, bool right = true) {
    GraphicsPath p;
    AddRoundRect(p, ToRectF(r), radius, left, right);
    SolidBrush b(ToColor(color));
    g.FillPath(&b, &p);
}

void DrawLabel(HDC dc, const Ctx& c, const std::wstring& text, RECT r, COLORREF color, UINT align, HFONT font = nullptr) {
    HGDIOBJ old = SelectObject(dc, font ? font : c.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

std::wstring WindowText(HWND h) {
    std::wstring s((size_t)GetWindowTextLengthW(h), L'\0');
    GetWindowTextW(h, s.data(), (int)s.size() + 1);
    return s;
}

bool FocusShown(HWND h) {
    return GetFocus() == h && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
}

void DrawFocus(Graphics& g, const Ctx& c, RECT r) {
    InflateRect(&r, -S(c, 1), -S(c, 1));
    GraphicsPath p;
    AddRoundRect(p, ToRectF(r), (float)S(c, kRadius));
    Pen pen(ToColor(c.pal.text), (REAL)S(c, 2));
    g.DrawPath(&pen, &p);
}

// ---- custom-drawn controls --------------------------------------------------------------------
// They stay real check boxes, radio buttons and push buttons, so the keyboard and screen readers
// handle them as usual; only their look is replaced.

void DrawToggle(HDC dc, const Ctx& c, HWND h, const RECT& r, bool hot) {
    const bool on = Button_GetCheck(h) == BST_CHECKED, enabled = IsWindowEnabled(h);
    const BYTE alpha = enabled ? 255 : 110;
    const int sw = S(c, kSwitchW), sh = S(c, kSwitchH);
    const RECT sr{ r.right - S(c, kPadX) - sw, (r.top + r.bottom - sh) / 2, r.right - S(c, kPadX), (r.top + r.bottom + sh) / 2 };
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        SolidBrush page(ToColor(c.pal.page));
        g.FillRectangle(&page, ToRectF(r));
        FillRound(g, r, (float)S(c, kRadius), hot && enabled ? c.pal.cardHot : c.pal.card);

        GraphicsPath track;
        AddRoundRect(track, ToRectF(sr), (float)sh / 2);
        const float knob = (float)S(c, 12), inset = (float)S(c, 4);
        const float kx = on ? (float)sr.right - inset - knob : (float)sr.left + inset;
        const RectF knobRect(kx, (float)(sr.top + sr.bottom) / 2 - knob / 2, knob, knob);
        if (on) {
            SolidBrush fill(ToColor(c.pal.accent, alpha)), dot(ToColor(c.pal.onAccent, alpha));
            g.FillPath(&fill, &track);
            g.FillEllipse(&dot, knobRect);
        } else {
            Pen outline(ToColor(c.pal.subtle, alpha), (REAL)S(c, 1));
            SolidBrush dot(ToColor(c.pal.subtle, alpha));
            g.DrawPath(&outline, &track);
            g.FillEllipse(&dot, knobRect);
        }
        if (FocusShown(h)) DrawFocus(g, c, r);
    }
    const COLORREF text = enabled ? c.pal.text : Mix(c.pal.card, c.pal.text, 0.45f);
    DrawLabel(dc, c, WindowText(h), { r.left + S(c, kPadX), r.top, sr.left - S(c, 90), r.bottom }, text, DT_LEFT);
    DrawLabel(dc, c, on ? L"Activé" : L"Désactivé", { sr.left - S(c, 86), r.top, sr.left - S(c, 10), r.bottom },
              enabled ? c.pal.subtle : text, DT_RIGHT);
}

void DrawSegment(HDC dc, const Ctx& c, HWND h, int index, const RECT& r, bool hot) {
    const bool on = Button_GetCheck(h) == BST_CHECKED;
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        SolidBrush card(ToColor(c.pal.card));
        g.FillRectangle(&card, ToRectF(r));
        const COLORREF fill = on ? c.pal.accent : hot ? c.pal.controlHot : c.pal.control;
        FillRound(g, r, (float)S(c, kRadius), fill, index == 0, index == 2);
        if (FocusShown(h)) DrawFocus(g, c, r);
    }
    DrawLabel(dc, c, WindowText(h), r, on ? c.pal.onAccent : c.pal.text, DT_CENTER);
}

void DrawPushButton(HDC dc, const Ctx& c, HWND h, const RECT& r, bool hot, bool down) {
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        SolidBrush page(ToColor(c.pal.page));
        g.FillRectangle(&page, ToRectF(r));
        FillRound(g, r, (float)S(c, kRadius), down ? c.pal.control : hot ? c.pal.controlHot : c.pal.control);
        if (FocusShown(h)) DrawFocus(g, c, r);
    }
    DrawLabel(dc, c, WindowText(h), r, c.pal.text, DT_CENTER);
}

LRESULT CustomDraw(const Ctx& c, const NMCUSTOMDRAW& cd) {
    if (cd.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    const int id = (int)cd.hdr.idFrom;
    const bool hot = (cd.uItemState & CDIS_HOT) != 0;
    if (id >= IdSizeCompact && id <= IdSizeLarge) DrawSegment(cd.hdc, c, cd.hdr.hwndFrom, id - IdSizeCompact, cd.rc, hot);
    else if (id >= IdArt && id <= IdAutostart) DrawToggle(cd.hdc, c, cd.hdr.hwndFrom, cd.rc, hot);
    else if (id == IDCANCEL || id == IdQuit) DrawPushButton(cd.hdc, c, cd.hdr.hwndFrom, cd.rc, hot, (cd.uItemState & CDIS_SELECTED) != 0);
    else return CDRF_DODEFAULT;
    return CDRF_SKIPDEFAULT;
}

// ---- values -----------------------------------------------------------------------------------

void AddItem(HWND box, const std::wstring& text, int value) {
    const LRESULT i = SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    SendMessageW(box, CB_SETITEMDATA, (WPARAM)i, (LPARAM)value);
}

void SelectValue(HWND box, int value) {
    const int n = (int)SendMessageW(box, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++)
        if ((int)SendMessageW(box, CB_GETITEMDATA, (WPARAM)i, 0) == value) SendMessageW(box, CB_SETCURSEL, (WPARAM)i, 0);
}

void ReadValue(HWND w, int id, int& out) {
    const LRESULT i = SendDlgItemMessageW(w, id, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR) out = (int)SendDlgItemMessageW(w, id, CB_GETITEMDATA, (WPARAM)i, 0);
}

std::wstring IdleLabel(int seconds) {
    if (seconds == 0) return L"Jamais";
    if (seconds % 3600 == 0) return std::to_wstring(seconds / 3600) + L" h";
    if (seconds % 60 == 0) return std::to_wstring(seconds / 60) + L" min";
    return std::to_wstring(seconds) + L" s";
}

void ReadControls(HWND w, Settings& s) {
    for (int id = IdSizeCompact; id <= IdSizeLarge; id++)
        if (IsDlgButtonChecked(w, id) == BST_CHECKED) s.size = (SizeMode)(id - IdSizeCompact);
    int theme = (int)s.theme;
    ReadValue(w, IdTheme, theme);
    s.theme = (Theme)std::clamp(theme, (int)Theme::Taskbar, (int)Theme::Transparent);
    ReadValue(w, IdFont, s.fontSize);
    ReadValue(w, IdIdle, s.idleTimeout);
    auto checked = [&](int id) { return IsDlgButtonChecked(w, id) == BST_CHECKED; };
    s.showAlbumArt = checked(IdArt);
    s.showTitle = checked(IdTitle);
    s.showArtist = checked(IdArtist);
    s.showPrev = checked(IdPrev);
    s.showPlay = checked(IdPlay);
    s.showNext = checked(IdNext);
    s.showSettings = checked(IdSettingsButton);
    s.hideWhenNoMedia = checked(IdHideNoMedia);
    s.hideFullscreen = checked(IdHideFullscreen);
}

// Compact mode shows no text: its settings are greyed out.
void UpdateEnabled(HWND w, const Ctx& c) {
    const bool text = c.settings.size != SizeMode::Compact;
    for (int id : { IdTitle, IdArtist, IdFont }) EnableWindow(GetDlgItem(w, id), text);
    InvalidateRect(w, nullptr, TRUE);
}

// Every change applies at once: saved, then the app reloads it.
void Changed(HWND w, Ctx& c) {
    ReadControls(w, c.settings);
    SaveSettings(c.settings);
    PostMessageW(c.owner, app::WM_SETTINGS_SAVED, 0, 0);
    UpdateEnabled(w, c);
}

// ---- layout -----------------------------------------------------------------------------------

// Creates the controls for c.dpi and returns the client height they need, in pixels at 100%.
int Build(HWND w, Ctx& c) {
    if (c.font) DeleteObject(c.font);
    if (c.header) DeleteObject(c.header);
    c.font = MakeFont(c, FW_NORMAL);
    c.header = MakeFont(c, FW_SEMIBOLD);
    c.headers.clear();
    c.cards.clear();

    auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, const RECT& r, int id) {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                 w, (HMENU)(INT_PTR)id, c.inst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)c.font, FALSE);
        if (c.dark && !lstrcmpW(cls, WC_COMBOBOXW)) SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        return h;
    };
    struct Column {
        int x, y;
    };
    auto header = [&](Column& col, const wchar_t* text) {
        c.headers.push_back({ Scaled(c, col.x + 2, col.y, kColW, kHeaderH - 8), text });
        col.y += kHeaderH;
    };
    auto row = [&](Column& col) {
        const int y = col.y;
        col.y += kRowH + kRowGap;
        return y;
    };
    auto toggle = [&](Column& col, int id, const wchar_t* label, bool on) {
        HWND b = add(WC_BUTTONW, label, WS_TABSTOP | BS_AUTOCHECKBOX, Scaled(c, col.x, row(col), kColW, kRowH), id);
        Button_SetCheck(b, on ? BST_CHECKED : BST_UNCHECKED);
    };
    auto combo = [&](Column& col, int id, const wchar_t* label) {
        const int y = row(col);
        c.cards.push_back({ Scaled(c, col.x, y, kColW, kRowH), label, id });
        HWND box = add(WC_COMBOBOXW, nullptr, WS_TABSTOP | WS_GROUP | WS_VSCROLL | CBS_DROPDOWNLIST,
                       Scaled(c, col.x + kColW - kPadX - kComboW, y, kComboW, 300), id);
        // A drop-down list's height comes from its font: centre it on the card once created.
        RECT br{};
        GetWindowRect(box, &br);
        SetWindowPos(box, nullptr, S(c, col.x + kColW - kPadX - kComboW), S(c, y) + (S(c, kRowH) - (br.bottom - br.top)) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return box;
    };
    const Settings& s = c.settings;

    Column left{ kMargin, kMargin - 8 }, right{ kMargin + kColW + kGutter, kMargin - 8 };
    header(left, L"Apparence");
    const int sizeY = row(left);
    c.cards.push_back({ Scaled(c, left.x, sizeY, kColW, kRowH), L"Taille", 0 });
    const wchar_t* sizes[] = { L"Compact", L"Classique", L"Large" };
    for (int i = 0; i < 3; i++) {
        const DWORD style = BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP | WS_TABSTOP : 0);
        HWND b = add(WC_BUTTONW, sizes[i], style,
                     Scaled(c, left.x + kColW - kPadX - (3 - i) * kSegW, sizeY + (kRowH - kSegH) / 2, kSegW, kSegH),
                     IdSizeCompact + i);
        Button_SetCheck(b, (int)s.size == i ? BST_CHECKED : BST_UNCHECKED);
    }
    HWND theme = combo(left, IdTheme, L"Thème");
    const wchar_t* themes[] = { L"Barre des tâches", L"Clair", L"Sombre", L"Transparent" };
    for (int i = 0; i < 4; i++) AddItem(theme, themes[i], i);
    SelectValue(theme, (int)s.theme);
    HWND font = combo(left, IdFont, L"Taille du texte");
    for (int px = Settings::kMinFont; px <= Settings::kMaxFont; px++) AddItem(font, std::to_wstring(px) + L" px", px);
    SelectValue(font, s.fontSize);

    header(left, L"Masquer le lecteur");
    HWND idle = combo(left, IdIdle, L"Après une pause de");
    bool listed = false;
    for (int v : kIdleChoices) listed |= v == s.idleTimeout;
    for (int v : kIdleChoices) {
        if (!listed && s.idleTimeout < v) {  // a value set by hand keeps its place in the list
            AddItem(idle, IdleLabel(s.idleTimeout), s.idleTimeout);
            listed = true;
        }
        AddItem(idle, IdleLabel(v), v);
    }
    SelectValue(idle, s.idleTimeout);
    toggle(left, IdHideNoMedia, L"Quand rien ne joue", s.hideWhenNoMedia);
    toggle(left, IdHideFullscreen, L"Pendant un plein écran", s.hideFullscreen);
    header(left, L"Démarrage");
    toggle(left, IdAutostart, L"Démarrer avec Windows", AutostartEnabled());

    header(right, L"Contenu");
    toggle(right, IdArt, L"Pochette d'album", s.showAlbumArt);
    toggle(right, IdTitle, L"Titre", s.showTitle);
    toggle(right, IdArtist, L"Artiste", s.showArtist);
    header(right, L"Boutons");
    toggle(right, IdPrev, L"Précédent", s.showPrev);
    toggle(right, IdPlay, L"Lecture / pause", s.showPlay);
    toggle(right, IdNext, L"Suivant", s.showNext);
    toggle(right, IdSettingsButton, L"Paramètres", s.showSettings);

    const int y = std::max(left.y, right.y) - kRowGap + 16;
    // Quitting lives here too: with the strip hidden, this window is the only way in.
    add(WC_BUTTONW, L"Quitter", WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON, Scaled(c, kMargin, y, kButtonW, kButtonH), IdQuit);
    add(WC_BUTTONW, L"Fermer", WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON,
        Scaled(c, kMargin + 2 * kColW + kGutter - kButtonW, y, kButtonW, kButtonH), IDCANCEL);
    // Shown for bug reports, between the two buttons.
    c.version = Scaled(c, kMargin + kButtonW, y, 2 * kColW + kGutter - 2 * kButtonW, kButtonH);
    UpdateEnabled(w, c);
    return y + kButtonH + kMargin;
}

int ClientWidth() { return 2 * kMargin + 2 * kColW + kGutter; }

SIZE WindowSize(const Ctx& c, int clientH) {
    RECT r{ 0, 0, S(c, ClientWidth()), S(c, clientH) };
    AdjustWindowRectExForDpi(&r, kStyle, FALSE, kExStyle, (UINT)c.dpi);
    return { r.right - r.left, r.bottom - r.top };
}

void Rebuild(HWND w, Ctx& c, const RECT& suggested) {
    ReadControls(w, c.settings);
    while (HWND child = GetWindow(w, GW_CHILD)) DestroyWindow(child);
    SIZE size = WindowSize(c, Build(w, c));
    SetWindowPos(w, nullptr, suggested.left, suggested.top, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Paint(HWND w, const Ctx& c) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        for (const Card& card : c.cards) FillRound(g, card.rect, (float)S(c, kRadius), c.pal.card);
    }
    for (const auto& [r, text] : c.headers) DrawLabel(dc, c, text, r, c.pal.text, DT_LEFT, c.header);
    for (const Card& card : c.cards) {
        const bool enabled = !card.id || IsWindowEnabled(GetDlgItem(w, card.id));
        const RECT r{ card.rect.left + S(c, kPadX), card.rect.top, card.rect.right - S(c, kPadX + kComboW + 8), card.rect.bottom };
        DrawLabel(dc, c, card.label, r, enabled ? c.pal.text : Mix(c.pal.card, c.pal.text, 0.45f), DT_LEFT);
    }
    DrawLabel(dc, c, kVersion, c.version, c.pal.subtle, DT_CENTER);
    EndPaint(w, &ps);
}

void Release(Ctx* c) {
    for (HGDIOBJ o : { (HGDIOBJ)c->font, (HGDIOBJ)c->header, (HGDIOBJ)c->page, (HGDIOBJ)c->field })
        if (o) DeleteObject(o);
    delete c;
}

LRESULT CALLBACK DialogProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            break;
        case WM_COMMAND: {
            if (!c) break;
            const int id = LOWORD(wp);
            if (id == IDOK || id == IDCANCEL) DestroyWindow(w);
            else if (id == IdQuit) PostMessageW(c->owner, WM_CLOSE, 0, 0);  // the app's exit closes this window
            else if (id == IdAutostart) SetAutostart(IsDlgButtonChecked(w, IdAutostart) == BST_CHECKED);
            else if ((HIWORD(wp) == BN_CLICKED && id != IdTheme && id != IdFont && id != IdIdle) || HIWORD(wp) == CBN_SELCHANGE)
                Changed(w, *c);
            return 0;
        }
        case WM_NOTIFY:
            if (c && reinterpret_cast<NMHDR*>(lp)->code == NM_CUSTOMDRAW)
                return CustomDraw(*c, *reinterpret_cast<NMCUSTOMDRAW*>(lp));
            break;
        case WM_CLOSE:
            DestroyWindow(w);
            return 0;
        case WM_DPICHANGED:
            if (c) {
                c->dpi = HIWORD(wp);
                Rebuild(w, *c, *reinterpret_cast<const RECT*>(lp));
            }
            return 0;
        case WM_ERASEBKGND:
            if (c) {
                RECT r;
                GetClientRect(w, &r);
                FillRect((HDC)wp, &r, c->page);
                return 1;
            }
            break;
        case WM_PAINT:
            if (c) {
                Paint(w, *c);
                return 0;
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            if (c && c->dark) {
                SetTextColor((HDC)wp, c->pal.text);
                SetBkColor((HDC)wp, c->pal.control);
                return (LRESULT)c->field;
            }
            break;
        case WM_DESTROY:
            g_dlg = nullptr;
            if (c) {
                Release(c);
                SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            }
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

namespace settings_dialog {

void Show(HINSTANCE inst, HWND owner, const Settings& current) {
    if (g_dlg) {
        SetForegroundWindow(g_dlg);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }

    auto* c = new Ctx;
    c->settings = current;
    c->owner = owner;
    c->inst = inst;
    c->dark = app::DarkApps();
    c->pal = MakePalette(c->dark);
    c->page = CreateSolidBrush(c->pal.page);
    c->field = CreateSolidBrush(c->pal.control);

    // Open on the monitor under the mouse, so it shows next to the taskbar that was clicked.
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    HWND w = CreateWindowExW(kExStyle, kClass, L"Taskbar Media Player — Paramètres", kStyle, mi.rcWork.left, mi.rcWork.top, 100,
                             100, nullptr, nullptr, inst, c);
    if (!w) {
        Release(c);
        return;
    }
    g_dlg = w;
    c->dpi = (int)GetDpiForWindow(w);
    BOOL dark = c->dark;
    DwmSetWindowAttribute(w, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // Windows 11: a title bar in the page colour; earlier versions ignore it.
    const COLORREF caption = c->pal.page;
    DwmSetWindowAttribute(w, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    SIZE size = WindowSize(*c, Build(w, *c));
    const int x = (mi.rcWork.left + mi.rcWork.right - size.cx) / 2;
    const int y = (mi.rcWork.top + mi.rcWork.bottom - size.cy) / 2;
    SetWindowPos(w, nullptr, x, y, size.cx, size.cy, SWP_NOZORDER);
    // Focus rectangles stay hidden until the keyboard is used, as in other Windows dialogs.
    SendMessageW(w, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    SetFocus(GetDlgItem(w, IdSizeCompact + (int)current.size));
}

void Close() {
    if (g_dlg) DestroyWindow(g_dlg);
}

bool PreTranslate(MSG* msg) { return g_dlg && IsDialogMessageW(g_dlg, msg); }

}  // namespace settings_dialog
