#include "view/console_renderer.h"
#include "util/utf8.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace ic {

namespace {

// Every glyph used here is in the WGL4 set, so it renders with the stock
// Windows console fonts (Consolas, Lucida Console, Cascadia).
struct BoxStyle {
    wchar_t tl, tr, bl, br, h, v;
    uint16_t border, text, name;
};

BoxStyle styleFor(NodeState state) {
    switch (state) {
        case NodeState::Selected:
            return {L'╔', L'╗', L'╚', L'╝', L'═', L'║', color::White, color::White, color::BrightYellow};
        case NodeState::Hovered:
            return {L'┌', L'┐', L'└', L'┘', L'─', L'│', color::BrightGreen, color::Gray, color::BrightYellow};
        case NodeState::InConnection:
            return {L'┌', L'┐', L'└', L'┘', L'─', L'│', color::BrightRed, color::Gray, color::BrightYellow};
        case NodeState::Normal: break;
    }
    return {L'┌', L'┐', L'└', L'┘', L'─', L'│', color::Cyan, color::Gray, color::BrightYellow};
}

// Big enough for a bordered box with a name label.
bool isFullBox(const ScreenRect& sr) {
    return sr.w >= 4.0 && sr.h >= 2.0;
}

// Too small for a label but still a real box (not a single-cell marker).
bool isCompactBox(const ScreenRect& sr) {
    return sr.w >= 2.0 && sr.h >= 2.0;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines(1);
    for (const char c : text) {
        if (c == '\n') lines.emplace_back();
        else if (c != '\r') lines.back() += c;
    }
    return lines;
}

std::string firstLine(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

// Glyph joining the given sides of a cell (for connection bends).
wchar_t junction(bool up, bool down, bool left, bool right) {
    if ((up || down) && !left && !right) return L'│';
    if ((left || right) && !up && !down) return L'─';
    if (down && right && !up && !left) return L'┌';
    if (down && left && !up && !right) return L'┐';
    if (up && right && !down && !left) return L'└';
    if (up && left && !down && !right) return L'┘';
    return L'┼';
}

int sign(int v) {
    return (v > 0) - (v < 0);
}

}  // namespace

void ConsoleRenderer::resize(int width, int height) {
    buffer_.resize(width, height);
    canvasArea_ = CanvasArea::forConsole(width, height);
}

// ====================================================================
// Layout
// ====================================================================

ScreenRect ConsoleRenderer::computeScreenRect(const Node& node, const Canvas& canvas) {
    if (auto it = screenRects_.find(node.uuid); it != screenRects_.end()) return it->second;

    ScreenRect sr;
    if (auto parent = canvas.getNode(node.parentId)) {
        // Children are laid out proportionally inside the parent's box.
        const ScreenRect psr = computeScreenRect(*parent, canvas);
        const double pw = parent->rect.width, ph = parent->rect.height;
        sr.x = psr.x + (pw > 0 ? node.rect.x / pw : 0.0) * psr.w;
        sr.y = psr.y + (ph > 0 ? node.rect.y / ph : 0.0) * psr.h;
        sr.w = (pw > 0 ? node.rect.width / pw : 0.1) * psr.w;
        sr.h = (ph > 0 ? node.rect.height / ph : 0.2) * psr.h;

        // Keep the child strictly inside the parent's border. Without this,
        // independent rounding of parent and child lets a child near the
        // edge drift onto the border, and the drift compounds per level.
        const double ix0 = psr.x + 1.0, iy0 = psr.y + 1.0;
        const double ix1 = psr.x + psr.w - 1.0, iy1 = psr.y + psr.h - 1.0;
        if (ix1 > ix0 && iy1 > iy0) {
            sr.x = std::clamp(sr.x, ix0, ix1);
            sr.y = std::clamp(sr.y, iy0, iy1);
            sr.w = std::min(sr.w, ix1 - sr.x);
            sr.h = std::min(sr.h, iy1 - sr.y);
        }
        sr.w = std::max(0.0, sr.w);
        sr.h = std::max(0.0, sr.h);
    } else {
        const Viewport& vp = canvas.viewport;
        sr.x = (node.rect.x - vp.centerX) * vp.scale + canvasArea_.centerX();
        sr.y = (node.rect.y - vp.centerY) * vp.scale + canvasArea_.centerY();
        sr.w = node.rect.width * vp.scale;
        sr.h = node.rect.height * vp.scale;
    }
    screenRects_[node.uuid] = sr;
    return sr;
}

void ConsoleRenderer::render(const Canvas& canvas, const UiState& ui, const HudInfo& hud) {
    buffer_.clear();
    screenRects_.clear();
    sidebarRects_.clear();

    auto visible = canvas.getVisibleNodes(canvas.viewport, canvasArea_.width(), canvasArea_.height());
    std::vector<const Node*> sorted;
    sorted.reserve(visible.size());
    for (const auto& n : visible) {
        computeScreenRect(*n, canvas);
        sorted.push_back(n.get());
    }
    // Parents before children.
    std::stable_sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) { return a->depth < b->depth; });

    // A node's children are only drawn individually once the node itself is
    // a full box with real interior room. Until then the parent's own box or
    // marker represents its whole subtree, which keeps zoomed-out views
    // readable instead of collapsing every level into overlapping noise.
    std::set<std::string> fullBoxes, compactBoxes, canHostChildren;
    for (const Node* node : sorted) {
        if (!node->parentId.empty() && !canHostChildren.count(node->parentId)) continue;
        const ScreenRect& sr = screenRects_[node->uuid];
        if (isFullBox(sr)) {
            fullBoxes.insert(node->uuid);
            const CellBox b = cellBox(sr);
            if (b.w >= 3 && b.h >= 3) canHostChildren.insert(node->uuid);
        } else if (isCompactBox(sr)) {
            compactBoxes.insert(node->uuid);
        }
    }

    std::set<std::string> drawn = fullBoxes;
    drawn.insert(compactBoxes.begin(), compactBoxes.end());

    // Point-sized nodes compete for single cells; several landing on the
    // same cell become one cluster marker.
    auto markerCell = [](const ScreenRect& sr) {
        return std::pair<int, int>{static_cast<int>(std::round(sr.centerX())), static_cast<int>(std::round(sr.centerY()))};
    };
    std::map<std::pair<int, int>, int> cellCounts;
    std::map<std::pair<int, int>, std::string> cellOwner;
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        const Node* node = *it;
        if (drawn.count(node->uuid)) continue;
        if (!node->parentId.empty() && !canHostChildren.count(node->parentId)) continue;
        const ScreenRect& sr = screenRects_[node->uuid];
        if (!sr.valid()) continue;
        const auto cell = markerCell(sr);
        if (!canvasArea_.contains(cell.first, cell.second)) continue;
        if (++cellCounts[cell] == 1) {
            cellOwner[cell] = node->uuid;
            drawn.insert(node->uuid);
        }
    }
    std::set<std::string> clusters;
    for (const auto& [cell, count] : cellCounts) {
        if (count > 1) clusters.insert(cellOwner[cell]);
    }

    // Snap markers to one cell, kept inside the parent's interior.
    for (const auto& uuid : drawn) {
        if (fullBoxes.count(uuid) || compactBoxes.count(uuid)) continue;
        ScreenRect& sr = screenRects_[uuid];
        auto [x, y] = markerCell(sr);
        if (auto n = canvas.getNode(uuid); n && !n->parentId.empty()) {
            const CellBox p = cellBox(screenRects_[n->parentId]);
            if (p.right() - 1 >= p.x + 1) x = std::clamp(x, p.x + 1, p.right() - 1);
            if (p.bottom() - 1 >= p.y + 1) y = std::clamp(y, p.y + 1, p.bottom() - 1);
        }
        sr = ScreenRect{static_cast<double>(x), static_cast<double>(y), 1.0, 1.0};
    }

    for (const Node* node : sorted) {
        if (drawn.count(node->uuid)) drawNode(*node, screenRects_[node->uuid], ui, clusters.count(node->uuid) > 0);
    }

    // After the boxes: a connection between two children must sit on top of
    // their parent's (opaque) interior.
    for (const auto& [_, conn] : canvas.allConnections()) {
        if (!fullBoxes.count(conn->fromUuid) || !fullBoxes.count(conn->toUuid)) continue;
        drawConnection(*conn, screenRects_[conn->fromUuid], screenRects_[conn->toUuid],
                       ui.selectedConnection == conn->uuid);
    }

    // Names last among canvas content, parents above children, so a child
    // box can never hide its parent's label.
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        const Node* node = *it;
        if (fullBoxes.count(node->uuid) || compactBoxes.count(node->uuid)) drawNodeName(*node, screenRects_[node->uuid], ui);
    }
    for (const auto& [_, conn] : canvas.allConnections()) {
        if (!fullBoxes.count(conn->fromUuid) || !fullBoxes.count(conn->toUuid)) continue;
        drawConnectionLabel(*conn, screenRects_[conn->fromUuid], screenRects_[conn->toUuid],
                            ui.selectedConnection == conn->uuid);
    }

    if (ui.area.active) drawAreaSelection(ui.area, canvas.viewport);
    if (ui.nodeEditor.active) drawNodeEditor(ui.nodeEditor);
    if (ui.connEditor.active) drawConnectionEditor(ui.connEditor);

    drawSidebar(canvas, ui);
    drawHud(canvas, ui, hud);

    // Only what's actually on screen is clickable.
    for (auto it = screenRects_.begin(); it != screenRects_.end();) {
        const bool editing = ui.nodeEditor.active && ui.nodeEditor.nodeUuid == it->first;
        if (!drawn.count(it->first) && !editing) it = screenRects_.erase(it);
        else ++it;
    }
}

// ====================================================================
// Nodes
// ====================================================================

void ConsoleRenderer::drawMarker(const Node& node, const ScreenRect& sr, NodeState state, bool isCluster) {
    const int cx = static_cast<int>(sr.centerX());
    const int cy = static_cast<int>(sr.centerY());

    wchar_t glyph = node.childrenUuids.empty() ? L'▪' : L'■';
    uint16_t attr = node.childrenUuids.empty() ? color::Gray : color::Cyan;
    if (isCluster) {
        glyph = L'*';
        attr = color::BrightYellow;
    } else if (state == NodeState::Selected) {
        glyph = L'●';
        attr = color::White;
    } else if (state == NodeState::Hovered) {
        glyph = L'○';
        attr = color::BrightGreen;
    }
    buffer_.put(cx, cy, glyph, attr);

    // A named marker gets its label in the free space to its right, so a
    // collapsed box doesn't look like empty canvas. Clusters don't: one
    // member's name would mislabel the others.
    if (isCluster || node.name.empty()) return;
    int x = cx + 1;
    int written = 0;
    utf8::forEach(node.name, [&](char32_t cp) {
        if (written >= 24 || !canvasArea_.contains(x, cy) || buffer_.charAt(x, cy) != L' ') return;
        buffer_.put(x++, cy, utf8::toCell(cp), color::Gray);
        ++written;
    });
}

void ConsoleRenderer::drawNode(const Node& node, const ScreenRect& sr, const UiState& ui, bool isCluster) {
    if (!sr.valid()) return;
    const NodeState state = ui.stateOf(node.uuid);
    const CellBox b = cellBox(sr);
    if (b.w < 2 || b.h < 2) {
        drawMarker(node, sr, state, isCluster);
        return;
    }

    const BoxStyle st = styleFor(state);
    // Opaque interior: whatever lies underneath (a parent's text, a line
    // passing behind) doesn't bleed through.
    buffer_.fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, L' ', color::Default);
    for (int x = b.x + 1; x < b.right(); ++x) {
        buffer_.put(x, b.y, st.h, st.border);
        buffer_.put(x, b.bottom(), st.h, st.border);
    }
    for (int y = b.y + 1; y < b.bottom(); ++y) {
        buffer_.put(b.x, y, st.v, st.border);
        buffer_.put(b.right(), y, st.v, st.border);
    }
    // Corners double as resize handles on a selected box; the one under the
    // mouse is shown in reverse video.
    const bool handles = state == NodeState::Selected && ui.cornerHoverNode == node.uuid;
    auto cornerAttr = [&](ResizeCorner c) { return handles && ui.cornerHover == c ? color::Inverse : st.border; };
    buffer_.put(b.x, b.y, st.tl, cornerAttr(ResizeCorner::TopLeft));
    buffer_.put(b.right(), b.y, st.tr, cornerAttr(ResizeCorner::TopRight));
    buffer_.put(b.x, b.bottom(), st.bl, cornerAttr(ResizeCorner::BottomLeft));
    buffer_.put(b.right(), b.bottom(), st.br, cornerAttr(ResizeCorner::BottomRight));

    // Connection anchors: drag from one to another box to link them.
    if (canShowAnchors(b)) {
        for (const Anchor& a : allAnchors()) {
            int px = 0, py = 0;
            if (!anchorCell(b, a, px, py)) continue;
            const bool hovered = ui.anchorHoverNode == node.uuid && ui.anchorHover == a;
            buffer_.put(px, py, L'•', hovered ? color::Inverse : color::BrightCyan);
        }
    }

    // Body text appears only once there's room; it's the first thing to go
    // when zooming out.
    if (node.text.empty()) return;
    const int innerW = b.w - 2;
    if (b.w >= 5 && b.h >= 4) {
        // Text rows don't scale with zoom, children do: stop above the first
        // child so lines never run underneath a child's box.
        int lastRow = b.bottom() - 1;
        for (const auto& cid : node.childrenUuids) {
            auto it = screenRects_.find(cid);
            if (it != screenRects_.end() && it->second.valid())
                lastRow = std::min(lastRow, static_cast<int>(std::floor(it->second.y)) - 1);
        }
        int y = b.y + 1;
        for (const auto& line : splitLines(node.text)) {
            if (y > lastRow) break;
            buffer_.text(b.x + 1, y++, line, st.text, innerW);
        }
    } else if (b.w >= 3 && b.h >= 3) {
        // Hint that there is content: its first character.
        size_t pos = 0;
        buffer_.put(b.x + 1, b.y + 1, utf8::toCell(utf8::decode(node.text, pos)), st.text);
    }
}

void ConsoleRenderer::drawNodeName(const Node& node, const ScreenRect& sr, const UiState& ui) {
    if (node.name.empty()) return;
    if (ui.nodeEditor.active && ui.nodeEditor.editingName && ui.nodeEditor.nodeUuid == node.uuid) return;

    const CellBox b = cellBox(sr);
    const int innerW = b.w - 2;
    if (innerW < 1 || b.h < 2) return;

    const NodeState state = ui.stateOf(node.uuid);
    const int len = std::min(utf8::length(node.name), innerW);
    const int start = b.x + 1 + (innerW - len) / 2;
    buffer_.text(start, b.y, node.name, styleFor(state).name, len);
}

// ====================================================================
// Connections
// ====================================================================

void ConsoleRenderer::drawConnection(const Connection& conn, const ScreenRect& from, const ScreenRect& to,
                                     bool selected) {
    if (!from.valid() || !to.valid()) return;

    wchar_t hCh = L'─', vCh = L'│';
    uint16_t attr = isArrow(conn.type) ? color::BrightBlue : color::DarkGray;
    if (conn.type == ConnType::DottedLine || conn.type == ConnType::DottedArrow) {
        hCh = vCh = L'·';
        attr = color::Magenta;
    } else if (conn.type == ConnType::DashedLine || conn.type == ConnType::DashedArrow) {
        hCh = L'-';
        vCh = L'¦';
        attr = color::Yellow;
    }
    if (selected) attr = color::White;
    const bool solid = hCh == L'─';

    const CellPath pts = routeConnection(from, to, conn);
    // The two end cells lie on the boxes' borders (often an anchor): leave
    // those to the boxes.
    auto put = [&](int x, int y, wchar_t ch) {
        if ((x == pts.front().first && y == pts.front().second) || (x == pts.back().first && y == pts.back().second))
            return;
        buffer_.put(x, y, ch, attr);
    };
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const auto [sx, sy] = pts[i];
        const auto [ex, ey] = pts[i + 1];
        if (sx == ex) {
            for (int y = std::min(sy, ey); y <= std::max(sy, ey); ++y) put(sx, y, vCh);
        } else if (sy == ey) {
            for (int x = std::min(sx, ex); x <= std::max(sx, ex); ++x) put(x, sy, hCh);
        }
    }

    // Proper corner glyphs where the route bends.
    if (solid) {
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            const auto [px, py] = pts[i - 1];
            const auto [cx, cy] = pts[i];
            const auto [nx, ny] = pts[i + 1];
            const bool up = py < cy || ny < cy, down = py > cy || ny > cy;
            const bool left = px < cx || nx < cx, right = px > cx || nx > cx;
            if (up || down || left || right) put(cx, cy, junction(up, down, left, right));
        }
    }

    if (isArrow(conn.type) && pts.size() >= 2) {
        // The route ends on the target's border, which is drawn over it, so
        // the head goes one cell outside, pointing in.
        const auto [ex, ey] = pts.back();
        const auto [sx, sy] = pts[pts.size() - 2];
        const int ox = sign(sx - ex), oy = sign(sy - ey);
        const wchar_t head = ox > 0 ? L'◄' : ox < 0 ? L'►' : oy > 0 ? L'▲' : L'▼';
        buffer_.put(ex + ox, ey + oy, head, attr);
    }
}

void ConsoleRenderer::drawConnectionLabel(const Connection& conn, const ScreenRect& from, const ScreenRect& to,
                                          bool selected) {
    if (conn.name.empty() || !from.valid() || !to.valid()) return;
    // Middle of the longest segment: the most room for the text.
    const CellPath pts = routeConnection(from, to, conn);
    size_t best = 0;
    int bestLen = -1;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const int len = std::abs(pts[i + 1].first - pts[i].first) + std::abs(pts[i + 1].second - pts[i].second);
        if (len > bestLen) {
            bestLen = len;
            best = i;
        }
    }
    const int mx = (pts[best].first + pts[best + 1].first) / 2;
    const int my = (pts[best].second + pts[best + 1].second) / 2;
    const bool horizontal = pts[best].second == pts[best + 1].second;
    const int len = utf8::length(conn.name);
    const int x = horizontal ? mx - len / 2 : mx + 1;
    const int y = horizontal ? my - 1 : my;  // above a horizontal line, beside a vertical one

    // Only where it fits on empty canvas; otherwise it appears once you
    // zoom in far enough to make room.
    for (int i = 0; i < len; ++i) {
        if (!canvasArea_.contains(x + i, y) || buffer_.charAt(x + i, y) != L' ') return;
    }
    buffer_.text(x, y, conn.name, selected ? color::White : color::BrightYellow);
}

// ====================================================================
// Overlays
// ====================================================================

void ConsoleRenderer::drawAreaSelection(const AreaSelection& sel, const Viewport& vp) {
    const double cx = canvasArea_.centerX(), cy = canvasArea_.centerY();
    const int x1 = static_cast<int>((sel.left() - vp.centerX) * vp.scale + cx);
    const int y1 = static_cast<int>((sel.top() - vp.centerY) * vp.scale + cy);
    const int x2 = static_cast<int>((sel.right() - vp.centerX) * vp.scale + cx);
    const int y2 = static_cast<int>((sel.bottom() - vp.centerY) * vp.scale + cy);

    const uint16_t attr = color::White;
    for (int x = x1 + 1; x < x2; ++x) {
        buffer_.put(x, y1, L'─', attr);
        buffer_.put(x, y2, L'─', attr);
    }
    for (int y = y1 + 1; y < y2; ++y) {
        buffer_.put(x1, y, L'│', attr);
        buffer_.put(x2, y, L'│', attr);
    }
    buffer_.put(x1, y1, L'┌', attr);
    buffer_.put(x2, y1, L'┐', attr);
    buffer_.put(x1, y2, L'└', attr);
    buffer_.put(x2, y2, L'┘', attr);
}

void ConsoleRenderer::drawNodeEditor(const NodeEditor& editor) {
    auto it = screenRects_.find(editor.nodeUuid);
    if (it == screenRects_.end() || !it->second.valid()) return;
    const CellBox b = cellBox(it->second);
    const uint16_t attr = color::make(color::White, color::Blue);

    if (editor.editingName) {
        // One-line field over the header row.
        const int len = utf8::length(editor.text);
        const int w = std::max(b.w, len + 4);
        buffer_.fill(b.x, b.y, w, 1, L' ', attr);
        buffer_.put(b.x, b.y, L'[', attr);
        buffer_.put(b.x + w - 1, b.y, L']', attr);
        buffer_.text(b.x + 2, b.y, editor.text, attr, w - 3);
        const int cursorX = std::min(b.x + 2 + editor.cursorCol, b.x + w - 2);
        buffer_.setAttr(cursorX, b.y, color::Inverse);
        if (buffer_.charAt(cursorX, b.y) == L' ') buffer_.put(cursorX, b.y, L'_', color::Inverse);
        it->second.w = std::max(it->second.w, static_cast<double>(w));
        return;
    }

    // Multi-line editor: grows past the box when the text needs more room.
    const auto lines = splitLines(editor.text);
    int maxLen = 0;
    for (const auto& l : lines) maxLen = std::max(maxLen, utf8::length(l));
    const int w = std::max(b.w, maxLen + 3);
    const int h = std::max(b.h, static_cast<int>(lines.size()) + 2);

    buffer_.fill(b.x, b.y, w, h, L' ', attr);
    for (int x = b.x + 1; x < b.x + w - 1; ++x) {
        buffer_.put(x, b.y, L'═', attr);
        buffer_.put(x, b.y + h - 1, L'═', attr);
    }
    for (int y = b.y + 1; y < b.y + h - 1; ++y) {
        buffer_.put(b.x, y, L'║', attr);
        buffer_.put(b.x + w - 1, y, L'║', attr);
    }
    buffer_.put(b.x, b.y, L'╔', attr);
    buffer_.put(b.x + w - 1, b.y, L'╗', attr);
    buffer_.put(b.x, b.y + h - 1, L'╚', attr);
    buffer_.put(b.x + w - 1, b.y + h - 1, L'╝', attr);

    for (size_t i = 0; i < lines.size(); ++i) {
        buffer_.text(b.x + 1, b.y + 1 + static_cast<int>(i), lines[i], attr, w - 2);
    }
    const int cx = b.x + 1 + editor.cursorCol;
    const int cy = b.y + 1 + editor.cursorRow;
    if (buffer_.charAt(cx, cy) == L' ') buffer_.put(cx, cy, L'_', color::Inverse);
    else buffer_.setAttr(cx, cy, color::Inverse);

    it->second.w = std::max(it->second.w, static_cast<double>(w));
    it->second.h = std::max(it->second.h, static_cast<double>(h));
}

void ConsoleRenderer::drawConnectionEditor(const ConnectionEditor& editor) {
    const int len = utf8::length(editor.text);
    const int w = std::max(10, len + 4);
    const int x = editor.screenX - w / 2;
    const int y = editor.screenY;
    const uint16_t attr = color::make(color::White, color::Blue);

    buffer_.fill(x, y, w, 1, L' ', attr);
    buffer_.put(x, y, L'[', attr);
    buffer_.put(x + w - 1, y, L']', attr);
    buffer_.text(x + 2, y, editor.text, attr, w - 3);
    const int cursorX = std::clamp(x + 2 + editor.cursorCol, x + 2, x + w - 2);
    if (buffer_.charAt(cursorX, y) == L' ') buffer_.put(cursorX, y, L'_', color::Inverse);
    else buffer_.setAttr(cursorX, y, color::Inverse);
}

// ====================================================================
// Chrome: status rows and sidebar
// ====================================================================

void ConsoleRenderer::drawHud(const Canvas& canvas, const UiState& ui, const HudInfo& hud) {
    const int w = buffer_.width();
    const int bottom = buffer_.height() - 1;
    if (w <= 0 || bottom < 1) return;

    // Top bar: what you're looking at.
    const uint16_t barAttr = color::make(color::White, color::Blue);
    buffer_.fill(0, 0, w, 1, L' ', barAttr);

    std::ostringstream zoom;
    zoom.setf(std::ios::fixed);
    const double pct = canvas.viewport.scale * 100.0;
    zoom.precision(pct >= 10.0 ? 0 : pct >= 0.1 ? 2 : 4);
    zoom << pct;

    std::string left = " Infinite Canvas  │  " + (hud.projectName.empty() ? std::string("Untitled") : hud.projectName);
    if (hud.unsaved) left += " (not saved yet)";
    left += "  │  zoom " + zoom.str() + "%  │  " + std::to_string(canvas.allNodes().size()) + " boxes";
    buffer_.text(0, 0, left, barAttr, w);

    if (!hud.status.empty()) {
        const std::string status = " " + hud.status + " ";
        const int len = utf8::length(status);
        const uint16_t attr = hud.statusIsError ? color::make(color::White, color::Red)
                                                : color::make(color::Black, color::Green);
        if (len < w) buffer_.text(w - len, 0, status, attr);
    }

    // Bottom bar: the keys that matter right now.
    std::string help;
    if (ui.nodeEditor.active && !ui.nodeEditor.editingName) {
        help = " Enter: new line   Ctrl+Enter or click outside: save   Esc: cancel";
    } else if (ui.nodeEditor.active || ui.connEditor.active) {
        help = " Enter: save   Esc: cancel";
    } else if (!ui.connectionSource.empty()) {
        help = " Release over another box to connect   Esc: cancel";
    } else {
        help = " N new   Dbl-click edit   R rename   Drag move   Corner resize   Drag • connect   "
               "Tab/Shift+Tab un/nest   F frame   X delete   Wheel zoom   WASD pan   0 fit   "
               "Shift+drag select   Right-click menu   Ctrl+S save   Ctrl+Q quit";
    }
    buffer_.fill(0, bottom, w, 1, L' ', color::make(color::Gray, color::Black));
    buffer_.text(0, bottom, help, color::DarkGray, w);
}

void ConsoleRenderer::drawSidebar(const Canvas& canvas, const UiState& ui) {
    const int sideW = canvasArea_.left;
    const int h = buffer_.height();
    if (sideW <= 0) return;

    buffer_.fill(0, 1, sideW, h - 2, L' ', color::Default);
    for (int y = 1; y < h - 1; ++y) buffer_.put(sideW - 1, y, L'│', color::DarkGray);

    buffer_.text(1, 1, "OUTLINE", color::BrightCyan);
    if (canvas.allNodes().empty()) {
        buffer_.text(1, 3, "Empty canvas.", color::DarkGray);
        buffer_.text(1, 4, "Press N to add a box.", color::DarkGray);
        return;
    }
    int row = 3;
    drawSidebarTree(canvas, ui, "", 0, row);
}

void ConsoleRenderer::drawSidebarTree(const Canvas& canvas, const UiState& ui, const std::string& parentUuid,
                                      int indent, int& row) {
    const int sideW = canvasArea_.left;
    const int maxRow = buffer_.height() - 2;

    for (const auto& node : canvas.getChildren(parentUuid)) {
        if (row > maxRow) return;

        std::string label = node->name;
        if (label.empty()) label = firstLine(node->text);
        if (label.empty()) label = "(untitled)";

        const int x = 1 + std::min(indent, 6) * 2;
        const int avail = sideW - 1 - x - 2;  // keep clear of the border
        if (avail >= 3) {
            if (utf8::length(label) > avail) label = utf8::prefix(label, avail - 1) + "…";
            const bool selected = ui.isSelected(node->uuid);
            const wchar_t bullet = selected ? L'►' : node->childrenUuids.empty() ? L'·' : L'▪';
            const uint16_t attr = selected ? color::BrightYellow : color::Gray;
            buffer_.put(x, row, bullet, selected ? color::BrightYellow : color::Cyan);
            const int n = buffer_.text(x + 2, row, label, attr, avail);
            sidebarRects_[node->uuid] = ScreenRect{static_cast<double>(x), static_cast<double>(row),
                                                   static_cast<double>(n + 2), 1.0};
        }
        ++row;
        drawSidebarTree(canvas, ui, node->uuid, indent + 1, row);
    }
}

}  // namespace ic
