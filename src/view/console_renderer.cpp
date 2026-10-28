#include "view/console_renderer.h"
#include "i18n/i18n.h"
#include "util/utf8.h"
#include "view/labels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    editorRect_ = ScreenRect{};
    // The sidebar width is the user's (draggable) choice.
    canvasArea_ = CanvasArea::forConsole(buffer_.width(), buffer_.height(), ui.sidebarWidth);

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
    //
    // An endpoint that is collapsed into an ancestor (zoomed out) is drawn
    // to that ancestor instead, dimmed, so links between distant parts of
    // the map stay visible. Several hidden links between the same two
    // visible boxes are drawn once.
    auto representative = [&](const std::string& uuid) {
        std::string id = uuid;
        for (size_t guard = 0; !id.empty() && !drawn.count(id) && guard < 1000; ++guard) {
            auto n = canvas.getNode(id);
            id = n ? n->parentId : std::string();
        }
        return id;
    };
    std::set<std::pair<std::string, std::string>> aggregated;
    for (const auto& [_, conn] : canvas.allConnections()) {
        const bool direct = fullBoxes.count(conn->fromUuid) && fullBoxes.count(conn->toUuid);
        if (direct) {
            drawConnection(*conn, screenRects_[conn->fromUuid], screenRects_[conn->toUuid],
                           ui.selectedConnection == conn->uuid, false);
            continue;
        }
        const std::string from = representative(conn->fromUuid);
        const std::string to = representative(conn->toUuid);
        if (from.empty() || to.empty() || from == to) continue;
        // Skip if one end is an ancestor of the other: the line would run
        // from a box into its own interior.
        if (canvas.isAncestorOf(from, to) || canvas.isAncestorOf(to, from)) continue;
        if (!aggregated.insert({from, to}).second) continue;
        Connection shown = *conn;
        if (from != conn->fromUuid) shown.fromAnchor = Anchor{};
        if (to != conn->toUuid) shown.toAnchor = Anchor{};
        drawConnection(shown, screenRects_[from], screenRects_[to], false, true);
    }
    if (ui.connectPreview && !ui.connectionSource.empty()) drawConnectionPreview(canvas, ui);

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
    // As much of the name as fits in the blank cells (at most 24) there.
    int room = 0;
    while (room < 24 && canvasArea_.contains(cx + 1 + room, cy) && buffer_.charAt(cx + 1 + room, cy) == L' ') ++room;
    buffer_.text(cx + 1, cy, node.name, color::Gray, room);
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
    if (ui.showsAnchors(node.uuid) && canShowAnchors(b)) {
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
        // Hint that there is content: its first character (which may be wide).
        buffer_.text(b.x + 1, b.y + 1, utf8::prefix(node.text, 1), st.text, innerW);
    }
}

void ConsoleRenderer::drawNodeName(const Node& node, const ScreenRect& sr, const UiState& ui) {
    if (node.name.empty()) return;
    if (ui.nodeEditor.active && ui.nodeEditor.editingName && ui.nodeEditor.nodeUuid == node.uuid) return;

    const CellBox b = cellBox(sr);
    const int innerW = b.w - 2;
    if (innerW < 1 || b.h < 2) return;

    const NodeState state = ui.stateOf(node.uuid);
    const int len = std::min(utf8::width(node.name), innerW);
    const int start = b.x + 1 + (innerW - len) / 2;
    buffer_.text(start, b.y, node.name, styleFor(state).name, len);
}

// ====================================================================
// Connections
// ====================================================================

void ConsoleRenderer::drawConnection(const Connection& conn, const ScreenRect& from, const ScreenRect& to,
                                     bool selected, bool dim) {
    if (!from.valid() || !to.valid()) return;

    wchar_t hCh = L'─', vCh = L'│';
    uint16_t attr = isArrow(conn.type) ? color::BrightBlue : color::Gray;
    if (conn.type == ConnType::DottedLine || conn.type == ConnType::DottedArrow) {
        hCh = vCh = L'·';
        attr = color::BrightMagenta;
    } else if (conn.type == ConnType::DashedLine || conn.type == ConnType::DashedArrow) {
        hCh = L'-';
        vCh = L'¦';
        attr = color::Yellow;
    }
    if (dim) attr = color::DarkGray;
    if (selected) attr = color::make(color::Black, color::BrightYellow);
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
    const int len = utf8::width(conn.name);
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

    const CanvasArea& area = canvasArea_;

    if (editor.editingName) {
        // One-line field over the header row, as wide as the text needs.
        const int len = utf8::width(editor.text);
        const int w = std::min(std::max({b.w, len + 4, 16}), area.width());
        const int x = std::clamp(b.x, area.left, area.right - w + 1);
        const int y = std::clamp(b.y, area.top, area.bottom);
        buffer_.fill(x, y, w, 1, L' ', attr);
        buffer_.put(x, y, L'[', attr);
        buffer_.put(x + w - 1, y, L']', attr);
        drawField(x + 2, y, w - 3, editor.text, editor.cursorCol, attr);
        editorRect_ = ScreenRect{static_cast<double>(x), static_cast<double>(y), static_cast<double>(w), 1.0};
        return;
    }

    // Multi-line text: a panel sized to the text (not to the box, which may
    // be a big container whose children must stay visible), placed at the
    // box's top-left and kept on screen. Scrolls to follow the cursor.
    const auto lines = splitLines(editor.text);
    int maxLen = 0;
    for (const auto& l : lines) maxLen = std::max(maxLen, utf8::width(l));
    const int w = std::min(std::max(maxLen + 4, 32), area.width());
    const int h = std::min(std::max(static_cast<int>(lines.size()) + 2, 4), area.height());
    const int x = std::clamp(b.x, area.left, area.right - w + 1);
    const int y = std::clamp(b.y, area.top, area.bottom - h + 1);
    const int innerW = w - 2, innerH = h - 2;
    const int firstRow = std::max(0, editor.cursorRow - innerH + 1);
    // Horizontal scroll in cells, so the cursor stays visible in wide text.
    const std::string& cursorLine = lines[static_cast<size_t>(std::clamp(editor.cursorRow, 0, static_cast<int>(lines.size()) - 1))];
    const int cursorCells = utf8::widthOfPrefix(cursorLine, editor.cursorCol);
    const int firstCells = std::max(0, cursorCells - innerW + 1);

    buffer_.fill(x, y, w, h, L' ', attr);
    for (int cx = x + 1; cx < x + w - 1; ++cx) {
        buffer_.put(cx, y, L'═', attr);
        buffer_.put(cx, y + h - 1, L'═', attr);
    }
    for (int cy = y + 1; cy < y + h - 1; ++cy) {
        buffer_.put(x, cy, L'║', attr);
        buffer_.put(x + w - 1, cy, L'║', attr);
    }
    buffer_.put(x, y, L'╔', attr);
    buffer_.put(x + w - 1, y, L'╗', attr);
    buffer_.put(x, y + h - 1, L'╚', attr);
    buffer_.put(x + w - 1, y + h - 1, L'╝', attr);
    buffer_.text(x + 2, y, std::string(" ") + tr(Msg::EditorTextTitle) + " ",
                 color::make(color::BrightYellow, color::Blue), w - 4);

    for (int row = 0; row < innerH && firstRow + row < static_cast<int>(lines.size()); ++row) {
        const std::string& l = lines[static_cast<size_t>(firstRow + row)];
        buffer_.text(x + 1, y + 1 + row, l.substr(utf8::fitWidth(l, firstCells).size()), attr, innerW);
    }
    const int cx = x + 1 + cursorCells - firstCells;
    const int cy = y + 1 + editor.cursorRow - firstRow;
    if (buffer_.charAt(cx, cy) == L' ') buffer_.put(cx, cy, L'_', color::Inverse);
    else buffer_.setAttr(cx, cy, color::Inverse);

    editorRect_ = ScreenRect{static_cast<double>(x), static_cast<double>(y), static_cast<double>(w),
                             static_cast<double>(h)};
}

void ConsoleRenderer::drawConnectionPreview(const Canvas& canvas, const UiState& ui) {
    auto it = screenRects_.find(ui.connectionSource);
    if (it == screenRects_.end() || !canvas.getNode(ui.connectionSource)) return;

    // Snap to the box under the cursor (and its anchor) when there is one.
    ScreenRect target{static_cast<double>(ui.previewX), static_cast<double>(ui.previewY), 1.0, 1.0};
    Connection preview;
    preview.fromAnchor = ui.previewFromAnchor;
    if (!ui.hovered.empty() && ui.hovered != ui.connectionSource) {
        if (auto t = screenRects_.find(ui.hovered); t != screenRects_.end()) {
            target = t->second;
            if (ui.anchorHoverNode == ui.hovered) preview.toAnchor = ui.anchorHover;
        }
    }
    const CellPath pts = routeConnection(it->second, target, preview);
    const uint16_t attr = color::BrightYellow;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const auto [sx, sy] = pts[i];
        const auto [ex, ey] = pts[i + 1];
        for (int y = std::min(sy, ey); y <= std::max(sy, ey); ++y)
            for (int x = std::min(sx, ex); x <= std::max(sx, ex); ++x)
                if (std::make_pair(x, y) != pts.front()) buffer_.put(x, y, L'·', attr);
    }
    buffer_.put(ui.previewX, ui.previewY, L'+', attr);
}

void ConsoleRenderer::drawConnectionEditor(const ConnectionEditor& editor) {
    const int len = utf8::width(editor.text);
    const int w = std::max(10, len + 4);
    const int x = editor.screenX - w / 2;
    const int y = editor.screenY;
    const uint16_t attr = color::make(color::White, color::Blue);

    buffer_.fill(x, y, w, 1, L' ', attr);
    buffer_.put(x, y, L'[', attr);
    buffer_.put(x + w - 1, y, L']', attr);
    drawField(x + 2, y, w - 3, editor.text, editor.cursorCol, attr);
}

// One line of editable text in `cells` cells, scrolled so the cursor (a
// code point index) is visible. Widths are in cells, so CJK text works.
void ConsoleRenderer::drawField(int x, int y, int cells, const std::string& text, int cursor, uint16_t attr) {
    const int cursorCells = utf8::widthOfPrefix(text, cursor);
    int first = 0;  // first visible code point
    while (first < cursor && cursorCells - utf8::widthOfPrefix(text, first) >= cells) ++first;
    const int offset = utf8::widthOfPrefix(text, first);
    buffer_.text(x, y, text.substr(utf8::byteOffset(text, first)), attr, cells);
    const int cx = x + cursorCells - offset;
    if (buffer_.charAt(cx, y) == L' ') buffer_.put(cx, y, L'_', color::Inverse);
    else buffer_.setAttr(cx, y, color::Inverse);
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

    const double pct = canvas.viewport.scale * 100.0;
    char zoom[32];
    std::snprintf(zoom, sizeof(zoom), "%.*f", pct >= 10.0 ? 0 : pct >= 0.1 ? 2 : 4, pct);

    std::string left = " Infinite Canvas  │  " + (hud.projectName.empty() ? std::string(tr(Msg::DefaultProject)) : hud.projectName);
    left += std::string("  │  ") + tr(Msg::HudZoom) + " " + zoom + "%  │  " +
            tr(Msg::Boxes, {std::to_string(canvas.allNodes().size())});
    buffer_.text(0, 0, left, barAttr, w);

    if (!hud.status.empty()) {
        const std::string status = " " + hud.status + " ";
        const int len = utf8::width(status);
        const uint16_t attr = hud.statusIsError ? color::make(color::White, color::Red)
                                                : color::make(color::Black, color::Green);
        if (len < w) buffer_.text(w - len, 0, status, attr);
        else buffer_.text(0, 0, status, attr, w);  // long messages win over the title
    }

    // Bottom bar: the keys that matter right now.
    std::string help;
    if (ui.nodeEditor.active && !ui.nodeEditor.editingName) {
        help = tr(Msg::HelpEditText);
    } else if (ui.nodeEditor.active || ui.connEditor.active) {
        help = tr(Msg::HelpEditLine);
    } else if (!ui.connectionSource.empty()) {
        help = tr(Msg::HelpConnecting);
    } else if (ui.dividerHover || ui.draggingDivider) {
        help = tr(Msg::HelpDivider);
    } else if (auto c = canvas.getConnection(ui.selectedConnection)) {
        help = tr(Msg::HelpConnection, {tr(styleLabel(c->type))});
    } else {
        help = tr(Msg::HelpDefault);
    }
    buffer_.fill(0, bottom, w, 1, L' ', color::make(color::Gray, color::Black));
    buffer_.text(1, bottom, help, color::DarkGray, w - 1);
}

void ConsoleRenderer::drawSidebar(const Canvas& canvas, const UiState& ui) {
    const int sideW = canvasArea_.left;
    const int h = buffer_.height();
    if (sideW <= 0) return;

    buffer_.fill(0, 1, sideW, h - 2, L' ', color::Default);
    // The divider is a handle: brighter and doubled while hovered or dragged.
    const bool active = ui.dividerHover || ui.draggingDivider;
    for (int y = 1; y < h - 1; ++y) {
        buffer_.put(sideW - 1, y, active ? L'║' : L'│', active ? color::BrightCyan : color::DarkGray);
    }
    if (sideW < 8) return;  // collapsed: just the handle

    buffer_.text(1, 1, tr(Msg::SidebarTitle), color::BrightCyan, sideW - 2);
    if (canvas.allNodes().empty()) {
        buffer_.text(1, 3, tr(Msg::SidebarEmpty), color::DarkGray, sideW - 2);
        buffer_.text(1, 4, tr(Msg::SidebarEmptyHint), color::DarkGray, sideW - 2);
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
        if (label.empty()) label = tr(Msg::Untitled);

        const int x = 1 + std::min(indent, 6) * 2;
        const int avail = sideW - 1 - x - 2;  // keep clear of the border
        if (avail >= 3) {
            if (utf8::width(label) > avail) label = utf8::fitWidth(label, avail - 1) + "…";
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
