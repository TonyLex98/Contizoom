#ifndef IC_GEOMETRY_H
#define IC_GEOMETRY_H

// Screen-space geometry shared by the renderer (to draw) and the input
// handler (to hit-test), so what you see is exactly what you can click.

#include "model/connection.h"
#include "view/ui_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace ic {

// A box's position on screen, in fractional console cells.
struct ScreenRect {
    double x = 0, y = 0, w = 0, h = 0;
    double centerX() const { return x + w / 2; }
    double centerY() const { return y + h / 2; }
    bool valid() const { return w > 0 && h > 0; }
};

// The region of the console the canvas is drawn in: everything except the
// sidebar on the left and the status rows at the top and bottom.
struct CanvasArea {
    // Sidebar width in columns, including its divider column. The user can
    // drag the divider; 1 means collapsed (just the divider is left).
    static constexpr int kSidebarWidth = 26;
    static constexpr int kSidebarCollapsed = 1;
    static constexpr int kMinCanvasWidth = 20;

    static int clampSidebar(int sidebar, int consoleWidth) {
        const int maxSidebar = consoleWidth - kMinCanvasWidth;
        if (maxSidebar < kSidebarCollapsed) return 0;  // console too narrow for any sidebar
        return sidebar < kSidebarCollapsed ? kSidebarCollapsed : sidebar > maxSidebar ? maxSidebar : sidebar;
    }

    int left = 0;
    int top = 0;
    int right = 0;   // inclusive
    int bottom = 0;  // inclusive

    double centerX() const { return (left + right) / 2.0; }
    double centerY() const { return (top + bottom) / 2.0; }
    int width() const { return right - left + 1; }
    int height() const { return bottom - top + 1; }
    bool contains(int x, int y) const { return x >= left && x <= right && y >= top && y <= bottom; }

    static CanvasArea forConsole(int width, int height, int sidebarWidth = kSidebarWidth) {
        const int sidebar = clampSidebar(sidebarWidth, width);
        return {sidebar, 1, width - 1, height - 2};
    }
};

// Integer cell box a ScreenRect is drawn into. Right/bottom edges come from
// floor(x + w), not floor(x) + floor(w), so a parent's drawn edge and the
// float bounds its children were clamped against always agree.
struct CellBox {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w - 1; }
    int bottom() const { return y + h - 1; }
};

inline CellBox cellBox(const ScreenRect& sr) {
    CellBox b;
    b.x = static_cast<int>(std::floor(sr.x));
    b.y = static_cast<int>(std::floor(sr.y));
    b.w = static_cast<int>(std::floor(sr.x + sr.w)) - b.x;
    b.h = static_cast<int>(std::floor(sr.y + sr.h)) - b.y;
    return b;
}

// Below this size the quarter/three-quarter anchors would round onto the
// corners or each other.
inline bool canShowAnchors(const CellBox& b) {
    return b.w >= 8 && b.h >= 6;
}

// Cell of an anchor point on the box border.
inline bool anchorCell(const CellBox& b, const Anchor& a, int& px, int& py) {
    if (!a.valid()) return false;
    const double frac = (a.index + 1) / 4.0;
    switch (a.edge) {
        case AnchorEdge::Left:
            px = b.x;
            py = b.y + static_cast<int>(std::round(frac * (b.h - 1)));
            return true;
        case AnchorEdge::Right:
            px = b.right();
            py = b.y + static_cast<int>(std::round(frac * (b.h - 1)));
            return true;
        case AnchorEdge::Bottom:
            px = b.x + static_cast<int>(std::round(frac * (b.w - 1)));
            py = b.bottom();
            return true;
        case AnchorEdge::None: break;
    }
    return false;
}

inline const std::array<Anchor, 9>& allAnchors() {
    static const std::array<Anchor, 9> anchors = {{
        {AnchorEdge::Left, 0},   {AnchorEdge::Left, 1},   {AnchorEdge::Left, 2},
        {AnchorEdge::Right, 0},  {AnchorEdge::Right, 1},  {AnchorEdge::Right, 2},
        {AnchorEdge::Bottom, 0}, {AnchorEdge::Bottom, 1}, {AnchorEdge::Bottom, 2},
    }};
    return anchors;
}

// Anchor within one cell of (x, y), if any.
inline Anchor anchorAt(const ScreenRect& sr, int x, int y) {
    if (!sr.valid()) return {};
    const CellBox b = cellBox(sr);
    if (!canShowAnchors(b)) return {};
    for (const Anchor& a : allAnchors()) {
        int px = 0, py = 0;
        if (anchorCell(b, a, px, py) && std::abs(x - px) <= 1 && std::abs(y - py) <= 1) return a;
    }
    return {};
}

// Resize corner within one cell of (x, y), if any. Boxes need some size for
// their corners to be distinguishable targets.
inline ResizeCorner cornerAt(const ScreenRect& sr, int x, int y) {
    if (!sr.valid()) return ResizeCorner::None;
    const CellBox b = cellBox(sr);
    if (b.w < 3 || b.h < 3) return ResizeCorner::None;
    const bool atLeft = std::abs(x - b.x) <= 1, atRight = std::abs(x - b.right()) <= 1;
    const bool atTop = std::abs(y - b.y) <= 1, atBottom = std::abs(y - b.bottom()) <= 1;
    if (atLeft && atTop) return ResizeCorner::TopLeft;
    if (atRight && atTop) return ResizeCorner::TopRight;
    if (atLeft && atBottom) return ResizeCorner::BottomLeft;
    if (atRight && atBottom) return ResizeCorner::BottomRight;
    return ResizeCorner::None;
}

using CellPath = std::vector<std::pair<int, int>>;

// Where a connection leaves a box and in which direction. An anchored end
// uses its anchor; otherwise the line leaves from the middle of the side
// facing the other box (cells are about twice as tall as wide, hence the
// factor 2), so it never cuts through the box's own text.
struct ConnectionEnd {
    int x = 0, y = 0;
    int dx = 0, dy = 0;  // unit exit direction
};

inline ConnectionEnd connectionEnd(const ScreenRect& sr, const Anchor& a, const ScreenRect& other) {
    const CellBox b = cellBox(sr);
    ConnectionEnd e;
    if (a.valid() && canShowAnchors(b) && anchorCell(b, a, e.x, e.y)) {
        e.dx = a.edge == AnchorEdge::Left ? -1 : a.edge == AnchorEdge::Right ? 1 : 0;
        e.dy = a.edge == AnchorEdge::Bottom ? 1 : 0;
        return e;
    }
    const double ddx = other.centerX() - sr.centerX();
    const double ddy = other.centerY() - sr.centerY();
    if (std::abs(ddx) >= 2.0 * std::abs(ddy)) {
        e.x = ddx >= 0 ? b.right() : b.x;
        e.y = b.y + b.h / 2;
        e.dx = ddx >= 0 ? 1 : -1;
    } else if (ddy > 0) {
        e.x = b.x + b.w / 2;
        e.y = b.bottom();
        e.dy = 1;
    } else {
        // Up through the top border, left of center to stay clear of the name.
        e.x = b.x + std::max(1, b.w / 4);
        e.y = b.y;
        e.dy = -1;
    }
    return e;
}

// Orthogonal route of a connection: a short stub out of each end in its
// exit direction, joined with one or two right-angle bends. A routing
// heuristic, not obstacle avoidance.
inline CellPath routeConnection(const ScreenRect& from, const ScreenRect& to, const Connection& conn) {
    const ConnectionEnd e1 = connectionEnd(from, conn.fromAnchor, to);
    const ConnectionEnd e2 = connectionEnd(to, conn.toAnchor, from);
    const int x1 = e1.x, y1 = e1.y, x2 = e2.x, y2 = e2.y;
    struct Dir {
        int dx, dy;
    };
    const Dir d1{e1.dx, e1.dy}, d2{e2.dx, e2.dy};

    constexpr int kStub = 2;
    const int ax = x1 + d1.dx * kStub, ay = y1 + d1.dy * kStub;
    const int bx = x2 + d2.dx * kStub, by = y2 + d2.dy * kStub;

    CellPath pts{{x1, y1}, {ax, ay}};
    const bool aHoriz = d1.dx != 0, bHoriz = d2.dx != 0;
    if (aHoriz && bHoriz) {
        const int midX = (ax + bx) / 2;
        pts.push_back({midX, ay});
        pts.push_back({midX, by});
    } else if (aHoriz) {
        pts.push_back({bx, ay});
    } else if (bHoriz) {
        pts.push_back({ax, by});
    } else {
        const int midY = (ay + by) / 2;
        pts.push_back({ax, midY});
        pts.push_back({bx, midY});
    }
    pts.push_back({bx, by});
    pts.push_back({x2, y2});
    return pts;
}

// Distance from a cell to the nearest point of a route.
inline double distanceToPath(const CellPath& path, double px, double py) {
    double best = 1e18;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const double x1 = path[i].first, y1 = path[i].second;
        const double dx = path[i + 1].first - x1, dy = path[i + 1].second - y1;
        const double lenSq = dx * dx + dy * dy;
        double t = lenSq > 0 ? ((px - x1) * dx + (py - y1) * dy) / lenSq : 0.0;
        t = std::fmax(0.0, std::fmin(1.0, t));
        const double cx = x1 + t * dx - px, cy = y1 + t * dy - py;
        best = std::fmin(best, std::sqrt(cx * cx + cy * cy));
    }
    return best;
}

}  // namespace ic

#endif  // IC_GEOMETRY_H
