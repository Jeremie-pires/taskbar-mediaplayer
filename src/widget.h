#pragma once
// The player strip on the taskbar, just left of the notification area: artwork, title / artist and
// previous / play / next buttons. It is a layered child window of the taskbar (Shell_TrayWnd) owned by
// this process, so nothing is loaded into Explorer and no admin rights are needed.
#include <windows.h>

#include "media.h"
#include "settings.h"

namespace widget {

// Creates the strip on the primary taskbar. `owner` receives app::WM_SHOW_MENU and
// app::WM_SHOW_SETTINGS. Returns false when the taskbar is not there (yet).
bool Create(HINSTANCE inst, HWND owner);
void Destroy();
bool Alive();
// The values below are kept across Destroy / Create.
void Apply(const Settings& s);
void SetMedia(const media::Snapshot& m);
void SetLightTheme(bool light);
void SetHidden(bool hidden);
// Re-checks the taskbar layout and moves the strip if needed; call about once a second.
void Reposition();

}  // namespace widget
