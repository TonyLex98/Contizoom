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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

namespace ic {

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

        // Clamp to parent bounds
        relX = std::max(0.02, std::min(relX, 0.98));
        relY = std::max(0.02, std::min(relY, 0.98));
        relW = std::max(0.05, std::min(relW, 0.98 - relX));
        relH = std::max(0.05, std::min(relH, 0.98 - relY));

        ScreenRect sr;
        sr.x = parentSr.x + relX * parentSr.w;
        sr.y = parentSr.y + relY * parentSr.h;
        sr.w = relW * parentSr.w;
        sr.h = relH * parentSr.h;
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
    sr.x = (world.x - vp.centerX) * scale + cx;
    sr.y = (world.y - vp.centerY) * scale + cy;
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

    // Connections first
    for (const auto& [_, conn] : canvas.allConnections()) {
        auto fromIt = screenRects_.find(conn->fromUuid);
        auto toIt = screenRects_.find(conn->toUuid);
        if (fromIt != screenRects_.end() && toIt != screenRects_.end()) {
            drawConnectionAt(conn.get(), fromIt->second, toIt->second, vw, vh);
        }
    }

    // Nodes
    for (const auto* node : sorted) {
        auto it = screenRects_.find(node->uuid);
        if (it != screenRects_.end()) {
            drawNodeAt(*node, it->second, vw, vh);
        }
    }

    // Selection
    if (canvas.selection_.active) drawSelection(canvas.selection_, vw, vh, vp);

    // Edit overlay
    if (canvas.editOverlay_.active) drawEditOverlay(canvas.editOverlay_, vw, vh, vp);

    // HUD
    drawHUD(vp, vw, vh);

    // Sidebar
    drawSidebar(canvas);

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

void ConsoleRenderer::drawNodeAt(const Node& node, const ScreenRect& sr, int vw, int vh) {
    if (!sr.valid()) return;

    int ox = static_cast<int>(sr.x);
    int oy = static_cast<int>(sr.y);
    int w = static_cast<int>(sr.w);
    int h = static_cast<int>(sr.h);

    // Dot mode: too small for a box
    if (w < 2 || h < 2) {
        char dot = node.state == NodeState::Selected ? '#' :
                   node.state == NodeState::Hovered ? '~' : '.';
        uint16_t attr = 7;
        if (node.state == NodeState::Selected) attr = 0x0F;
        else if (node.state == NodeState::Hovered) attr = 0x0A;
        putChar(static_cast<int>(sr.centerX()), static_cast<int>(sr.centerY()), dot);
        putAttr(static_cast<int>(sr.centerX()), static_cast<int>(sr.centerY()), attr);
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

    // Top border — ALWAYS render name centered in header
    bool hasHeader = (h >= 3 && w >= 4) || !node.name.empty();
    int textStartY = oy + 1;
    if (hasHeader && w >= 4) {
        // Top-left/right corners
        putChar(ox, oy, corner); putAttr(ox, oy, borderAttr);
        putChar(ox + w - 1, oy, corner); putAttr(ox + w - 1, oy, borderAttr);
        // Fill entire top border with hLine first
        for (int x = ox + 1; x < ox + w - 1; x++) {
            putChar(x, oy, hLine);
            putAttr(x, oy, borderAttr);
        }
        // Name centered in the header row, highlighted
        if (!node.name.empty()) {
            int nameW = w - 2;
            int nameLen = static_cast<int>(node.name.size());
            int startX = (nameW - nameLen) / 2;
            if (startX < 0) startX = 0;
            int drawLen = std::min(nameW, nameLen);
            for (int i = 0; i < drawLen; i++) {
                unsigned char b = static_cast<unsigned char>(node.name[startX + i]);
                wchar_t ch;
                if (b < 0x80) ch = b;
                else if (b < 0xE0 && startX + i + 1 < nameLen) {
                    ch = ((b & 0x1F) << 6) | (static_cast<unsigned char>(node.name[startX + i + 1]) & 0x3F);
                }
                else if (b >= 0xE0 && startX + i + 2 < nameLen) {
                    ch = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(node.name[startX + i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(node.name[startX + i + 2]) & 0x3F);
                }
                else ch = b;
                putChar(ox + 1 + i, oy, ch);
                putAttr(ox + 1 + i, oy, nameAttr);
            }
        }
        textStartY = oy + 1;
    } else {
        // Simple top border, no header room
        putChar(ox, oy, corner); putAttr(ox, oy, borderAttr);
        putChar(ox + w - 1, oy, corner); putAttr(ox + w - 1, oy, borderAttr);
        for (int x = ox + 1; x < ox + w - 1; x++) {
            putChar(x, oy, hLine);
            putAttr(x, oy, borderAttr);
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
    // If node is too small for text but has a name, show name centered in the body
    if (!showText && !node.name.empty() && w >= 4 && h >= 2) {
        int nameLen = static_cast<int>(node.name.size());
        int bodyW = w - 2;
        int startX = (bodyW - nameLen) / 2;
        if (startX < 0) startX = 0;
        for (int i = 0; i < bodyW && i < nameLen; i++) {
            unsigned char b = static_cast<unsigned char>(node.name[i]);
            wchar_t ch = (b < 0x80) ? b : static_cast<wchar_t>(b);
            putChar(ox + 1 + startX + i, oy + h / 2, ch);
            putAttr(ox + 1 + startX + i, oy + h / 2, nameAttr);
        }
    } else if (!showText && w >= 3 && h >= 2 && !node.text.empty()) {
        unsigned char b = static_cast<unsigned char>(node.text[0]);
        wchar_t fc;
        if (b < 0x80) fc = b;
        else if (b < 0xE0 && node.text.size() >= 2) fc = ((b & 0x1F) << 6) | (static_cast<unsigned char>(node.text[1]) & 0x3F);
        else fc = '?';
        putChar(ox + 1, oy + (h > 2 ? 1 : 0), fc);
        putAttr(ox + 1, oy + (h > 2 ? 1 : 0), textAttr);
    }
}

void ConsoleRenderer::drawConnectionAt(const Connection* conn, const ScreenRect& from,
                                        const ScreenRect& to, int, int) {
    if (!from.valid() || !to.valid()) return;

    int x1 = static_cast<int>(from.centerX());
    int y1 = static_cast<int>(from.centerY());
    int x2 = static_cast<int>(to.centerX());
    int y2 = static_cast<int>(to.centerY());

    char ch = '-';
    uint16_t connAttr = 0x08;
    if (conn->type == ConnType::DottedLine || conn->type == ConnType::DottedArrow) {
        ch = '.'; connAttr = 0x05;
    }
    if (conn->type == ConnType::DashedLine || conn->type == ConnType::DashedArrow) {
        ch = '-'; connAttr = 0x06;
    }
    if (conn->type == ConnType::Arrow || conn->type == ConnType::DottedArrow ||
        conn->type == ConnType::DashedArrow) {
        connAttr = 0x09;
    }

    int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        char drawCh = (dx > dy) ? ch : (ch == '.' ? ':' : '|');
        putChar(x1, y1, drawCh);
        putAttr(x1, y1, connAttr);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }

    if (conn->type == ConnType::Arrow || conn->type == ConnType::DottedArrow ||
        conn->type == ConnType::DashedArrow) {
        putChar(x2, y2, '>');
        putAttr(x2, y2, connAttr);
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
    if (w < 3) w = 3;
    if (h < 2) h = 2;

    uint16_t editAttr = 0x0F;

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

void ConsoleRenderer::drawHUD(const Viewport& vp, int vw, int vh) {
    uint16_t topAttr = 0x0E, botAttr = 0x08;

    std::string top = "Lv:" + std::to_string(vp.zoomLevel) +
                       " Z:" + std::to_string(static_cast<int>(vp.textScale * 100)) + "%" +
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
