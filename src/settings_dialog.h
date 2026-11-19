#pragma once
// The settings window. Modeless; each change is saved at once and posts app::WM_SETTINGS_SAVED to `owner`.
#include <windows.h>

#include "settings.h"

namespace settings_dialog {

void Show(HINSTANCE inst, HWND owner, const Settings& current);
void Close();
// Call from the message loop: keyboard navigation (Tab, Enter, Esc). True when the message was used.
bool PreTranslate(MSG* msg);

}  // namespace settings_dialog
