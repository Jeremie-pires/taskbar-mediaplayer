#pragma once
// GDI+ headers use the min/max macros that NOMINMAX removes from windows.h.
#include <windows.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
