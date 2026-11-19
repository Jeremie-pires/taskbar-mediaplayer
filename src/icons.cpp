#include "icons.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>

namespace {

using namespace Gdiplus;

constexpr float kViewBox = 256;

// Phosphor Icons, "fill" weight, in the order of icons::Icon.
constexpr const char* kPaths[] = {
    // skip-back
    "M208 47.88v160.24a16 16 0 0 1-24.43 13.43L64 146.77V216a8 8 0 0 1-16 0V40a8 8 0 0 1 16 0v69.23l119.57-74.78"
    "A15.95 15.95 0 0 1 208 47.88",
    // skip-forward
    "M208 40v176a8 8 0 0 1-16 0v-69.23L72.43 221.55A15.95 15.95 0 0 1 48 208.12V47.88a15.95 15.95 0 0 1 24.43-13.43"
    "L192 109.23V40a8 8 0 0 1 16 0",
    // play
    "M240 128a15.74 15.74 0 0 1-7.6 13.51L88.32 229.65a16 16 0 0 1-16.2.3A15.86 15.86 0 0 1 64 216.13V39.87"
    "a15.86 15.86 0 0 1 8.12-13.82a16 16 0 0 1 16.2.3l144.08 88.14A15.74 15.74 0 0 1 240 128",
    // pause
    "M216 48v160a16 16 0 0 1-16 16h-40a16 16 0 0 1-16-16V48a16 16 0 0 1 16-16h40a16 16 0 0 1 16 16M96 32H56"
    "a16 16 0 0 0-16 16v160a16 16 0 0 0 16 16h40a16 16 0 0 0 16-16V48a16 16 0 0 0-16-16",
    // gear-six
    "M237.94 107.21a8 8 0 0 0-3.89-5.4l-29.83-17l-.12-33.62a8 8 0 0 0-2.83-6.08a111.9 111.9 0 0 0-36.72-20.67"
    "a8 8 0 0 0-6.46.59L128 41.85L97.88 25a8 8 0 0 0-6.47-.6a111.9 111.9 0 0 0-36.68 20.75a8 8 0 0 0-2.83 6.07"
    "l-.15 33.65l-29.83 17a8 8 0 0 0-3.89 5.4a106.5 106.5 0 0 0 0 41.56a8 8 0 0 0 3.89 5.4l29.83 17l.12 33.63"
    "a8 8 0 0 0 2.83 6.08a111.9 111.9 0 0 0 36.72 20.67a8 8 0 0 0 6.46-.59L128 214.15L158.12 231a7.9 7.9 0 0 0 3.9 1"
    "a8.1 8.1 0 0 0 2.57-.42a112.1 112.1 0 0 0 36.68-20.73a8 8 0 0 0 2.83-6.07l.15-33.65l29.83-17a8 8 0 0 0 3.89-5.4"
    "a106.5 106.5 0 0 0-.03-41.52M128 168a40 40 0 1 1 40-40a40 40 0 0 1-40 40",
    // music-note
    "m210.3 56.34l-80-24A8 8 0 0 0 120 40v108.26A48 48 0 1 0 136 184V98.75l69.7 20.91A8 8 0 0 0 216 112V64"
    "a8 8 0 0 0-5.7-7.66",
};

// Adds an SVG elliptical arc as cubic Béziers of at most 90 degrees each (SVG 1.1, appendix F.6.5).
void AddSvgArc(GraphicsPath& path, PointF from, double rx, double ry, double rotation, bool large, bool sweep, PointF to) {
    if (rx == 0 || ry == 0) {
        path.AddLine(from, to);
        return;
    }
    if (from.X == to.X && from.Y == to.Y) return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    const double phi = rotation * std::numbers::pi / 180, cs = std::cos(phi), sn = std::sin(phi);
    const double dx = (from.X - to.X) / 2.0, dy = (from.Y - to.Y) / 2.0;
    const double x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
    if (const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry); lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coef = std::sqrt(std::max(0.0, num / den));
    if (large == sweep) coef = -coef;
    const double cxp = coef * rx * y1 / ry, cyp = -coef * ry * x1 / rx;
    const double cx = cs * cxp - sn * cyp + (from.X + to.X) / 2.0;
    const double cy = sn * cxp + cs * cyp + (from.Y + to.Y) / 2.0;

    auto angle = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
    const double ux = (x1 - cxp) / rx, uy = (y1 - cyp) / ry;
    const double start = angle(1, 0, ux, uy);
    double delta = angle(ux, uy, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && delta > 0) delta -= 2 * std::numbers::pi;
    else if (sweep && delta < 0) delta += 2 * std::numbers::pi;

    const int segments = std::max(1, (int)std::ceil(std::abs(delta) / (std::numbers::pi / 2) - 1e-9));
    const double step = delta / segments, k = 4.0 / 3.0 * std::tan(step / 4);
    auto point = [&](double t) {
        return PointF((REAL)(cx + cs * rx * std::cos(t) - sn * ry * std::sin(t)),
                      (REAL)(cy + sn * rx * std::cos(t) + cs * ry * std::sin(t)));
    };
    auto tangent = [&](double t) {
        return PointF((REAL)(k * (-cs * rx * std::sin(t) - sn * ry * std::cos(t))),
                      (REAL)(k * (-sn * rx * std::sin(t) + cs * ry * std::cos(t))));
    };
    for (int i = 0; i < segments; ++i) {
        const double ta = start + i * step, tb = ta + step;
        const PointF a = i == 0 ? from : point(ta);
        const PointF b = i == segments - 1 ? to : point(tb);
        const PointF da = tangent(ta), db = tangent(tb);
        path.AddBezier(a, PointF(a.X + da.X, a.Y + da.Y), PointF(b.X - db.X, b.Y - db.Y), b);
    }
}

// Reads the subset of SVG path data the icons use: M L H V C A Z, absolute and relative.
class PathReader {
public:
    explicit PathReader(const char* d) : m_p(d) {}

    void Build(GraphicsPath& path) {
        PointF cur, start;
        char cmd = 0;
        for (;;) {
            SkipSeparators();
            if (!*m_p) return;
            if (std::isalpha((unsigned char)*m_p)) cmd = *m_p++;
            else if (!cmd) return;  // a number with no command before it: malformed data
            const bool rel = std::islower((unsigned char)cmd) != 0;
            const PointF base = rel ? cur : PointF(0, 0);
            switch (std::tolower((unsigned char)cmd)) {
                case 'm':
                    cur = start = Point(base);
                    path.StartFigure();
                    cmd = rel ? 'l' : 'L';  // further pairs after a move are lines
                    break;
                case 'l': {
                    const PointF p = Point(base);
                    path.AddLine(cur, p);
                    cur = p;
                    break;
                }
                case 'h': {
                    const PointF p(Number() + base.X, cur.Y);
                    path.AddLine(cur, p);
                    cur = p;
                    break;
                }
                case 'v': {
                    const PointF p(cur.X, Number() + base.Y);
                    path.AddLine(cur, p);
                    cur = p;
                    break;
                }
                case 'c': {
                    const PointF c1 = Point(base), c2 = Point(base), p = Point(base);
                    path.AddBezier(cur, c1, c2, p);
                    cur = p;
                    break;
                }
                case 'a': {
                    const float rx = Number(), ry = Number(), rotation = Number();
                    const bool large = Flag(), sweep = Flag();
                    const PointF p = Point(base);
                    AddSvgArc(path, cur, rx, ry, rotation, large, sweep, p);
                    cur = p;
                    break;
                }
                case 'z':
                    path.CloseFigure();
                    cur = start;
                    cmd = 0;
                    break;
                default: return;
            }
        }
    }

private:
    void SkipSeparators() {
        while (*m_p == ' ' || *m_p == ',' || *m_p == '\n' || *m_p == '\t') ++m_p;
    }
    float Number() {
        SkipSeparators();
        char* end;
        const float v = std::strtof(m_p, &end);
        // No number where one is due: malformed data, so skip to the end and draw what was read.
        m_p = end != m_p ? end : m_p + std::strlen(m_p);
        return v;
    }
    // Arc flags are single digits and may be packed against the next number ("a8 8 0 011 1").
    bool Flag() {
        SkipSeparators();
        if (!*m_p) return false;
        return *m_p++ == '1';
    }
    PointF Point(PointF base) {
        const float x = Number();
        return PointF(x + base.X, Number() + base.Y);
    }

    const char* m_p;
};

}  // namespace

namespace icons {

void Fill(Graphics& g, Icon icon, float size, PointF centre, const Brush& brush) {
    GraphicsPath path;  // alternate fill: inner figures, such as the gear's hole, stay empty
    PathReader(kPaths[(int)icon]).Build(path);
    Matrix scale;
    scale.Scale(size / kViewBox, size / kViewBox);
    path.Transform(&scale);
    path.Flatten(nullptr, 0.1f);  // exact bounds: unflattened bounds include Bézier control points
    RectF b;
    path.GetBounds(&b);
    Matrix move;
    move.Translate(std::round(centre.X - b.Width / 2) - b.X, std::round(centre.Y - b.Height / 2) - b.Y);
    path.Transform(&move);
    g.FillPath(&brush, &path);
}

}  // namespace icons
