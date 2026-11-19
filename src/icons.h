#pragma once
// Vector icons: Phosphor "fill" weight (MIT, phosphoricons.com), kept as SVG path data and drawn with GDI+.
#include "gdiplus_inc.h"

namespace icons {

enum class Icon { Prev, Next, Play, Pause, Settings, Music };

// Fills `icon` with its 256-unit view box scaled to `size` pixels, centred on its own outline at `centre`.
void Fill(Gdiplus::Graphics& g, Icon icon, float size, Gdiplus::PointF centre, const Gdiplus::Brush& brush);

}  // namespace icons
