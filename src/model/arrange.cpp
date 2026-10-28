#include "model/arrange.h"
#include "model/canvas.h"
#include "util/utf8.h"

#include <algorithm>
#include <cmath>

namespace ic {

namespace {

constexpr double kMargin = 3.0;     // inside a parent, left/right/bottom
constexpr double kGapX = 4.0;       // between siblings in a row
constexpr double kGapY = 2.0;       // between rows
constexpr double kRootGap = 10.0;   // between root boxes

struct Size {
    double w, h;
};

// Room a box needs above its children: header row plus its text lines.
double headerHeight(const Node& n) {
    int lines = n.text.empty() ? 0 : 1;
    for (const char c : n.text) lines += c == '\n';
    return 2.0 + lines * 1.2;
}

double textWidth(const Node& n) {
    double w = utf8::width(n.name) + 4.0;
    size_t start = 0;
    while (start <= n.text.size()) {
        size_t end = n.text.find('\n', start);
        if (end == std::string::npos) end = n.text.size();
        w = std::max(w, utf8::width(n.text.substr(start, end - start)) + 2.0);
        start = end + 1;
    }
    return std::min(w, 80.0);
}

// Packs boxes of the given sizes into rows no wider than `maxWidth`,
// starting at (x0, y0); returns the extent used.
Size packRows(const std::vector<std::shared_ptr<Node>>& boxes, double x0, double y0, double maxWidth, double gapX,
              double gapY) {
    double x = x0, y = y0, rowH = 0, right = x0;
    for (const auto& b : boxes) {
        if (x > x0 && x + b->rect.width > x0 + maxWidth) {
            x = x0;
            y += rowH + gapY;
            rowH = 0;
        }
        b->rect.x = x;
        b->rect.y = y;
        x += b->rect.width + gapX;
        rowH = std::max(rowH, b->rect.height);
        right = std::max(right, b->rect.right());
    }
    return {right - x0, y + rowH - y0};
}

// Row width that gives a roughly 3:2 block for boxes of this total area.
double targetRowWidth(const std::vector<std::shared_ptr<Node>>& boxes, double gap) {
    double area = 0, widest = 0;
    for (const auto& b : boxes) {
        area += (b->rect.width + gap) * (b->rect.height + gap);
        widest = std::max(widest, b->rect.width);
    }
    return std::max(widest, std::sqrt(area * 1.5) * 1.2);
}

Size arrange(Canvas& canvas, const std::shared_ptr<Node>& node) {
    const auto children = canvas.getChildren(node->uuid);
    if (children.empty()) {
        node->rect.width = std::max(20.0, textWidth(*node));
        node->rect.height = std::max(4.0, headerHeight(*node) + 1.0);
        return {node->rect.width, node->rect.height};
    }
    for (const auto& c : children) arrange(canvas, c);

    const double top = headerHeight(*node) + 1.0;
    const Size used = packRows(children, kMargin, top, targetRowWidth(children, kGapX), kGapX, kGapY);
    node->rect.width = std::max({20.0, textWidth(*node), used.w + 2 * kMargin});
    node->rect.height = top + used.h + kMargin;
    return {node->rect.width, node->rect.height};
}

}  // namespace

void arrangeTree(Canvas& canvas) {
    // getChildren() returns reading order, so sort children by their
    // current order first: imported nodes are created top to bottom with
    // increasing y, which arrange() then turns into rows.
    const auto roots = canvas.getChildren("");
    for (const auto& r : roots) arrange(canvas, r);
    packRows(roots, 0, 0, std::max(160.0, targetRowWidth(roots, kRootGap)), kRootGap, kRootGap);
    for (const auto& [uuid, _] : canvas.allNodes()) canvas.markChanged(uuid);
}

}  // namespace ic
