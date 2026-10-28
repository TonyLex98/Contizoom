#include "view/console_renderer.h"
#include "model/canvas.h"
#include "model/node.h"
#include "model/connection.h"
#include "version/version.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <set>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

namespace ic {

// Full box: large enough for a bordered box with its name label.
static bool shouldDrawNodeBox(const ScreenRect& sr) {
    return sr.w >= 4.0 && sr.h >= 2.0;
}

// Compact box: too small for a name label, but still large enough for
// drawNodeAt to render real borders (matches its own w<2||h<2 dot cutoff).
// Nodes in this range must NOT be collapsed to a single-cell marker - they
// have real, distinct screen area and should keep it.
static bool canRenderAsBox(const ScreenRect& sr) {
    return sr.w >= 2.0 && sr.h >= 2.0;
}

// Integer right/bottom edge as floor(x+w), NOT floor(x)+floor(w). A box's
// own drawn width must be derived the same way its children's containment
// was checked against it (computeScreenRect clamps children against the
// float edge x+w) - otherwise the two disagree by up to a cell and a child
// that was correctly kept inside the parent in float space ends up drawn
// past the parent's actual last row/column once both are rounded down
// independently.
static int floorEdge(double origin, double extent) {
    return static_cast<int>(std::floor(origin + extent));
}

// Connection anchor points: 3 evenly spaced points each along the Left,
// Right and Bottom edges (Top is skipped - that row is reserved for the
// name label, an anchor there would sit under the text). Ordinal mirrors
// AnchorHover: 0=Left, 1=Right, 2=Bottom; index 0/1/2 = quarter/half/
// three-quarter along that edge.
static bool anchorPoint(int ox, int oy, int w, int h, int edge, int index, int& px, int& py) {
    double frac = (index + 1) / 4.0; // 0.25, 0.5, 0.75
    switch (edge) {
        case 0: px = ox;         py = oy + static_cast<int>(std::round(frac * (h - 1))); return true;
        case 1: px = ox + w - 1; py = oy + static_cast<int>(std::round(frac * (h - 1))); return true;
        case 2: px = ox + static_cast<int>(std::round(frac * (w - 1))); py = oy + h - 1;  return true;
        default: return false;
    }
}

// Below this, quarter/three-quarter points would round onto the corners or
// each other - not worth showing.
static bool canShowAnchors(int w, int h) {
    return w >= 8 && h >= 6;
}

ConsoleRenderer::ConsoleRenderer() {
    hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
}

void ConsoleRenderer::initialize(int w, int h) {
    viewWidth_ = w;
    viewHeight_ = h;
    buffer_.resize(w * h);
    canvasArea_ = CanvasArea::withSidebar(w, h);
    DWORD outMode = 0;
    GetConsoleMode(hOut_, &outMode);
    outMode &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut_, outMode);
}

void ConsoleRenderer::clear() {
    fillBuffer();
    flush();
}

void ConsoleRenderer::flush() {
    std::vector<CHAR_INFO> ci(viewWidth_ * viewHeight_);
    for (int i = 0; i < viewWidth_ * viewHeight_; i++) {
        ci[i].Char.UnicodeChar = buffer_[i].ch;
        ci[i].Attributes = static_cast<WORD>(buffer_[i].attr);
    }
    COORD size = { static_cast<SHORT>(viewWidth_), static_cast<SHORT>(viewHeight_) };
    COORD origin = { 0, 0 };
    SMALL_RECT region = { 0, 0, size.X - 1, size.Y - 1 };
    WriteConsoleOutputW(hOut_, ci.data(), size, origin, &region);
}

// ====================================================================
// Screen rect computation: roots via worldToScreen, children relative to parent
// ====================================================================
ScreenRect ConsoleRenderer::computeScreenRect(const Node* node, const Canvas& canvas,
                                                const Viewport& vp, int vw, int vh) {
    auto it = screenRects_.find(node->uuid);
    if (it != screenRects_.end()) return it->second;

    auto parent = canvas.getNode(node->parentId);
    if (parent) {
        // Children are positioned inside parent on screen
        ScreenRect parentSr = computeScreenRect(parent.get(), canvas, vp, vw, vh);
        double pw = parent->rect.width;
        double ph = parent->rect.height;
        double relX = pw > 0 ? node->rect.x / pw : 0.0;
        double relY = ph > 0 ? node->rect.y / ph : 0.0;
        double relW = pw > 0 ? node->rect.width / pw : 0.1;
        double relH = ph > 0 ? node->rect.height / ph : 0.2;

        ScreenRect sr;
        sr.x = parentSr.x + relX * parentSr.w;
        sr.y = parentSr.y + relY * parentSr.h;
        sr.w = relW * parentSr.w;
        sr.h = relH * parentSr.h;

        // Clamp strictly inside the parent's own rendered box (inset by its
        // 1-char border). The parent's box and a child's position each get
        // cast to integer character cells independently and with different
        // rounding (floor for box dimensions, round() for point markers) -
        // without this, a child positioned near the parent's edge can drift
        // past the parent's border once both are rounded, and the drift
        // compounds across nested levels of parent/child.
        double innerX0 = parentSr.x + 1.0;
        double innerY0 = parentSr.y + 1.0;
        double innerX1 = parentSr.x + parentSr.w - 1.0;
        double innerY1 = parentSr.y + parentSr.h - 1.0;
        if (innerX1 > innerX0 && innerY1 > innerY0) {
            sr.x = std::min(std::max(sr.x, innerX0), innerX1);
            sr.y = std::min(std::max(sr.y, innerY0), innerY1);
            sr.w = std::min(sr.w, innerX1 - sr.x);
            sr.h = std::min(sr.h, innerY1 - sr.y);
        }
        sr.w = std::max(0.0, sr.w);
        sr.h = std::max(0.0, sr.h);

        screenRects_[node->uuid] = sr;
        return sr;
    }

    // Root node: position via worldToScreen
    Rect world = canvas.computeWorldRect(node->uuid);
    int depthDiff = node->zoomLevel - vp.zoomLevel;
    double scale = vp.textScale;
    if (depthDiff < 0) {
        for (int i = 0; i < -depthDiff; i++) scale *= 2.0;
    } else {
        for (int i = 0; i < depthDiff; i++) scale *= 0.5;
    }

    double cx = canvasArea_.centerX();
    double cy = canvasArea_.centerY();
    ScreenRect sr;
    sr.x = (world.x - vp.centerX) * vp.textScale + cx;
    sr.y = (world.y - vp.centerY) * vp.textScale + cy;
    sr.w = world.width * scale;
    sr.h = world.height * scale;
    screenRects_[node->uuid] = sr;
    return sr;
}

// ====================================================================
// Full render
// ====================================================================
void ConsoleRenderer::render(const Canvas& canvas) {
    fillBuffer();
    screenRects_.clear();
    sidebarNodeRects_.clear();

    int vw = viewWidth_;
    int vh = viewHeight_;
    const auto& vp = canvas.viewport_;
    auto nodes = canvas.getVisibleNodes(vp, static_cast<double>(vw), static_cast<double>(vh));

    // Compute screen rects for all visible nodes (with caching)
    std::vector<const Node*> sorted;
    for (const auto& n : nodes) {
        computeScreenRect(n.get(), canvas, vp, vw, vh);
        sorted.push_back(n.get());
    }
    std::sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) {
        return a->zoomLevel < b->zoomLevel;
    });

    auto markerCell = [](const ScreenRect& sr) {
        return std::pair<int, int>{
            static_cast<int>(std::round(sr.centerX())),
            static_cast<int>(std::round(sr.centerY()))
        };
    };

    auto markerVisible = [&](const ScreenRect& sr) {
        auto [x, y] = markerCell(sr);
        return canvasArea_.contains(x, y);
    };

    // A node's children are only drawn individually if the node itself opened
    // up into a full labeled box AND actually has interior room for them -
    // a 4x2 box clears shouldDrawNodeBox but is just a header+footer with
    // zero content rows, so anything clamped "inside" it is squeezed to
    // nothing and drifts onto or past its border anyway. If a folder is
    // still collapsed down to a compact box, a dot, or a box with no real
    // interior, there is no room to also show its contents without every
    // level of the hierarchy overlapping into unreadable noise - the
    // folder's own (possibly tiny) marker stands in for its whole subtree
    // until you zoom in enough for it to become a full box itself. sorted is
    // ascending by zoomLevel, so parents are always visited before children.
    std::set<std::string> boxNodes;         // full box + name label
    std::set<std::string> compactNodes;     // small but real box, no name label
    std::set<std::string> canHostChildren;  // full box AND has real interior space
    for (const auto* node : sorted) {
        bool parentEligible = node->parentId.empty() || canHostChildren.count(node->parentId) > 0;
        if (!parentEligible) continue;

        auto it = screenRects_.find(node->uuid);
        if (it == screenRects_.end()) continue;
        const ScreenRect& sr = it->second;
        if (shouldDrawNodeBox(sr)) {
            boxNodes.insert(node->uuid);
            int ox = static_cast<int>(std::floor(sr.x));
            int oy = static_cast<int>(std::floor(sr.y));
            int ex = floorEdge(sr.x, sr.w);
            int ey = floorEdge(sr.y, sr.h);
            if (ex - ox >= 3 && ey - oy >= 3) {
                canHostChildren.insert(node->uuid);
            }
        } else if (canRenderAsBox(sr)) {
            compactNodes.insert(node->uuid);
        }
    }

    // Boxes and compact boxes occupy real, distinct screen area and are always
    // drawn as-is. Only genuine point-sized nodes (below canRenderAsBox) are
    // small enough that several of them can round to the same screen cell -
    // those compete for that single cell below.
    std::set<std::string> drawnNodes = boxNodes;
    drawnNodes.insert(compactNodes.begin(), compactNodes.end());

    std::map<std::pair<int, int>, int> markerCellCounts;
    std::map<std::pair<int, int>, std::string> markerCellOwner;
    for (auto rit = sorted.rbegin(); rit != sorted.rend(); ++rit) {
        const auto* node = *rit;
        if (drawnNodes.count(node->uuid)) continue;
        // Same rule as boxes/compact boxes: a node whose parent has no real
        // interior room doesn't get its own dot either - the collapsed
        // ancestor already represents it.
        bool parentEligible = node->parentId.empty() || canHostChildren.count(node->parentId) > 0;
        if (!parentEligible) continue;
        auto it = screenRects_.find(node->uuid);
        if (it == screenRects_.end() || !it->second.valid() || !markerVisible(it->second)) continue;

        auto cell = markerCell(it->second);
        int& count = markerCellCounts[cell];
        count++;
        if (count == 1) {
            markerCellOwner[cell] = node->uuid;
            drawnNodes.insert(node->uuid);
        }
    }
    // Cells where 2+ nodes collapsed onto the same point: render distinctly
    // (a cluster marker) instead of quietly showing one node's ordinary dot
    // as if nothing else were there.
    std::set<std::string> clusterNodes;
    for (const auto& [cell, count] : markerCellCounts) {
        if (count > 1) clusterNodes.insert(markerCellOwner[cell]);
    }
    for (const auto& uuid : drawnNodes) {
        if (boxNodes.count(uuid) || compactNodes.count(uuid)) continue;
        auto it = screenRects_.find(uuid);
        if (it == screenRects_.end()) continue;
        auto [x, y] = markerCell(it->second);

        // markerCell rounds to the nearest cell, but a box's own edges are
        // floored (floorEdge) - near an edge those two can disagree by a
        // cell, which would let a dot land on or past its own parent's
        // border. Clamp it back inside the parent's drawn interior.
        auto n = canvas.getNode(uuid);
        if (n && !n->parentId.empty()) {
            auto pit = screenRects_.find(n->parentId);
            if (pit != screenRects_.end()) {
                const ScreenRect& psr = pit->second;
                int px0 = static_cast<int>(std::floor(psr.x)) + 1;
                int py0 = static_cast<int>(std::floor(psr.y)) + 1;
                int px1 = floorEdge(psr.x, psr.w) - 2;
                int py1 = floorEdge(psr.y, psr.h) - 2;
                if (px1 >= px0) x = std::min(std::max(x, px0), px1);
                if (py1 >= py0) y = std::min(std::max(y, py0), py1);
            }
        }

        it->second = ScreenRect{
            static_cast<double>(x),
            static_cast<double>(y),
            1.0,
            1.0
        };
    }

    // Connections first
    for (const auto& [_, conn] : canvas.allConnections()) {
        if (!boxNodes.count(conn->fromUuid) || !boxNodes.count(conn->toUuid)) continue;
        auto fromIt = screenRects_.find(conn->fromUuid);
        auto toIt = screenRects_.find(conn->toUuid);
        if (fromIt != screenRects_.end() && toIt != screenRects_.end()) {
            drawConnectionAt(conn.get(), fromIt->second, toIt->second, vw, vh,
                              canvas.selectedConnection_ == conn->uuid);
        }
    }

    // Nodes
    for (const auto* node : sorted) {
        if (drawnNodes.count(node->uuid)) {
            auto it = screenRects_.find(node->uuid);
            int hoveredCorner = (canvas.cornerHover_.nodeUuid == node->uuid) ? canvas.cornerHover_.corner : 0;
            int hoveredAnchorEdge = -1, hoveredAnchorIndex = -1;
            if (canvas.anchorHover_.nodeUuid == node->uuid) {
                hoveredAnchorEdge = canvas.anchorHover_.edge;
                hoveredAnchorIndex = canvas.anchorHover_.index;
            }
            drawNodeAt(*node, it->second, vw, vh, clusterNodes.count(node->uuid) > 0, hoveredCorner,
                       hoveredAnchorEdge, hoveredAnchorIndex);
        }
    }

    // Selection
    if (canvas.selection_.active) drawSelection(canvas.selection_, vw, vh, vp);

    // Edit overlay
    if (canvas.editOverlay_.active) drawEditOverlay(canvas.editOverlay_, vw, vh, vp);
    if (canvas.connEditOverlay_.active) drawConnectionEditOverlay(canvas.connEditOverlay_, vw, vh);

    // HUD
    drawHUD(vp, vw, vh);

    // Sidebar
    drawSidebar(canvas);

    // FINAL pass: draw ALL names on absolute top of everything
    // Reverse order: parents (low zoomLevel) on top of children (high zoomLevel)
    for (auto rit = sorted.rbegin(); rit != sorted.rend(); ++rit) {
        const auto* node = *rit;
        if (node->name.empty()) continue;
        if (canvas.editOverlay_.active &&
            canvas.editOverlay_.editingName &&
            canvas.editOverlay_.nodeUuid == node->uuid) {
            continue;
        }
        auto sit = screenRects_.find(node->uuid);
        if (sit == screenRects_.end()) continue;
        const auto& sr = sit->second;
        if (!boxNodes.count(node->uuid) && !compactNodes.count(node->uuid)) continue;
        int ox = static_cast<int>(std::floor(sr.x));
        int oy = static_cast<int>(std::floor(sr.y));
        int w = floorEdge(sr.x, sr.w) - ox;
        int h = floorEdge(sr.y, sr.h) - oy;
        if (w < 2 || h < 2) continue;

        // Count display characters (UTF-8 aware)
        int displayChars = 0;
        std::vector<int> charBytes;
        {
            size_t i = 0;
            while (i < node->name.size()) {
                unsigned char b = static_cast<unsigned char>(node->name[i]);
                if (b < 0x80) { charBytes.push_back(static_cast<int>(i)); i += 1; }
                else if (b < 0xE0) { charBytes.push_back(static_cast<int>(i)); i += 2; }
                else { charBytes.push_back(static_cast<int>(i)); i += 3; }
                displayChars++;
            }
        }
        if (displayChars == 0) continue;

        int innerW = w - 2;
        int nameStartChar = (innerW - displayChars) / 2;
        if (nameStartChar < 0) nameStartChar = 0;
        int drawChars = std::min(innerW - nameStartChar, displayChars);
        uint16_t finalNameAttr = node->state == NodeState::Selected ? 0x0F : 0x0E;
        for (int i = 0; i < drawChars; i++) {
            int bytePos = charBytes[i];
            unsigned char b = static_cast<unsigned char>(node->name[bytePos]);
            wchar_t ch;
            if (b < 0x80) ch = b;
            else if (b < 0xE0 && bytePos + 1 < static_cast<int>(node->name.size())) {
                ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(node->name[bytePos + 1]) & 0x3F);
            }
            else if (b >= 0xE0 && bytePos + 2 < static_cast<int>(node->name.size())) {
                ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(node->name[bytePos + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(node->name[bytePos + 2]) & 0x3F);
            }
            else ch = b;
            putChar(ox + 1 + nameStartChar + i, oy, ch);
            putAttr(ox + 1 + nameStartChar + i, oy, finalNameAttr);
        }

        // Redraw corners to guarantee they are never overwritten by names
        if (w >= 2 && h >= 2) {
            if (node->state == NodeState::Selected) {
                int hoveredCorner = (canvas.cornerHover_.nodeUuid == node->uuid) ? canvas.cornerHover_.corner : 0;
                uint16_t tlAttr = hoveredCorner == 1 ? 0xF0 : 0x0F;
                uint16_t trAttr = hoveredCorner == 2 ? 0xF0 : 0x0F;
                putChar(ox, oy, '\\');           putAttr(ox, oy, tlAttr);
                putChar(ox + w - 1, oy, '/');     putAttr(ox + w - 1, oy, trAttr);
            } else {
                char hCorner = (node->state == NodeState::Hovered) ? '#' : '+';
                uint16_t hAttr = (node->state == NodeState::Hovered) ? 0x0A : 0x09;
                putChar(ox, oy, hCorner); putAttr(ox, oy, hAttr);
                putChar(ox + w - 1, oy, hCorner); putAttr(ox + w - 1, oy, hAttr);
            }
        }
    }

    for (auto it = screenRects_.begin(); it != screenRects_.end(); ) {
        if (!drawnNodes.count(it->first) &&
            !(canvas.editOverlay_.active && canvas.editOverlay_.nodeUuid == it->first)) {
            it = screenRects_.erase(it);
        } else {
            ++it;
        }
    }

    flush();
}

void ConsoleRenderer::renderNode(const Node&, int, int) {}
void ConsoleRenderer::renderConnection(const Connection&, const Node&, const Node&) {}
void ConsoleRenderer::renderSelection(const Selection&, const Viewport&, double, double) {}
void ConsoleRenderer::renderHUD(const Viewport& vp, double vw, double vh) {
    drawHUD(vp, static_cast<int>(vw), static_cast<int>(vh));
}
void ConsoleRenderer::renderFrame(const std::vector<std::shared_ptr<Node>>&, const std::string&) {}

// ====================================================================
// Drawing primitives
// ====================================================================
void ConsoleRenderer::fillBuffer() {
    buffer_.resize(viewWidth_ * viewHeight_);
    for (auto& c : buffer_) { c.ch = ' '; c.attr = 7; }
}

void ConsoleRenderer::putChar(int x, int y, wchar_t c) {
    if (x >= 0 && x < viewWidth_ && y >= 0 && y < viewHeight_)
        buffer_[y * viewWidth_ + x].ch = c;
}

void ConsoleRenderer::putAttr(int x, int y, uint16_t attr) {
    if (x >= 0 && x < viewWidth_ && y >= 0 && y < viewHeight_)
        buffer_[y * viewWidth_ + x].attr = attr;
}

void ConsoleRenderer::drawNodeAt(const Node& node, const ScreenRect& sr, int vw, int vh,
                                   bool isCluster, int hoveredCorner,
                                   int hoveredAnchorEdge, int hoveredAnchorIndex) {
    if (!sr.valid()) return;

    int ox = static_cast<int>(std::floor(sr.x));
    int oy = static_cast<int>(std::floor(sr.y));
    int w = floorEdge(sr.x, sr.w) - ox;
    int h = floorEdge(sr.y, sr.h) - oy;

    // Dot mode: too small for a box. Still a real, visible marker - a dim,
    // near-black glyph here made isolated small nodes indistinguishable from
    // empty canvas.
    if (w < 2 || h < 2) {
        char dot = isCluster ? '*' :
                   node.state == NodeState::Selected ? '#' :
                   node.state == NodeState::Hovered ? '~' : '+';
        uint16_t attr = node.childrenUuids.empty() ? 0x07 : 0x09;
        if (isCluster) attr = 0x0E;
        else if (node.state == NodeState::Selected) attr = 0x0F;
        else if (node.state == NodeState::Hovered) attr = 0x0A;
        int cx = static_cast<int>(sr.centerX());
        int cy = static_cast<int>(sr.centerY());
        putChar(cx, cy, dot);
        putAttr(cx, cy, attr);

        // Best-effort label: a single collapsed node with a name shouldn't
        // look like an empty patch of canvas just because it has no room for
        // a box. Print as much of the name as fits into the blank space
        // immediately to its right. Cluster markers skip this - the name of
        // one arbitrary member would be misleading for a merged cell.
        if (!isCluster && !node.name.empty()) {
            int px = cx + 1;
            size_t i = 0;
            int written = 0;
            while (i < node.name.size() && written < 24 && px < vw &&
                   canvasArea_.contains(px, cy) &&
                   buffer_[cy * viewWidth_ + px].ch == L' ') {
                unsigned char b = static_cast<unsigned char>(node.name[i]);
                wchar_t ch;
                if (b < 0x80) { ch = b; i += 1; }
                else if (b < 0xE0 && i + 1 < node.name.size()) {
                    ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(node.name[i + 1]) & 0x3F);
                    i += 2;
                } else if (i + 2 < node.name.size()) {
                    ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(node.name[i + 1]) & 0x3F) << 6) |
                         (static_cast<unsigned char>(node.name[i + 2]) & 0x3F);
                    i += 3;
                } else { ch = b; i += 1; }
                putChar(px, cy, ch);
                putAttr(px, cy, 0x07);
                px++;
                written++;
            }
        }
        return;
    }

    if (w < 3) w = 3;
    if (h < 2) h = 2;
    if (ox + w > vw) w = vw - ox;
    if (oy + h > vh) h = vh - oy;
    if (w < 1 || h < 1) return;

    char hLine = '-', vLine = '|', corner = '+';
    uint16_t borderAttr = 7, textAttr = 7, nameAttr = 0x0E;

    if (node.state == NodeState::Selected) {
        hLine = '='; vLine = '#'; corner = '#';
        borderAttr = 0x0F; textAttr = 0x0F; nameAttr = 0x0F;
    } else if (node.state == NodeState::Hovered) {
        hLine = '~'; vLine = ':'; corner = '.';
        borderAttr = 0x0A; textAttr = 0x0A; nameAttr = 0x0E;
    } else if (node.state == NodeState::InConnection) {
        hLine = '*'; vLine = '*'; corner = '*';
        borderAttr = 0x0C; textAttr = 0x0C; nameAttr = 0x0E;
    }

    // Bottom border
    int by = oy + h - 1;
    for (int x = ox; x < ox + w; x++) {
        putChar(x, by, (x == ox || x == ox + w - 1) ? corner : hLine);
        putAttr(x, by, borderAttr);
    }
    // Side borders
    for (int y = oy + 1; y < by; y++) {
        putChar(ox, y, vLine);
        putAttr(ox, y, borderAttr);
        if (ox + w - 1 > ox) {
            putChar(ox + w - 1, y, vLine);
            putAttr(ox + w - 1, y, borderAttr);
        }
    }

    bool hasHeader = (h >= 3 && w >= 4) || !node.name.empty();
    int textStartY = oy + 1;
    char hCorner = corner, hLineChar = hLine;
    uint16_t hAttr = borderAttr;
    if (node.state == NodeState::Selected) {
        hCorner = '#'; hLineChar = '='; hAttr = 0x0F;
    } else if (node.state == NodeState::Hovered) {
        hCorner = '#'; hLineChar = '='; hAttr = 0x0A;
    } else {
        hCorner = '+'; hLineChar = '='; hAttr = 0x09;
    }

    // --- Top border ---
    if (hasHeader && w >= 4) {
        putChar(ox, oy, hCorner); putAttr(ox, oy, hAttr);
        putChar(ox + w - 1, oy, hCorner); putAttr(ox + w - 1, oy, hAttr);
        for (int x = ox + 1; x < ox + w - 1; x++) {
            putChar(x, oy, hLineChar);
            putAttr(x, oy, hAttr);
        }
    } else {
        putChar(ox, oy, corner); putAttr(ox, oy, borderAttr);
        putChar(ox + w - 1, oy, corner); putAttr(ox + w - 1, oy, borderAttr);
        for (int x = ox + 1; x < ox + w - 1; x++) {
            putChar(x, oy, hLine);
            putAttr(x, oy, borderAttr);
        }
    }

    // Resize handles: a selected box's corners get their own diagonal marks
    // instead of the ordinary border corner, so they read as grabbable
    // rather than just "where the border turns." Whichever one the mouse is
    // actually over is drawn in reverse video so it's obvious which corner
    // will move if you click now.
    if (node.state == NodeState::Selected) {
        int rightX = ox + w - 1;
        int bottomY = oy + h - 1;
        uint16_t plainAttr = 0x0F;
        uint16_t hoverAttr = 0xF0;
        putChar(ox, oy, '\\');          putAttr(ox, oy, hoveredCorner == 1 ? hoverAttr : plainAttr);
        putChar(rightX, oy, '/');       putAttr(rightX, oy, hoveredCorner == 2 ? hoverAttr : plainAttr);
        putChar(ox, bottomY, '/');      putAttr(ox, bottomY, hoveredCorner == 3 ? hoverAttr : plainAttr);
        putChar(rightX, bottomY, '\\'); putAttr(rightX, bottomY, hoveredCorner == 4 ? hoverAttr : plainAttr);
    }

    // Connection anchor points: always shown (not just for selected nodes) -
    // dragging from one to another node creates a connection. Whichever one
    // is currently under the mouse lights up in reverse video, same as a
    // resize corner, so it's clear it's a live drop target.
    if (canShowAnchors(w, h)) {
        for (int edge = 0; edge < 3; edge++) {
            for (int index = 0; index < 3; index++) {
                int px, py;
                if (!anchorPoint(ox, oy, w, h, edge, index, px, py)) continue;
                bool hovered = (edge == hoveredAnchorEdge && index == hoveredAnchorIndex);
                putChar(px, py, 'o');
                putAttr(px, py, hovered ? 0xF0 : 0x0B);
            }
        }
    }

    // Body text — only if node is large enough (zoom check: text disappears first when zooming out)
    bool showText = (w >= 5 && h >= (hasHeader ? 4 : 3)) && !node.text.empty();
    if (showText) {
        std::istringstream iss(node.text);
        std::string line;
        int ty = textStartY;
        while (std::getline(iss, line) && ty < oy + h - 1) {
            int cx = 0;
            for (size_t i = 0; i < line.size() && cx < w - 2;) {
                unsigned char b = static_cast<unsigned char>(line[i]);
                wchar_t ch;
                if (b < 0x80) { ch = b; i++; }
                else if (b < 0xE0) { ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(line[i+1]) & 0x3F); i += 2; }
                else { ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(line[i+1]) & 0x3F) << 6) | (static_cast<unsigned char>(line[i+2]) & 0x3F); i += 3; }
                putChar(ox + 1 + cx, ty, ch);
                putAttr(ox + 1 + cx, ty, textAttr);
                cx++;
            }
            ty++;
        }
    }
    // Fallback: if node is too small for text but room exists, show first char of text
    if (!showText && w >= 3 && h >= 2 && !node.text.empty()) {
        unsigned char b = static_cast<unsigned char>(node.text[0]);
        wchar_t fc;
        if (b < 0x80) fc = b;
        else if (b < 0xE0 && node.text.size() >= 2) fc = ((b & 0x1F) << 6) | (static_cast<unsigned char>(node.text[1]) & 0x3F);
        else fc = '?';
        putChar(ox + 1, oy + 1, fc);
        putAttr(ox + 1, oy + 1, textAttr);
    }


}

namespace {
struct ExitDir { int dx, dy; };
}

// Which way a line leaves a given anchor edge (0=Left,1=Right,2=Bottom), or
// for a center-fallback endpoint (edge<0) a direction picked from where the
// other end sits - there's no Top anchor (that row is the name label), so
// the vertical case always exits downward.
static ExitDir exitDirFor(int edge, double fromX, double fromY, double towardX, double towardY) {
    if (edge == 0) return {-1, 0};
    if (edge == 1) return {1, 0};
    if (edge == 2) return {0, 1};
    double ddx = towardX - fromX, ddy = towardY - fromY;
    if (std::abs(ddx) > std::abs(ddy)) return {ddx >= 0 ? 1 : -1, 0};
    return {0, 1};
}

// Orthogonal (Manhattan) path between two points that leave in fixed
// directions: a short stub out of each anchor, then one or two bends to
// join them. This is a routing heuristic, not full obstacle avoidance - it
// won't detour around a node that happens to sit between the two stubs.
static std::vector<std::pair<int, int>> routeOrthogonal(int x1, int y1, ExitDir d1,
                                                          int x2, int y2, ExitDir d2) {
    const int stub = 2;
    std::vector<std::pair<int, int>> pts;
    pts.push_back({x1, y1});
    int ax = x1 + d1.dx * stub, ay = y1 + d1.dy * stub;
    pts.push_back({ax, ay});
    int bx = x2 + d2.dx * stub, by = y2 + d2.dy * stub;

    bool aHoriz = d1.dx != 0;
    bool bHoriz = d2.dx != 0;
    if (aHoriz && bHoriz) {
        int midX = (ax + bx) / 2;
        pts.push_back({midX, ay});
        pts.push_back({midX, by});
    } else if (aHoriz && !bHoriz) {
        pts.push_back({bx, ay});
    } else if (!aHoriz && bHoriz) {
        pts.push_back({ax, by});
    } else {
        int midY = (ay + by) / 2;
        pts.push_back({ax, midY});
        pts.push_back({bx, midY});
    }
    pts.push_back({bx, by});
    pts.push_back({x2, y2});
    return pts;
}

void ConsoleRenderer::drawConnectionAt(const Connection* conn, const ScreenRect& from,
                                        const ScreenRect& to, int vw, int vh, bool selected) {
    if (!from.valid() || !to.valid()) return;

    // If the connection was dragged from/to a specific anchor point, draw
    // it starting/ending exactly there instead of the node's center - a
    // fraction of the box, so it still lands on the right edge point after
    // the node moves, resizes, or the view zooms.
    auto endpointOf = [](const ScreenRect& sr, int edge, int index, int fallbackX, int fallbackY) {
        if (edge < 0 || index < 0) return std::pair<int, int>{fallbackX, fallbackY};
        int ox = static_cast<int>(std::floor(sr.x));
        int oy = static_cast<int>(std::floor(sr.y));
        int w = floorEdge(sr.x, sr.w) - ox;
        int h = floorEdge(sr.y, sr.h) - oy;
        if (!canShowAnchors(w, h)) return std::pair<int, int>{fallbackX, fallbackY};
        int px, py;
        if (anchorPoint(ox, oy, w, h, edge, index, px, py)) return std::pair<int, int>{px, py};
        return std::pair<int, int>{fallbackX, fallbackY};
    };

    auto [x1, y1] = endpointOf(from, conn->fromAnchorEdge, conn->fromAnchorIndex,
                                static_cast<int>(from.centerX()), static_cast<int>(from.centerY()));
    auto [x2, y2] = endpointOf(to, conn->toAnchorEdge, conn->toAnchorIndex,
                                static_cast<int>(to.centerX()), static_cast<int>(to.centerY()));

    char hCh = '-', vCh = '|';
    uint16_t connAttr = 0x08;
    if (conn->type == ConnType::DottedLine || conn->type == ConnType::DottedArrow) {
        hCh = '.'; vCh = ':'; connAttr = 0x05;
    }
    if (conn->type == ConnType::DashedLine || conn->type == ConnType::DashedArrow) {
        hCh = '-'; vCh = '|'; connAttr = 0x06;
    }
    bool isArrow = conn->type == ConnType::Arrow || conn->type == ConnType::DottedArrow ||
                   conn->type == ConnType::DashedArrow;
    if (isArrow) connAttr = 0x09;
    if (selected) connAttr = 0x0F;

    ExitDir d1 = exitDirFor(conn->fromAnchorEdge, x1, y1, x2, y2);
    ExitDir d2 = exitDirFor(conn->toAnchorEdge, x2, y2, x1, y1);
    auto pts = routeOrthogonal(x1, y1, d1, x2, y2, d2);

    for (size_t i = 0; i + 1 < pts.size(); i++) {
        auto [sx, sy] = pts[i];
        auto [ex, ey] = pts[i + 1];
        if (sx == ex) {
            int y0 = std::min(sy, ey), y1b = std::max(sy, ey);
            for (int y = y0; y <= y1b; y++) { putChar(sx, y, vCh); putAttr(sx, y, connAttr); }
        } else if (sy == ey) {
            int x0 = std::min(sx, ex), x1b = std::max(sx, ex);
            for (int x = x0; x <= x1b; x++) { putChar(x, sy, hCh); putAttr(x, sy, connAttr); }
        }
    }
    // Mark the bends so the route reads as deliberate right angles rather
    // than a gap in the line.
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        putChar(pts[i].first, pts[i].second, '+');
        putAttr(pts[i].first, pts[i].second, connAttr);
    }

    if (isArrow) {
        // Points into the target in the direction the final stub actually
        // approaches from, not a fixed '>' - the route can arrive from
        // any side depending on which anchors were used.
        char arrowCh = d2.dx > 0 ? '<' : d2.dx < 0 ? '>' : '^';
        putChar(x2, y2, arrowCh);
        putAttr(x2, y2, connAttr);
    }

    if (!conn->name.empty()) {
        auto [lx, ly] = pts[pts.size() / 2];
        int px = lx + 1;
        size_t i = 0;
        while (i < conn->name.size() && px < vw && ly >= 0 && ly < vh) {
            unsigned char b = static_cast<unsigned char>(conn->name[i]);
            wchar_t ch;
            if (b < 0x80) { ch = b; i += 1; }
            else if (b < 0xE0 && i + 1 < conn->name.size()) {
                ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(conn->name[i + 1]) & 0x3F);
                i += 2;
            } else if (i + 2 < conn->name.size()) {
                ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(conn->name[i + 1]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(conn->name[i + 2]) & 0x3F);
                i += 3;
            } else { ch = b; i += 1; }
            putChar(px, ly, ch);
            putAttr(px, ly, selected ? 0x0F : 0x0E);
            px++;
        }
    }
}

void ConsoleRenderer::drawSelection(const Selection& sel, int vw, int vh, const Viewport& vp) {
    double cx = canvasArea_.centerX();
    double cy = canvasArea_.centerY();
    int x1 = static_cast<int>((sel.left() - vp.centerX) * vp.textScale + cx);
    int y1 = static_cast<int>((sel.top() - vp.centerY) * vp.textScale + cy);
    int x2 = static_cast<int>((sel.right() - vp.centerX) * vp.textScale + cx);
    int y2 = static_cast<int>((sel.bottom() - vp.centerY) * vp.textScale + cy);

    uint16_t selAttr = 0x0F;
    for (int x = std::min(x1, x2); x <= std::max(x1, x2); x++) {
        putChar(x, y1, (x == x1 || x == x2) ? '#' : '-');
        putAttr(x, y1, selAttr);
        putChar(x, y2, (x == x1 || x == x2) ? '#' : '-');
        putAttr(x, y2, selAttr);
    }
    for (int y = std::min(y1, y2); y <= std::max(y1, y2); y++) {
        putChar(x1, y, (y == y1 || y == y2) ? '#' : '|');
        putAttr(x1, y, selAttr);
        putChar(x2, y, (y == y1 || y == y2) ? '#' : '|');
        putAttr(x2, y, selAttr);
    }
}

void ConsoleRenderer::drawEditOverlay(const EditOverlay& overlay, int vw, int vh, const Viewport&) {
    auto it = screenRects_.find(overlay.nodeUuid);
    if (it == screenRects_.end()) return;

    const ScreenRect& sr = it->second;
    if (!sr.valid()) return;

    int ox = static_cast<int>(sr.x);
    int oy = static_cast<int>(sr.y);
    int w = static_cast<int>(sr.w);
    int h = static_cast<int>(sr.h);
    if (overlay.nodeRect.width > 0) {
        w = std::max(w, static_cast<int>(std::round(overlay.nodeRect.width)));
    }
    if (overlay.nodeRect.height > 0) {
        h = std::max(h, static_cast<int>(std::round(overlay.nodeRect.height)));
    }
    if (w < 3) w = 3;
    if (h < 2) h = 2;
    screenRects_[overlay.nodeUuid] = ScreenRect{
        static_cast<double>(ox),
        static_cast<double>(oy),
        static_cast<double>(w),
        static_cast<double>(h)
    };

    uint16_t editAttr = 0x0F;

    if (overlay.editingName) {
        for (int y = oy; y < oy + h; y++) {
            for (int x = ox; x < ox + w; x++) {
                if (x >= 0 && x < vw && y >= 0 && y < vh) {
                    putChar(x, y, ' ');
                    putAttr(x, y, editAttr);
                }
            }
        }

        for (int x = ox; x < ox + w; x++) {
            if (x >= 0 && x < vw) {
                if (oy >= 0 && oy < vh) {
                    putChar(x, oy, x == ox || x == ox + w - 1 ? '+' : '=');
                    putAttr(x, oy, editAttr);
                }
                int by = oy + h - 1;
                if (by >= 0 && by < vh && by != oy) {
                    putChar(x, by, x == ox || x == ox + w - 1 ? '+' : '~');
                    putAttr(x, by, editAttr);
                }
            }
        }
        for (int y = oy + 1; y < oy + h - 1; y++) {
            if (y >= 0 && y < vh) {
                if (ox >= 0 && ox < vw) {
                    putChar(ox, y, ':');
                    putAttr(ox, y, editAttr);
                }
                int rx = ox + w - 1;
                if (rx >= 0 && rx < vw && rx != ox) {
                    putChar(rx, y, ':');
                    putAttr(rx, y, editAttr);
                }
            }
        }

        int displayChars = 0;
        std::vector<int> charBytes;
        for (size_t i = 0; i < overlay.text.size();) {
            unsigned char b = static_cast<unsigned char>(overlay.text[i]);
            charBytes.push_back(static_cast<int>(i));
            if (b < 0x80) i += 1;
            else if (b < 0xE0) i += 2;
            else i += 3;
            displayChars++;
        }

        int innerW = std::max(1, w - 2);
        int nameStartChar = (innerW - displayChars) / 2;
        if (nameStartChar < 0) nameStartChar = 0;
        int drawChars = std::min(innerW - nameStartChar, displayChars);

        for (int cx = 0; cx < drawChars; cx++) {
            int bytePos = charBytes[cx];
            unsigned char b = static_cast<unsigned char>(overlay.text[bytePos]);
            wchar_t ch;
            if (b < 0x80) ch = b;
            else if (b < 0xE0 && bytePos + 1 < static_cast<int>(overlay.text.size())) {
                ch = ((b & 0x1F) << 6) |
                     (static_cast<unsigned char>(overlay.text[bytePos + 1]) & 0x3F);
            } else if (bytePos + 2 < static_cast<int>(overlay.text.size())) {
                ch = ((b & 0x0F) << 12) |
                     ((static_cast<unsigned char>(overlay.text[bytePos + 1]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(overlay.text[bytePos + 2]) & 0x3F);
            } else {
                ch = '?';
            }

            int px = ox + 1 + nameStartChar + cx;
            if (px >= 0 && px < vw && oy >= 0 && oy < vh) {
                putChar(px, oy, ch);
                putAttr(px, oy, editAttr);
            }
        }

        int cursorPx = ox + 1 + nameStartChar + overlay.cursorCol;
        cursorPx = std::max(ox + 1, std::min(ox + w - 2, cursorPx));
        if (cursorPx >= 0 && cursorPx < vw && oy >= 0 && oy < vh) {
            if (buffer_[oy * viewWidth_ + cursorPx].ch != '=' &&
                buffer_[oy * viewWidth_ + cursorPx].ch != ' ') {
                buffer_[oy * viewWidth_ + cursorPx].attr = 0xF0;
            } else {
                putChar(cursorPx, oy, '_');
                putAttr(cursorPx, oy, 0xF0);
            }
        }
        return;
    }

    // ---- Render text FIRST, track cursor position ----
    std::istringstream iss(overlay.text);
    std::string line;
    int lineRow = 0;
    int ty = oy + 1;
    int cursorPx = -1, cursorPy = -1;

    while (std::getline(iss, line)) {
        int cx = 0;
        for (size_t i = 0; i < line.size() && cx < w - 2;) {
            unsigned char b = static_cast<unsigned char>(line[i]);
            wchar_t ch;
            if (b < 0x80) { ch = b; i++; }
            else if (b < 0xE0) { ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(line[i+1]) & 0x3F); i += 2; }
            else { ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(line[i+1]) & 0x3F) << 6) | (static_cast<unsigned char>(line[i+2]) & 0x3F); i += 3; }
            int px = ox + 1 + cx;
            if (px >= 0 && px < vw && ty >= 0 && ty < vh) {
                putChar(px, ty, ch); putAttr(px, ty, editAttr);
            }
            cx++;
        }
        // Track cursor: even if this row is outside the visible box area
        if (lineRow == overlay.cursorRow) {
            cursorPx = ox + 1 + overlay.cursorCol;
            cursorPy = ty;
        }
        ty++;
        lineRow++;
    }

    // If cursor row is past all text lines, place it on the next available row
    if (cursorPx < 0) {
        cursorPx = ox + 1 + overlay.cursorCol;
        cursorPy = oy + 1 + overlay.cursorRow;
    }

    // ---- NOW draw the border, expanding to include cursor if needed ----
    int drawH = h;
    if (cursorPy >= 0 && cursorPy >= oy + drawH) {
        drawH = cursorPy - oy + 2;
    }

    // Clamp to viewport
    if (ox + w > vw) w = vw - ox;
    if (oy + drawH > vh) drawH = vh - oy;
    if (w < 1 || drawH < 1) return;

    // Border
    for (int x = ox; x < ox + w; x++) {
        if (oy >= 0 && oy < vh) { putChar(x, oy, '+'); putAttr(x, oy, editAttr); }
        int by = oy + drawH - 1;
        if (by >= 0 && by < vh && by != oy) { putChar(x, by, '+'); putAttr(x, by, editAttr); }
    }
    for (int y = oy; y < oy + drawH; y++) {
        if (y >= 0 && y < vh) {
            putChar(ox, y, '#'); putAttr(ox, y, editAttr);
            if (ox + w - 1 != ox) { putChar(ox + w - 1, y, '#'); putAttr(ox + w - 1, y, editAttr); }
        }
    }

    // ---- Draw cursor (ALWAYS visible) ----
    if (cursorPx >= 0 && cursorPy >= 0) {
        int px = cursorPx;
        int py = cursorPy;
        // Clamp to viewport
        px = std::max(0, std::min(px, vw - 1));
        py = std::max(0, std::min(py, vh - 1));

        // Check if there's a character at this position
        bool hasChar = false;
        for (int y = oy; y < oy + drawH; y++) {
            for (int x = ox; x < ox + w; x++) {
                if (x == px && y == py) {
                    hasChar = (buffer_[y * viewWidth_ + x].ch != ' ');
                    break;
                }
            }
            if (hasChar) break;
        }

        if (hasChar) {
            buffer_[py * viewWidth_ + px].attr = 0xF0; // reverse video
        } else {
            putChar(px, py, '_'); putAttr(px, py, 0xF0); // blinking underscore
        }
    }
}

void ConsoleRenderer::drawConnectionEditOverlay(const ConnectionEditOverlay& overlay, int vw, int vh) {
    if (!overlay.active) return;

    int displayChars = 0;
    std::vector<int> charBytes;
    {
        size_t i = 0;
        while (i < overlay.text.size()) {
            unsigned char b = static_cast<unsigned char>(overlay.text[i]);
            charBytes.push_back(static_cast<int>(i));
            if (b < 0x80) i += 1; else if (b < 0xE0) i += 2; else i += 3;
            displayChars++;
        }
    }

    int w = std::max(10, displayChars + 4);
    int ox = overlay.screenX - w / 2;
    int oy = overlay.screenY;
    if (oy < 0 || oy >= vh) return;
    uint16_t attr = 0x0F;

    for (int x = ox; x < ox + w; x++) { putChar(x, oy, ' '); putAttr(x, oy, attr); }
    putChar(ox, oy, '['); putAttr(ox, oy, attr);
    putChar(ox + w - 1, oy, ']'); putAttr(ox + w - 1, oy, attr);

    int textStart = ox + 2;
    for (int i = 0; i < displayChars; i++) {
        int bytePos = charBytes[i];
        unsigned char b = static_cast<unsigned char>(overlay.text[bytePos]);
        wchar_t ch;
        if (b < 0x80) ch = b;
        else if (b < 0xE0 && bytePos + 1 < static_cast<int>(overlay.text.size())) {
            ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(overlay.text[bytePos + 1]) & 0x3F);
        } else if (bytePos + 2 < static_cast<int>(overlay.text.size())) {
            ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(overlay.text[bytePos + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(overlay.text[bytePos + 2]) & 0x3F);
        } else { ch = '?'; }
        int px = textStart + i;
        if (px >= 0 && px < vw) { putChar(px, oy, ch); putAttr(px, oy, attr); }
    }

    int cursorPx = textStart + overlay.cursorCol;
    cursorPx = std::max(textStart, std::min(ox + w - 2, cursorPx));
    if (cursorPx >= 0 && cursorPx < vw) putAttr(cursorPx, oy, 0xF0);
}

void ConsoleRenderer::drawHUD(const Viewport& vp, int vw, int vh) {
    uint16_t topAttr = 0x0E, botAttr = 0x08;

    std::ostringstream zoom;
    zoom.setf(std::ios::fixed);
    if (vp.textScale >= 0.1) {
        zoom.precision(0);
        zoom << vp.textScale * 100.0;
    } else {
        zoom.precision(4);
        zoom << vp.textScale * 100.0;
    }

    std::string top = "Lv:" + std::to_string(vp.zoomLevel) +
                       " Z:" + zoom.str() + "%" +
                       " C:(" + std::to_string(static_cast<int>(vp.centerX)) + "," +
                       std::to_string(static_cast<int>(vp.centerY)) + ")";
    for (int x = 0; x < static_cast<int>(top.size()) && x < vw; x++) {
        putChar(x, 0, top[x]);
        putAttr(x, 0, topAttr);
    }

    std::string bot = " DblClick:Select+Edit Click+Drag:Move Click:Clear Pan:DragEmpty Scroll:Zoom [ ]:Level +/-:Text "
                         "N:NewNode Sel+N:Child F:Frame X:Del Tab:Promote Esc:Clear RClick:Menu "
                         "Ctrl+S:Save Ctrl+N:New Ctrl+Q:Quit " +
                        std::string(IC_VERSION_STRING);
    for (int x = 0; x < static_cast<int>(bot.size()) && x < vw; x++) {
        putChar(x, vh - 1, bot[x]);
        putAttr(x, vh - 1, botAttr);
    }
}

void ConsoleRenderer::drawSidebar(const Canvas& canvas) {
    int vw = viewWidth_;
    int vh = viewHeight_;
    int sideW = 26;

    if (vw < sideW + 10) return;

    // Fill sidebar background
    for (int y = 1; y < vh - 1; y++) {
        for (int x = 0; x < sideW; x++) {
            putChar(x, y, L' ');
            putAttr(x, y, 0x01);
        }
    }

    // Borders
    for (int y = 1; y < vh - 1; y++) {
        putChar(0, y, L'\u2502');
        putAttr(0, y, 0x0B);
        putChar(sideW - 1, y, L'\u2502');
        putAttr(sideW - 1, y, 0x0B);
    }

    // Header
    std::string header = "  PROJECT ";
    for (size_t i = 0; i < header.size() && 1 + static_cast<int>(i) < sideW - 1; i++) {
        putChar(1 + static_cast<int>(i), 1, static_cast<wchar_t>(header[i]));
        putAttr(1 + static_cast<int>(i), 1, 0x0B);
    }

    // Check if there are any nodes
    if (canvas.allNodes().empty()) {
        std::string noProj = "No project";
        for (size_t i = 0; i < noProj.size() && 1 + static_cast<int>(i) < sideW - 1; i++) {
            putChar(1 + static_cast<int>(i), 2, static_cast<wchar_t>(noProj[i]));
            putAttr(1 + static_cast<int>(i), 2, 0x08);
        }
        return;
    }

    int row = 2;
    // Draw root nodes first
    drawSidebarNodeTree(canvas, "", 0, row);
}

void ConsoleRenderer::drawSidebarNodeTree(const Canvas& canvas, const std::string& parentUuid, int indent, int& row) {
    int vh = viewHeight_;
    int sideW = 26;

    // Collect children of parentUuid
    std::vector<std::shared_ptr<Node>> children;
    for (const auto& [uuid, node] : canvas.allNodes()) {
        if (node->parentId == parentUuid) {
            children.push_back(node);
        }
    }

    // Sort children by position
    std::sort(children.begin(), children.end(), [](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
        if (a->rect.y != b->rect.y) return a->rect.y < b->rect.y;
        return a->rect.x < b->rect.x;
    });

    for (const auto& node : children) {
        if (row >= vh - 2) return;

        int labelStart = 1 + indent * 2;
        int availWidth = sideW - 2 - indent * 2;

        // Get first line of text, truncated
        std::string label = node->text;
        size_t nlPos = label.find('\n');
        if (nlPos != std::string::npos) label = label.substr(0, nlPos);
        // Truncate to available width
        if (static_cast<int>(label.size()) > availWidth - 2) {
            label = label.substr(0, availWidth - 4) + "..";
        }

        // Prefix
        std::string prefix = "\u00B7 ";
        if (node->state == NodeState::Selected) {
            prefix = "\u2192 ";
        }

        std::string fullLabel = prefix + label;
        uint16_t textAttr = (node->state == NodeState::Selected) ? 0x0E : 0x0B;

        ScreenRect sr;
        sr.x = labelStart;
        sr.y = row;
        sr.w = fullLabel.size();
        sr.h = 1;
        sidebarNodeRects_[node->uuid] = sr;

        for (size_t i = 0; i < fullLabel.size() && labelStart + static_cast<int>(i) < sideW - 1; i++) {
            putChar(labelStart + static_cast<int>(i), row, static_cast<wchar_t>(fullLabel[i]));
            putAttr(labelStart + static_cast<int>(i), row, textAttr);
        }

        row++;

        // Recurse into children
        drawSidebarNodeTree(canvas, node->uuid, indent + 1, row);
    }
}

} // namespace ic
