#include "input/handler.h"
#include "model/canvas.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <sstream>
#include <vector>

namespace ic {

InputHandler::InputHandler(Canvas& canvas) : canvas_(canvas) {}

static std::vector<std::string> splitEditorLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;

    for (char c : text) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }

    lines.push_back(line);
    return lines;
}

static std::string joinEditorLines(const std::vector<std::string>& lines) {
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) text += '\n';
        text += lines[i];
    }
    return text;
}

static double rootPanScale(const Canvas& canvas, const Node& node) {
    (void)node;
    return std::max(0.0001, canvas.viewport_.textScale);
}

// Which corner (if any) of sr is within 1 screen cell of (screenX, screenY).
// A box needs to be at least a few cells across for its corners to be
// distinguishable targets - otherwise top-left and top-right (etc) would
// overlap and grabbing one reliably would be impossible.
static InputHandler::ResizeCorner cornerAt(const ScreenRect& sr, int screenX, int screenY) {
    using RC = InputHandler::ResizeCorner;
    if (!sr.valid()) return RC::None;
    int left = static_cast<int>(std::round(sr.x));
    int top = static_cast<int>(std::round(sr.y));
    int right = static_cast<int>(std::round(sr.x + sr.w)) - 1;
    int bottom = static_cast<int>(std::round(sr.y + sr.h)) - 1;
    if (right - left < 2 || bottom - top < 2) return RC::None;

    auto near = [](int a, int b) { return std::abs(a - b) <= 1; };
    bool atLeft = near(screenX, left), atRight = near(screenX, right);
    bool atTop = near(screenY, top), atBottom = near(screenY, bottom);
    if (atLeft && atTop) return RC::TopLeft;
    if (atRight && atTop) return RC::TopRight;
    if (atLeft && atBottom) return RC::BottomLeft;
    if (atRight && atBottom) return RC::BottomRight;
    return RC::None;
}

// Which connection anchor point (if any) is within 1 screen cell of
// (screenX, screenY). Independently mirrors console_renderer.cpp's
// anchorPoint/canShowAnchors (edge 0=Left,1=Right,2=Bottom; index 0/1/2 =
// quarter/half/three-quarter along that edge) - hit-testing only needs to
// land close, not pixel-exact, same tolerance as corner grabbing.
static bool anchorAt(const ScreenRect& sr, int screenX, int screenY,
                      InputHandler::AnchorEdge& outEdge, int& outIndex) {
    if (!sr.valid()) return false;
    int left = static_cast<int>(std::round(sr.x));
    int top = static_cast<int>(std::round(sr.y));
    int right = static_cast<int>(std::round(sr.x + sr.w)) - 1;
    int bottom = static_cast<int>(std::round(sr.y + sr.h)) - 1;
    int w = right - left + 1, h = bottom - top + 1;
    if (w < 8 || h < 6) return false;

    auto near = [](int a, int b) { return std::abs(a - b) <= 1; };
    for (int edge = 0; edge < 3; edge++) {
        for (int index = 0; index < 3; index++) {
            double frac = (index + 1) / 4.0;
            int px, py;
            if (edge == 0)      { px = left;  py = top + static_cast<int>(std::round(frac * (h - 1))); }
            else if (edge == 1) { px = right; py = top + static_cast<int>(std::round(frac * (h - 1))); }
            else                { px = left + static_cast<int>(std::round(frac * (w - 1))); py = bottom; }

            if (near(screenX, px) && near(screenY, py)) {
                outEdge = static_cast<InputHandler::AnchorEdge>(edge);
                outIndex = index;
                return true;
            }
        }
    }
    return false;
}

// Distance from (px,py) to the segment (x1,y1)-(x2,y2), for hit-testing a
// click against a connection's drawn line.
static double pointSegmentDistance(double px, double py, double x1, double y1, double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1;
    double lenSq = dx * dx + dy * dy;
    double t = lenSq > 0.0 ? ((px - x1) * dx + (py - y1) * dy) / lenSq : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    double cx = x1 + t * dx, cy = y1 + t * dy;
    double ddx = px - cx, ddy = py - cy;
    return std::sqrt(ddx * ddx + ddy * ddy);
}

InputResult InputHandler::process(const InputEvent& event) {
    sidebarNodeUuid_.clear();
    switch (event.type) {
        case InputType::KeyPress:    return handleKeyPress(event);
        case InputType::KeyRelease:  return handleKeyRelease(event);
        case InputType::MouseMove:   return handleMouseMove(event);
        case InputType::MouseDown:   return handleMouseDown(event);
        case InputType::MouseUp:     return handleMouseUp(event);
        case InputType::MouseScroll: return handleMouseScroll(event);
        case InputType::Resize:      return handleResize(event);
        case InputType::Quit:        return InputResult::RequestQuit;
    }
    return InputResult::NotHandled;
}

// ============================================================
// KEYBOARD
// ============================================================
InputResult InputHandler::handleKeyPress(const InputEvent& event) {
    // If editing, handle edit keys first
    if (editingConnection_) {
        handleConnectionEditKey(event);
        return InputResult::Handled;
    }
    if (editingNode_) {
        handleEditKey(event);
        return InputResult::Handled;
    }
    int key = std::tolower(static_cast<unsigned char>(event.key));

    if (event.mods.ctrl) {
        if (key == 'q') return InputResult::RequestQuit;
        if (key == 's') return InputResult::RequestSave;
        if (key == 'n') return InputResult::RequestNew;
        return InputResult::NotHandled;
    }

    // Navigation (pan viewport in LOGICAL coordinates)
    double panAmt = 20.0 * panSpeed_;
    if (key == 'w' || key == 'k') { canvas_.viewport_.centerY += panAmt; canvas_.markCanvasChanged(); return InputResult::Handled; }
    if (key == 's' || key == 'j') { canvas_.viewport_.centerY -= panAmt; canvas_.markCanvasChanged(); return InputResult::Handled; }
    if (key == 'a' || key == 'h') { canvas_.viewport_.centerX -= panAmt; canvas_.markCanvasChanged(); return InputResult::Handled; }
    if (key == 'd' || key == 'l') { canvas_.viewport_.centerX += panAmt; canvas_.markCanvasChanged(); return InputResult::Handled; }

    // Zoom level
    if (key >= '0' && key <= '9') {
        canvas_.viewport_.zoomLevel = key - '0';
        canvas_.markCanvasChanged();
        return InputResult::Handled;
    }
    if (key == '[') {
        canvas_.viewport_.zoomLevel--;
        autoCenterViewportOnRoot();
        canvas_.markCanvasChanged();
        return InputResult::Handled;
    }
    if (key == ']') { canvas_.viewport_.zoomLevel++; canvas_.markCanvasChanged(); return InputResult::Handled; }
    if (key == 'z') { zHeld_ = true; return InputResult::Handled; }

    // Actions
    if (key == 27 || key == '\x1b') { // Escape
        canvas_.selection_.clear();
        for (auto& [_, n] : canvas_.allNodes()) n->state = NodeState::Normal;
        canvas_.selectedConnection_.clear();
        cancelConnection();
        return InputResult::Handled;
    }
    if (key == 'f') { createFrame(); return InputResult::Handled; }
    if (key == 'r') { renameSelected(); return InputResult::Handled; }
    if (key == 'x') {
        if (!canvas_.selectedConnection_.empty()) deleteSelectedConnection();
        else deleteSelected();
        return InputResult::Handled;
    }
    if (key == 'n') { createNodeAtCursor(); return InputResult::Handled; }
    if (key == '+') { canvas_.viewport_.textScale = std::min(1e6, canvas_.viewport_.textScale * 1.2); canvas_.markCanvasChanged(); return InputResult::Handled; }
    if (key == '-') { canvas_.viewport_.textScale = std::max(1e-6, canvas_.viewport_.textScale / 1.2); canvas_.markCanvasChanged(); return InputResult::Handled; }
    if (key == '\t') {
        if (event.mods.shift) demoteSelected(); else promoteSelected();
        return InputResult::Handled;
    }
    return InputResult::NotHandled;
}

InputResult InputHandler::handleKeyRelease(const InputEvent& event) {
    if (std::tolower(event.key) == 'z') zHeld_ = false;
    return InputResult::Handled;
}

// ============================================================
// MOUSE РІР‚вЂќ all coordinates are SCREEN, convert to LOGICAL
// ============================================================
InputResult InputHandler::handleMouseMove(const InputEvent& event) {
    double lx = screenToLogicalX(event.mouseX);
    double ly = screenToLogicalY(event.mouseY);

    if (resizing_.active) {
        updateResize(event.mouseX, event.mouseY);
        return InputResult::Handled;
    }

    if (connectionFrom_) {
        // Dragging a connection also holds the left button down, which
        // would otherwise fall through to viewport panning below - live
        // target feedback instead, no panning while the drag is in progress.
        for (auto& [_, n] : canvas_.allNodes())
            if (n->state == NodeState::Hovered) n->state = NodeState::Normal;

        std::string anchorUuid;
        AnchorEdge anchorEdge;
        int anchorIndex = -1;
        if (findAnchorAtScreen(event.mouseX, event.mouseY, anchorUuid, anchorEdge, anchorIndex)) {
            canvas_.anchorHover_.nodeUuid = anchorUuid;
            canvas_.anchorHover_.edge = static_cast<int>(anchorEdge);
            canvas_.anchorHover_.index = anchorIndex;
        } else {
            canvas_.anchorHover_.nodeUuid.clear();
            canvas_.anchorHover_.edge = -1;
            canvas_.anchorHover_.index = -1;
        }

        auto target = findNodeAtScreen(event.mouseX, event.mouseY);
        if (target && target->uuid != *connectionFrom_ && target->state != NodeState::Selected)
            target->state = NodeState::Hovered;

        return InputResult::Handled;
    }

    if (canvas_.selection_.active) {
        canvas_.selection_.update(lx, ly);
        return InputResult::Handled;
    }

    // Pending click РІвЂ вЂ™ drag Р ВµРЎРѓР В»Р С‘ Р СРЎвЂ№РЎв‚¬РЎРЉ РЎРѓР Т‘Р Р†Р С‘Р Р…РЎС“Р В»Р В°РЎРѓРЎРЉ
    if (pendingClick_.active) {
        int dx = std::abs(event.mouseX - pendingClick_.screenX);
        int dy = std::abs(event.mouseY - pendingClick_.screenY);
        if (dx + dy > 2) {
            pendingClick_.active = false;
            if (editingNode_) {
                commitEdit();
            }
            draggingNodes_ = true;
            dragStates_.clear();
            for (auto& [uuid, n] : canvas_.allNodes()) {
                if (n->state != NodeState::Selected) continue;
                DragState ds;
                auto parent = canvas_.getNode(n->parentId);
                if (parent) {
                    auto pit = screenRects_.find(parent->uuid);
                    // Without the parent's current on-screen size, a pixel
                    // of mouse movement can't be reliably converted into
                    // this node's local units - skip it rather than guess
                    // with a made-up scale that could fling it far away.
                    if (pit == screenRects_.end() || !pit->second.valid()) continue;
                    ds.isChild = true;
                    ds.startRelX = n->rect.x;
                    ds.startRelY = n->rect.y;
                    ds.startScreenX = pendingClick_.screenX;
                    ds.startScreenY = pendingClick_.screenY;
                    ds.parentSrW = pit->second.w;
                    ds.parentSrH = pit->second.h;
                    ds.parentRectW = parent->rect.width;
                    ds.parentRectH = parent->rect.height;
                } else {
                    ds.isChild = false;
                    ds.startRelX = n->rect.x;
                    ds.startRelY = n->rect.y;
                    ds.startScreenX = pendingClick_.screenX;
                    ds.startScreenY = pendingClick_.screenY;
                    ds.screenScaleX = std::max(0.0001, canvas_.viewport_.textScale);
                    ds.screenScaleY = ds.screenScaleX;
                }
                dragStates_[n->uuid] = ds;
            }
        }
        if (pendingClick_.active) {
            return InputResult::Handled;
        }
    }

    if (draggingNodes_) {
        std::set<std::string> moved;

        for (auto& [_, n] : canvas_.allNodes()) {
            if (n->state != NodeState::Selected || moved.count(n->uuid)) continue;
            auto it = dragStates_.find(n->uuid);
            if (it == dragStates_.end()) continue;

            const auto& ds = it->second;

            if (ds.isChild) {
                double dsx = event.mouseX - ds.startScreenX;
                double dsy = event.mouseY - ds.startScreenY;
                double dx = ds.parentSrW > 0 ? dsx * ds.parentRectW / ds.parentSrW : 0;
                double dy = ds.parentSrH > 0 ? dsy * ds.parentRectH / ds.parentSrH : 0;
                n->rect.x = ds.startRelX + dx;
                n->rect.y = ds.startRelY + dy;
            } else {
                double dsx = event.mouseX - ds.startScreenX;
                double dsy = event.mouseY - ds.startScreenY;
                double dx = dsx / ds.screenScaleX;
                double dy = dsy / ds.screenScaleY;
                n->rect.x = ds.startRelX + dx;
                n->rect.y = ds.startRelY + dy;
            }

            moved.insert(n->uuid);
            auto descendants = canvas_.getDescendants(n->uuid);
            for (auto& d : descendants) moved.insert(d->uuid);

            canvas_.expandParentToFit(n->uuid, 2.0);
        }
        return InputResult::Handled;
    }

    if (mouseDown_.left && !draggingNodes_) {
        // Pan: use screen delta directly for 1:1 movement
        int dsx = event.mouseX - lastScreenX_;
        int dsy = event.mouseY - lastScreenY_;
        canvas_.viewport_.centerX -= dsx / panScreenScale_;
        canvas_.viewport_.centerY -= dsy / panScreenScale_;
        canvas_.markCanvasChanged();
        lastScreenX_ = event.mouseX;
        lastScreenY_ = event.mouseY;
        return InputResult::Handled;
    }
    // Hover РІР‚вЂќ use screen rects for fractal hit testing
    for (auto& [_, n] : canvas_.allNodes())
        if (n->state == NodeState::Hovered) n->state = NodeState::Normal;

    std::string cornerUuid;
    ResizeCorner hoverCorner = findResizeCornerAtScreen(event.mouseX, event.mouseY, cornerUuid);
    canvas_.cornerHover_.nodeUuid = (hoverCorner != ResizeCorner::None) ? cornerUuid : std::string();
    canvas_.cornerHover_.corner = static_cast<int>(hoverCorner);

    std::string anchorUuid;
    AnchorEdge anchorEdge;
    int anchorIndex = -1;
    if (findAnchorAtScreen(event.mouseX, event.mouseY, anchorUuid, anchorEdge, anchorIndex)) {
        canvas_.anchorHover_.nodeUuid = anchorUuid;
        canvas_.anchorHover_.edge = static_cast<int>(anchorEdge);
        canvas_.anchorHover_.index = anchorIndex;
    } else {
        canvas_.anchorHover_.nodeUuid.clear();
        canvas_.anchorHover_.edge = -1;
        canvas_.anchorHover_.index = -1;
    }

    auto node = findNodeAtScreen(event.mouseX, event.mouseY);
    if (node && node->state != NodeState::Selected) node->state = NodeState::Hovered;
    return InputResult::Handled;
}

InputResult InputHandler::handleMouseDown(const InputEvent& event) {
    double lx = screenToLogicalX(event.mouseX);
    double ly = screenToLogicalY(event.mouseY);

    // Check sidebar area first (left-click only)
    if (event.button == 0 && event.mouseX < sidebarWidth_) {
        for (const auto& [uuid, sr] : sidebarNodeRects_) {
            if (sr.valid() &&
                event.mouseX >= static_cast<int>(sr.x) &&
                event.mouseX <= static_cast<int>(sr.x + sr.w) &&
                event.mouseY >= static_cast<int>(sr.y) &&
                event.mouseY <= static_cast<int>(sr.y + sr.h)) {
                sidebarNodeUuid_ = uuid;
                return InputResult::SidebarNodeClick;
            }
        }
    }

    if (event.button == 0) { // Left
        auto node = findNodeAtScreen(event.mouseX, event.mouseY);

        // Double-click detection. This must be checked before the corner-grab
        // test below: an already-selected node's corners can sit close to
        // where a double-click naturally lands (e.g. near the header edge),
        // and letting corner-resize win there would hijack the second click
        // of what the user meant as a double-click-to-rename.
        auto nowMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        bool isDoubleClick = (nowMs - lastClickTime_ < 350) &&
                              (std::abs(event.mouseX - lastClickX_) < 2) &&
                              (std::abs(event.mouseY - lastClickY_) < 2) &&
                              node && node->uuid == lastClickedNode_.value_or("");

        if (isDoubleClick && node) {
            // Double click: Р Р†РЎвЂ№Р В±РЎР‚Р В°РЎвЂљРЎРЉ, РЎР‚Р ВµР Т‘Р В°Р С”РЎвЂљР С‘РЎР‚Р С•Р Р†Р В°РЎвЂљРЎРЉ Р С‘Р СРЎРЏ Р С‘Р В»Р С‘ РЎвЂљР ВµР С”РЎРѓРЎвЂљ Р С—Р С• Р С—Р С•Р В·Р С‘РЎвЂ Р С‘Р С‘ Р С”Р В»Р С‘Р С”Р В°
            selectNode(node->uuid, event.mods.shift);
            auto pit = screenRects_.find(node->uuid);
            if (pit != screenRects_.end()) {
                bool onHeader = (event.mouseY == static_cast<int>(pit->second.y));
                if (onHeader) startEditingName(node->uuid);
                else startEditing(node->uuid);
            }
            pendingClick_.active = true;
            pendingClick_.screenX = event.mouseX;
            pendingClick_.screenY = event.mouseY;
            pendingClick_.nodeUuid = node->uuid;
            lastClickTime_ = 0;
            mouseDown_.left = true;
            lastScreenX_ = event.mouseX;
            lastScreenY_ = event.mouseY;
            lastClickX_ = event.mouseX;
            lastClickY_ = event.mouseY;
            lastClickTime_ = nowMs;
            lastClickedNode_ = node ? std::optional<std::string>(node->uuid) : std::nullopt;
            return InputResult::Handled;
        }

        // Р вЂўРЎРѓР В»Р С‘ РЎР‚Р ВµР Т‘Р В°Р С”РЎвЂљР С‘РЎР‚РЎС“Р ВµР С Р С‘ Р С”Р В»Р С‘Р С”Р В°Р ВµР С РІР‚вЂќ РЎРѓР С•РЎвЂ¦РЎР‚Р В°Р Р…РЎРЏР ВµР С РЎвЂљР ВµР С”РЎС“РЎвЂ°Р ВµР Вµ РЎР‚Р ВµР Т‘Р В°Р С”РЎвЂљР С‘РЎР‚Р С•Р Р†Р В°Р Р…Р С‘Р Вµ
        // Grabbing a corner of an already-selected node resizes it instead
        // of the normal click/select/move handling below.
        std::string resizeUuid;
        ResizeCorner corner = findResizeCornerAtScreen(event.mouseX, event.mouseY, resizeUuid);
        if (corner != ResizeCorner::None) {
            if (editingNode_) commitEdit();
            pendingClick_.active = false;
            beginResize(resizeUuid, corner, event.mouseX, event.mouseY);
            if (resizing_.active) {
                mouseDown_.left = true;
                lastScreenX_ = event.mouseX;
                lastScreenY_ = event.mouseY;
                return InputResult::Handled;
            }
            // beginResize declined (e.g. parent's screen extent isn't known
            // right now) - fall through to normal click handling below.
        }

        // Grabbing a connection anchor starts a drag-to-connect, same
        // mechanism as Ctrl+click but without needing the modifier or a
        // prior selection - release over another node to link them.
        std::string anchorUuid;
        AnchorEdge anchorEdge;
        int anchorIndex = -1;
        if (findAnchorAtScreen(event.mouseX, event.mouseY, anchorUuid, anchorEdge, anchorIndex)) {
            if (editingNode_) commitEdit();
            pendingClick_.active = false;
            startConnection(anchorUuid, static_cast<int>(anchorEdge), anchorIndex);
            if (auto n = canvas_.getNode(anchorUuid)) n->state = NodeState::InConnection;
            mouseDown_.left = true;
            lastScreenX_ = event.mouseX;
            lastScreenY_ = event.mouseY;
            return InputResult::Handled;
        }

        if (editingNode_) {
            commitEdit();
        }

        // Single click
        canvas_.selectedConnection_.clear();
        if (event.mods.ctrl && node) {
            startConnection(node->uuid);
            node->state = NodeState::InConnection;
        } else if (node) {
            if (node->state == NodeState::Selected) {
                // Pending click: Р ВµРЎРѓР В»Р С‘ Р СРЎвЂ№РЎв‚¬РЎРЉ Р Р…Р Вµ РЎРѓР Т‘Р Р†Р С‘Р Р…Р ВµРЎвЂљРЎРѓРЎРЏ РІР‚вЂќ Р С”Р В»Р С‘Р С” (edit), Р ВµРЎРѓР В»Р С‘ РЎРѓР Т‘Р Р†Р С‘Р Р…Р ВµРЎвЂљРЎРѓРЎРЏ РІР‚вЂќ drag (move)
                pendingClick_.active = true;
                pendingClick_.screenX = event.mouseX;
                pendingClick_.screenY = event.mouseY;
                pendingClick_.nodeUuid = node->uuid;
            } else {
                // Р С™Р В»Р С‘Р С” Р Р…Р В° Р Р…Р ВµР Р†РЎвЂ№Р В±РЎР‚Р В°Р Р…Р Р…РЎС“РЎР‹: РЎРѓР Р…РЎРЏРЎвЂљРЎРЉ Р Р†РЎвЂ№Р Т‘Р ВµР В»Р ВµР Р…Р С‘Р Вµ Р С‘ РЎР‚Р ВµР Т‘Р В°Р С”РЎвЂљР С‘РЎР‚Р С•Р Р†Р В°Р Р…Р С‘Р Вµ
                if (editingNode_) commitEdit();
                for (auto& [_, n] : canvas_.allNodes())
                    if (n->state == NodeState::Selected) n->state = NodeState::Normal;
            }
        } else {
            // Р С™Р В»Р С‘Р С” Р Р† Р С—РЎС“РЎРѓРЎвЂљР С•РЎвЂљРЎС“: РЎРѓР Р…РЎРЏРЎвЂљРЎРЉ Р Р†РЎвЂ№Р Т‘Р ВµР В»Р ВµР Р…Р С‘Р Вµ Р С‘ РЎР‚Р ВµР Т‘Р В°Р С”РЎвЂљР С‘РЎР‚Р С•Р Р†Р В°Р Р…Р С‘Р Вµ
            if (editingNode_) commitEdit();
            for (auto& [_, n] : canvas_.allNodes())
                if (n->state == NodeState::Selected) n->state = NodeState::Normal;

            std::string connUuid = findConnectionAtScreen(event.mouseX, event.mouseY);
            if (!connUuid.empty()) {
                bool isConnDoubleClick = (nowMs - lastConnClickTime_ < 350) &&
                                          (std::abs(event.mouseX - lastConnClickX_) < 2) &&
                                          (std::abs(event.mouseY - lastConnClickY_) < 2) &&
                                          lastClickedConnection_ && *lastClickedConnection_ == connUuid;
                selectConnection(connUuid);
                if (isConnDoubleClick) {
                    startEditingConnection(connUuid, event.mouseX, event.mouseY);
                    lastConnClickTime_ = 0;
                } else {
                    lastConnClickTime_ = nowMs;
                }
                lastConnClickX_ = event.mouseX;
                lastConnClickY_ = event.mouseY;
                lastClickedConnection_ = connUuid;
            } else {
                lastClickedConnection_.reset();
            }
        }

        mouseDown_.left = true;
        lastLogicalX_ = lx;
        lastLogicalY_ = ly;
        lastScreenX_ = event.mouseX;
        lastScreenY_ = event.mouseY;
        panScreenScale_ = panScaleAtScreen(event.mouseX, event.mouseY);

        lastClickX_ = event.mouseX;
        lastClickY_ = event.mouseY;
        lastClickTime_ = nowMs;
        lastClickedNode_ = node ? std::optional<std::string>(node->uuid) : std::nullopt;
    } else if (event.button == 1) { // Right РІР‚вЂќ area select
        if (editingNode_) commitEdit();
        canvas_.selection_.begin(lx, ly);
        mouseDown_.right = true;
    }
    return InputResult::Handled;
}

InputResult InputHandler::handleMouseUp(const InputEvent& event) {
    if (event.button == 0 && resizing_.active) {
        endResize();
        mouseDown_.left = false;
        return InputResult::Handled;
    }

    if (event.button == 0) {
        // Pending click РІвЂ вЂ™ edit (Р ВµРЎРѓР В»Р С‘ Р Р…Р Вµ Р С”Р С•Р Р…Р Р†Р ВµРЎР‚РЎвЂљР С‘РЎР‚Р С•Р Р†Р В°Р В»РЎРѓРЎРЏ Р Р† drag)
        if (pendingClick_.active) {
            pendingClick_.active = false;
            auto node = canvas_.getNode(pendingClick_.nodeUuid);
            if (node) {
                auto pit = screenRects_.find(node->uuid);
                if (pit != screenRects_.end()) {
                    bool onHeader = (event.mouseY == static_cast<int>(pit->second.y));
                    if (onHeader) startEditingName(node->uuid);
                    else startEditing(node->uuid);
                }
            }
            mouseDown_.left = false;
            return InputResult::Handled;
        }

        // Р СџРЎР‚Р С‘ Р С•РЎвЂљР С—РЎС“РЎРѓР С”Р В°Р Р…Р С‘Р С‘ РІР‚вЂќ РЎвЂћР С‘Р Р…Р В°Р В»РЎРЉР Р…Р С•Р Вµ РЎР‚Р В°Р В·РЎР‚Р ВµРЎв‚¬Р ВµР Р…Р С‘Р Вµ Р Р†РЎРѓР ВµРЎвЂ¦ Р С—Р ВµРЎР‚Р ВµРЎРѓР ВµРЎвЂЎР ВµР Р…Р С‘Р в„– Р Р† РЎР‚Р С•Р Т‘Р С‘РЎвЂљР ВµР В»РЎРЏРЎвЂ¦
        // Р РЋР С•Р В±Р С‘РЎР‚Р В°Р ВµР С РЎС“Р Р…Р С‘Р С”Р В°Р В»РЎРЉР Р…РЎвЂ№РЎвЂ¦ РЎР‚Р С•Р Т‘Р С‘РЎвЂљР ВµР В»Р ВµР в„– Р Р†РЎвЂ№Р В±РЎР‚Р В°Р Р…Р Р…РЎвЂ№РЎвЂ¦ Р Р…Р С•Р Т‘
        std::set<std::string> parents;
        for (auto& [_, n] : canvas_.allNodes()) {
            if (n->state == NodeState::Selected && !n->parentId.empty()) {
                parents.insert(n->parentId);
            }
        }
        for (const auto& pId : parents) {
            canvas_.resolveOverlaps(pId);
        }
        for (auto& [_, n] : canvas_.allNodes()) {
            if (n->state == NodeState::Selected) {
                canvas_.markChanged(n->uuid);
            }
        }

        draggingNodes_ = false;
        dragStates_.clear();
    }

    if (event.button == 0 && connectionFrom_) {
        auto target = findNodeAtScreen(event.mouseX, event.mouseY);
        if (target && target->uuid != *connectionFrom_) {
            std::string toUuid;
            AnchorEdge toEdge;
            int toIndex = -1;
            bool onAnchor = findAnchorAtScreen(event.mouseX, event.mouseY, toUuid, toEdge, toIndex) &&
                             toUuid == target->uuid;

            std::string connUuid = canvas_.createConnection(*connectionFrom_, target->uuid, "", ConnType::Arrow);
            if (auto conn = canvas_.getConnection(connUuid)) {
                conn->fromAnchorEdge = connectionFromAnchorEdge_;
                conn->fromAnchorIndex = connectionFromAnchorIndex_;
                if (onAnchor) {
                    conn->toAnchorEdge = static_cast<int>(toEdge);
                    conn->toAnchorIndex = toIndex;
                }
            }
        }
        cancelConnection();
    }
    if (event.button == 1 && canvas_.selection_.active) {
        selectArea(canvas_.selection_.left(), canvas_.selection_.top(),
                    canvas_.selection_.right(), canvas_.selection_.bottom());
        canvas_.selection_.clear();
    }
    mouseDown_.left = false;
    mouseDown_.right = false;
    return InputResult::Handled;
}

  InputResult InputHandler::handleMouseScroll(const InputEvent& event) {
    double vp = canvas_.viewport_.textScale;
    double factor = (event.scrollY > 0) ? 1.15 : 1.0 / 1.15;
    double newScale = std::max(1e-6, std::min(1e6, vp * factor));

    if (newScale != vp) {
        // Zoom toward cursor: keep logical point under mouse unchanged
        double cx = canvasArea_.centerX();
        double cy = canvasArea_.centerY();
        double lx = canvas_.viewport_.centerX + (event.mouseX - cx) / vp;
        double ly = canvas_.viewport_.centerY + (event.mouseY - cy) / vp;
        canvas_.viewport_.centerX = lx - (event.mouseX - cx) / newScale;
        canvas_.viewport_.centerY = ly - (event.mouseY - cy) / newScale;
        canvas_.viewport_.textScale = newScale;
        canvas_.markCanvasChanged();
    }
    return InputResult::Handled;
}

InputResult InputHandler::handleResize(const InputEvent& event) {
    setViewSize(event.width, event.height);
    return InputResult::Handled;
}

// ============================================================
// ACTIONS
// ============================================================
void InputHandler::selectNode(const std::string& uuid, bool add) {
    if (!add) for (auto& [_, n] : canvas_.allNodes())
        if (n->state == NodeState::Selected) n->state = NodeState::Normal;
    auto node = canvas_.getNode(uuid);
    if (node) node->state = NodeState::Selected;
}

void InputHandler::selectArea(double x1, double y1, double x2, double y2) {
    double mnX = std::min(x1,x2), mxX = std::max(x1,x2);
    double mnY = std::min(y1,y2), mxY = std::max(y1,y2);
    for (auto& [uuid, n] : canvas_.allNodes()) {
        Rect world = canvas_.computeWorldRect(uuid);
        bool in = (world.x + world.width > mnX && world.x < mxX &&
                    world.y + world.height > mnY && world.y < mxY);
        n->state = in ? NodeState::Selected : (n->state==NodeState::Selected ? NodeState::Normal : n->state);
    }
}

void InputHandler::createFrame() {
    std::vector<std::string> sel;
    for (auto& [u,n] : canvas_.allNodes()) if (n->state==NodeState::Selected) sel.push_back(u);
    if (sel.size()>1) canvas_.createFrame(sel, "Frame");
}

void InputHandler::deleteSelected() {
    std::vector<std::string> selected;
    for (const auto& [u, n] : canvas_.allNodes()) {
        if (n->state == NodeState::Selected) selected.push_back(u);
    }
    for (const auto& u : selected) {
        canvas_.removeNode(u);
    }
}

void InputHandler::promoteSelected() {
    for (auto& [u,n] : canvas_.allNodes()) if (n->state==NodeState::Selected) { canvas_.promoteNode(u); break; }
}

void InputHandler::demoteSelected() {
    for (auto& [u,n] : canvas_.allNodes()) if (n->state==NodeState::Selected) { canvas_.setNodeLevel(u, n->zoomLevel+1); break; }
}

void InputHandler::createNodeAtCursor() {
    // Р вЂўРЎРѓР В»Р С‘ Р ВµРЎРѓРЎвЂљРЎРЉ Р Р†РЎвЂ№Р В±РЎР‚Р В°Р Р…Р Р…РЎвЂ№Р в„– РЎР‚Р С•Р Т‘Р С‘РЎвЂљР ВµР В»РЎРЉ РІР‚вЂќ РЎРѓР С•Р В·Р Т‘Р В°РЎвЂР С РЎР‚Р ВµР В±РЎвЂР Р…Р С”Р В° Р Р†Р Р…РЎС“РЎвЂљРЎР‚Р С‘
    std::string parentId;
    for (auto& [_, n] : canvas_.allNodes()) {
        if (n->state == NodeState::Selected) { parentId = n->uuid; break; }
    }
    if (parentId.empty()) {
        for (auto& [_, n] : canvas_.allNodes()) {
            if (n->state == NodeState::Hovered) { parentId = n->uuid; break; }
        }
    }
    Rect r;
    int level = 0;
    if (!parentId.empty()) {
        auto parent = canvas_.getNode(parentId);
        if (parent) {
            // Р РЋР С•Р В·Р Т‘Р В°РЎвЂР С РЎР‚Р ВµР В±РЎвЂР Р…Р С”Р В° Р Р† РЎвЂ Р ВµР Р…РЎвЂљРЎР‚Р Вµ РЎР‚Р С•Р Т‘Р С‘РЎвЂљР ВµР В»РЎРЏ (Р С•РЎвЂљР Р…Р С•РЎРѓР С‘РЎвЂљР ВµР В»РЎРЉР Р…РЎвЂ№Р Вµ Р С”Р С•Р С•РЎР‚Р Т‘Р С‘Р Р…Р В°РЎвЂљРЎвЂ№)
            r.x = parent->rect.width / 2.0 - 10.0;
            r.y = parent->rect.height / 2.0 - 2.0;
            r.width = 20.0;
            r.height = 4.0;
            level = parent->zoomLevel + 1;
        } else {
            parentId.clear();
        }
    }
    if (parentId.empty()) {
        // Create root node at canvas area center
        double cx = screenToLogicalX(canvasArea_.centerX());
        double cy = screenToLogicalY(canvasArea_.centerY());
        r.x = cx - 10.0;
        r.y = cy - 2.0;
        r.width = 20.0;
        r.height = 4.0;
        level = 0;
    }
    std::string uuid = canvas_.createNode("New Node", r, "New Node", parentId, level);
    if (!uuid.empty()) {
        auto node = canvas_.getNode(uuid);
        if (node) {
            node->autoResize();
            canvas_.markChanged(uuid);
            selectNode(uuid);
            startEditing(uuid);
            if (!parentId.empty()) {
                canvas_.resolveOverlaps(parentId);
            }
        }
    }
}

void InputHandler::renameSelected() {
    for (auto& [u, n] : canvas_.allNodes()) {
        if (n->state == NodeState::Selected) {
            startEditingName(u);
            break;
        }
    }
}

void InputHandler::autoCenterViewportOnRoot() {
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    bool hasNodes = false;
    for (const auto& [u, n] : canvas_.allNodes()) {
        if (n->parentId.empty()) {
            Rect world = canvas_.computeWorldRect(u);
            minX = std::min(minX, world.x);
            minY = std::min(minY, world.y);
            maxX = std::max(maxX, world.x + world.width);
            maxY = std::max(maxY, world.y + world.height);
            hasNodes = true;
        }
    }
    if (hasNodes) {
        canvas_.viewport_.centerX = (minX + maxX) / 2.0;
        canvas_.viewport_.centerY = (minY + maxY) / 2.0;
    }
}

void InputHandler::startConnection(const std::string& f, int anchorEdge, int anchorIndex) {
    connectionFrom_ = f;
    connectionFromAnchorEdge_ = anchorEdge;
    connectionFromAnchorIndex_ = anchorIndex;
}
void InputHandler::cancelConnection() {
    if (connectionFrom_) {
        auto n = canvas_.getNode(*connectionFrom_);
        if (n && n->state==NodeState::InConnection) n->state = NodeState::Normal;
        connectionFrom_.reset();
        connectionFromAnchorEdge_ = -1;
        connectionFromAnchorIndex_ = -1;
    }
}

// ============================================================
// INLINE TEXT EDITING
// ============================================================
void InputHandler::startEditing(const std::string& nodeUuid) {
    editingName_ = false;
    auto node = canvas_.getNode(nodeUuid);
    if (!node) return;
    editingNode_ = nodeUuid;
    editText_ = node->text;
    editNodeRect_ = node->rect;
    // Set cursor at end of text (character count, not byte count)
    std::vector<std::string> tmpLines;
    { std::istringstream iss(node->text); std::string l; while (std::getline(iss, l)) tmpLines.push_back(l); }
    editCursorRow_ = std::max(0, static_cast<int>(tmpLines.size()) - 1);
    editCursorCol_ = tmpLines.empty() ? 0 : utf8CharCount(tmpLines.back());
    canvas_.editOverlay_.active = true;
    canvas_.editOverlay_.editingName = false;
    canvas_.editOverlay_.nodeUuid = nodeUuid;
    canvas_.editOverlay_.text = editText_;
    canvas_.editOverlay_.nodeRect = node->rect;
    canvas_.editOverlay_.cursorCol = editCursorCol_;
    canvas_.editOverlay_.cursorRow = editCursorRow_;
}

 void InputHandler::startEditingName(const std::string& nodeUuid) {
    editingName_ = true;
    auto node = canvas_.getNode(nodeUuid);
    if (!node) return;
    editingNode_ = nodeUuid;
    editText_ = node->name;
    editCursorCol_ = utf8CharCount(editText_);
    editCursorRow_ = 0;
    editNodeRect_ = node->rect;
    canvas_.editOverlay_.active = true;
    canvas_.editOverlay_.editingName = true;
    canvas_.editOverlay_.nodeUuid = nodeUuid;
    canvas_.editOverlay_.nodeRect = node->rect;
    canvas_.editOverlay_.text = node->name;
    canvas_.editOverlay_.cursorCol = editCursorCol_;
    canvas_.editOverlay_.cursorRow = editCursorRow_;
}

void InputHandler::handleEditKey(const InputEvent& event) {
    {
    auto syncOverlay = [this]() {
        auto lines = splitEditorLines(editText_);
        if (lines.empty()) lines.push_back("");
        editCursorRow_ = std::max(0, std::min(editCursorRow_, static_cast<int>(lines.size()) - 1));
        editCursorCol_ = std::max(0, std::min(editCursorCol_, utf8CharCount(lines[editCursorRow_])));

        canvas_.editOverlay_.text = editText_;
        canvas_.editOverlay_.editingName = editingName_;
        canvas_.editOverlay_.cursorCol = editCursorCol_;
        canvas_.editOverlay_.cursorRow = editCursorRow_;

        auto node = canvas_.getNode(*editingNode_);
        if (node) {
            int maxLen = 0;
            for (const auto& line : lines) {
                maxLen = std::max(maxLen, utf8CharCount(line));
            }
            double neededW = static_cast<double>(maxLen + 4);
            node->rect.width = std::max(node->rect.width, neededW);
            if (!editingName_) {
                node->rect.height = std::max(node->rect.height, static_cast<double>(lines.size() + 1) * 1.2);
            }
            canvas_.expandParentToFit(*editingNode_, 2.0);
            canvas_.editOverlay_.nodeRect = node->rect;
        }
    };

    auto lines = splitEditorLines(editText_);
    if (lines.empty()) lines.push_back("");

    auto clampCursor = [this, &lines]() {
        editCursorRow_ = std::max(0, std::min(editCursorRow_, static_cast<int>(lines.size()) - 1));
        editCursorCol_ = std::max(0, std::min(editCursorCol_, utf8CharCount(lines[editCursorRow_])));
    };
    auto applyLines = [&]() {
        clampCursor();
        editText_ = joinEditorLines(lines);
        syncOverlay();
    };

    clampCursor();
    int key = event.key;

    if (key == 27 || key == '\x1b') { cancelEdit(); return; }
    if (event.mods.ctrl && key == '\r') { commitEdit(); return; }

    if (key == '\r') {
        if (editingName_) {
            commitEdit();
            return;
        }

        std::string& line = lines[editCursorRow_];
        int bytePos = utf8CharToByte(line, editCursorCol_);
        std::string after = line.substr(bytePos);
        line.erase(bytePos);
        lines.insert(lines.begin() + editCursorRow_ + 1, after);
        editCursorRow_++;
        editCursorCol_ = 0;
        applyLines();
        return;
    }

    if (key == 8 || key == 0x08) {
        if (editCursorCol_ > 0) {
            std::string& line = lines[editCursorRow_];
            int bytePos = utf8CharToByte(line, editCursorCol_ - 1);
            line.erase(bytePos, utf8CharLen(line, bytePos));
            editCursorCol_--;
        } else if (!editingName_ && editCursorRow_ > 0) {
            editCursorCol_ = utf8CharCount(lines[editCursorRow_ - 1]);
            lines[editCursorRow_ - 1] += lines[editCursorRow_];
            lines.erase(lines.begin() + editCursorRow_);
            editCursorRow_--;
        }
        applyLines();
        return;
    }

    if (key == 46) {
        std::string& line = lines[editCursorRow_];
        int charCount = utf8CharCount(line);
        if (editCursorCol_ < charCount) {
            int bytePos = utf8CharToByte(line, editCursorCol_);
            line.erase(bytePos, utf8CharLen(line, bytePos));
        } else if (!editingName_ && editCursorRow_ < static_cast<int>(lines.size()) - 1) {
            line += lines[editCursorRow_ + 1];
            lines.erase(lines.begin() + editCursorRow_ + 1);
        }
        applyLines();
        return;
    }

    if (key == 37) {
        if (editCursorCol_ > 0) editCursorCol_--;
        else if (!editingName_ && editCursorRow_ > 0) {
            editCursorRow_--;
            editCursorCol_ = utf8CharCount(lines[editCursorRow_]);
        }
        syncOverlay();
        return;
    }
    if (key == 39) {
        if (editCursorCol_ < utf8CharCount(lines[editCursorRow_])) {
            editCursorCol_++;
        } else if (!editingName_ && editCursorRow_ < static_cast<int>(lines.size()) - 1) {
            editCursorRow_++;
            editCursorCol_ = 0;
        }
        syncOverlay();
        return;
    }
    if (key == 38) {
        if (!editingName_ && editCursorRow_ > 0) {
            editCursorRow_--;
            editCursorCol_ = std::min(editCursorCol_, utf8CharCount(lines[editCursorRow_]));
        }
        syncOverlay();
        return;
    }
    if (key == 40) {
        if (!editingName_ && editCursorRow_ < static_cast<int>(lines.size()) - 1) {
            editCursorRow_++;
            editCursorCol_ = std::min(editCursorCol_, utf8CharCount(lines[editCursorRow_]));
        }
        syncOverlay();
        return;
    }

    wchar_t ch = event.ch;
    if (!ch || ch < 0x20) return;

    std::string utf8ch;
    if (ch < 0x80) {
        utf8ch += static_cast<char>(ch);
    } else if (ch < 0x800) {
        utf8ch += static_cast<char>(0xC0 | (ch >> 6));
        utf8ch += static_cast<char>(0x80 | (ch & 0x3F));
    } else {
        utf8ch += static_cast<char>(0xE0 | (ch >> 12));
        utf8ch += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        utf8ch += static_cast<char>(0x80 | (ch & 0x3F));
    }

    std::string& line = lines[editCursorRow_];
    int bytePos = utf8CharToByte(line, editCursorCol_);
    line.insert(bytePos, utf8ch);
    editCursorCol_++;
    applyLines();
    return;
    }
}

void InputHandler::commitEdit() {
    if (!editingNode_) return;
    auto node = canvas_.getNode(*editingNode_);
    if (!node) { editingNode_.reset(); return; }
    if (editingName_) {
        node->name = editText_;
    } else {
        node->text = editText_;
        node->autoResize();
    }
    canvas_.markChanged(*editingNode_);
    editingNode_.reset();
    canvas_.editOverlay_.active = false;
}

void InputHandler::cancelEdit() {
    editingNode_.reset();
    canvas_.editOverlay_.active = false;
}

// ============================================================
// HIT TESTING (screen coordinates РІвЂ вЂ™ node)
// ============================================================
std::shared_ptr<Node> InputHandler::findNodeAtScreen(int screenX, int screenY) const {
    // Use cached screen rects for fractal layout hit testing
    std::shared_ptr<Node> topmost;
    int topmostLevel = -1000;

    for (const auto& [uuid, sr] : screenRects_) {
        if (sr.valid() &&
            screenX >= static_cast<int>(sr.x) && screenX <= static_cast<int>(sr.x + sr.w) &&
            screenY >= static_cast<int>(sr.y) && screenY <= static_cast<int>(sr.y + sr.h)) {
            auto node = canvas_.getNode(uuid);
            if (node && node->zoomLevel > topmostLevel) {
                topmost = node;
                topmostLevel = node->zoomLevel;
            }
        }
    }
    return topmost;
}

InputHandler::ResizeCorner InputHandler::findResizeCornerAtScreen(int screenX, int screenY, std::string& outUuid) const {
    for (const auto& [uuid, sr] : screenRects_) {
        auto node = canvas_.getNode(uuid);
        if (!node || node->state != NodeState::Selected) continue;
        ResizeCorner c = cornerAt(sr, screenX, screenY);
        if (c != ResizeCorner::None) {
            outUuid = uuid;
            return c;
        }
    }
    return ResizeCorner::None;
}

bool InputHandler::findAnchorAtScreen(int screenX, int screenY, std::string& outUuid,
                                       AnchorEdge& outEdge, int& outIndex) const {
    // Unlike resize corners, anchors work on any node - you should be able
    // to start a connection without first selecting either endpoint.
    for (const auto& [uuid, sr] : screenRects_) {
        if (anchorAt(sr, screenX, screenY, outEdge, outIndex)) {
            outUuid = uuid;
            return true;
        }
    }
    return false;
}

std::string InputHandler::findConnectionAtScreen(int screenX, int screenY) const {
    const double hitRadius = 1.0;
    for (const auto& [uuid, conn] : canvas_.allConnections()) {
        auto fromIt = screenRects_.find(conn->fromUuid);
        auto toIt = screenRects_.find(conn->toUuid);
        if (fromIt == screenRects_.end() || toIt == screenRects_.end()) continue;
        if (!fromIt->second.valid() || !toIt->second.valid()) continue;

        double dist = pointSegmentDistance(
            screenX, screenY,
            fromIt->second.centerX(), fromIt->second.centerY(),
            toIt->second.centerX(), toIt->second.centerY());
        if (dist <= hitRadius) return uuid;
    }
    return {};
}

void InputHandler::selectConnection(const std::string& uuid) {
    for (auto& [_, n] : canvas_.allNodes())
        if (n->state == NodeState::Selected) n->state = NodeState::Normal;
    canvas_.selectedConnection_ = uuid;
}

void InputHandler::deleteSelectedConnection() {
    if (canvas_.selectedConnection_.empty()) return;
    canvas_.removeConnection(canvas_.selectedConnection_);
    canvas_.selectedConnection_.clear();
}

void InputHandler::startEditingConnection(const std::string& connUuid, int screenX, int screenY) {
    auto conn = canvas_.getConnection(connUuid);
    if (!conn) return;
    editingConnection_ = connUuid;
    editConnText_ = conn->name;
    editConnCursorCol_ = utf8CharCount(editConnText_);
    canvas_.connEditOverlay_.active = true;
    canvas_.connEditOverlay_.connUuid = connUuid;
    canvas_.connEditOverlay_.text = editConnText_;
    canvas_.connEditOverlay_.cursorCol = editConnCursorCol_;
    canvas_.connEditOverlay_.screenX = screenX;
    canvas_.connEditOverlay_.screenY = screenY;
}

void InputHandler::commitConnectionEdit() {
    if (!editingConnection_) return;
    auto conn = canvas_.getConnection(*editingConnection_);
    if (conn) conn->name = editConnText_;
    editingConnection_.reset();
    canvas_.connEditOverlay_.active = false;
    canvas_.markCanvasChanged();
}

void InputHandler::cancelConnectionEdit() {
    editingConnection_.reset();
    canvas_.connEditOverlay_.active = false;
}

void InputHandler::handleConnectionEditKey(const InputEvent& event) {
    int key = event.key;
    if (key == 27 || key == '\x1b') { cancelConnectionEdit(); return; }
    if (key == '\r') { commitConnectionEdit(); return; }

    if (key == 8 || key == 0x08) {
        if (editConnCursorCol_ > 0) {
            int bytePos = utf8CharToByte(editConnText_, editConnCursorCol_ - 1);
            editConnText_.erase(bytePos, utf8CharLen(editConnText_, bytePos));
            editConnCursorCol_--;
        }
    } else if (key == 46) { // Delete
        int charCount = utf8CharCount(editConnText_);
        if (editConnCursorCol_ < charCount) {
            int bytePos = utf8CharToByte(editConnText_, editConnCursorCol_);
            editConnText_.erase(bytePos, utf8CharLen(editConnText_, bytePos));
        }
    } else if (key == 37) { // Left
        if (editConnCursorCol_ > 0) editConnCursorCol_--;
    } else if (key == 39) { // Right
        if (editConnCursorCol_ < utf8CharCount(editConnText_)) editConnCursorCol_++;
    } else {
        wchar_t ch = event.ch;
        if (!ch || ch < 0x20) return;
        std::string utf8ch;
        if (ch < 0x80) {
            utf8ch += static_cast<char>(ch);
        } else if (ch < 0x800) {
            utf8ch += static_cast<char>(0xC0 | (ch >> 6));
            utf8ch += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            utf8ch += static_cast<char>(0xE0 | (ch >> 12));
            utf8ch += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            utf8ch += static_cast<char>(0x80 | (ch & 0x3F));
        }
        int bytePos = utf8CharToByte(editConnText_, editConnCursorCol_);
        editConnText_.insert(bytePos, utf8ch);
        editConnCursorCol_++;
    }

    canvas_.connEditOverlay_.text = editConnText_;
    canvas_.connEditOverlay_.cursorCol = editConnCursorCol_;
}

void InputHandler::beginResize(const std::string& uuid, ResizeCorner corner, int screenX, int screenY) {
    resizing_ = ResizeState{};
    auto node = canvas_.getNode(uuid);
    if (!node) return;

    auto parent = canvas_.getNode(node->parentId);
    double parentSrW = 1, parentSrH = 1;
    if (parent) {
        auto pit = screenRects_.find(parent->uuid);
        // Without the parent's current on-screen size there is no reliable
        // way to convert a mouse-pixel delta into this node's local units -
        // guessing (e.g. defaulting to 1) previously meant a couple of
        // pixels of mouse jitter could translate into a delta hundreds of
        // logical units wide, ballooning the box instantly. Decline instead.
        if (pit == screenRects_.end() || !pit->second.valid()) return;
        parentSrW = pit->second.w;
        parentSrH = pit->second.h;
    }

    resizing_.active = true;
    resizing_.nodeUuid = uuid;
    resizing_.corner = corner;
    resizing_.startRectX = node->rect.x;
    resizing_.startRectY = node->rect.y;
    resizing_.startRectW = node->rect.width;
    resizing_.startRectH = node->rect.height;
    resizing_.startScreenX = screenX;
    resizing_.startScreenY = screenY;

    if (parent) {
        resizing_.isChild = true;
        resizing_.parentSrW = parentSrW;
        resizing_.parentSrH = parentSrH;
        resizing_.parentRectW = parent->rect.width;
        resizing_.parentRectH = parent->rect.height;
    } else {
        resizing_.isChild = false;
        resizing_.screenScaleX = std::max(0.0001, canvas_.viewport_.textScale);
        resizing_.screenScaleY = resizing_.screenScaleX;
    }

    // A folder can't be shrunk past the point where its own children would
    // have to go below their own minimum size. Each direct child caps how
    // far the content can scale down (minChildW/child.width, the same way
    // scaling factors compose); the tightest child sets the floor. The
    // whole subtree is snapshotted so it can be rescaled in lockstep as the
    // node resizes, instead of only the direct children moving.
    if (!node->childrenUuids.empty()) {
        const double minChildW = 6.0, minChildH = 3.0;
        double sxFloor = 0.0, syFloor = 0.0;
        for (const auto& cid : node->childrenUuids) {
            auto child = canvas_.getNode(cid);
            if (!child || child->rect.width <= 0 || child->rect.height <= 0) continue;
            sxFloor = std::max(sxFloor, minChildW / child->rect.width);
            syFloor = std::max(syFloor, minChildH / child->rect.height);
        }
        if (sxFloor > 0.0) resizing_.minContentW = resizing_.startRectW * sxFloor;
        if (syFloor > 0.0) resizing_.minContentH = resizing_.startRectH * syFloor;

        for (const auto& d : canvas_.getDescendants(uuid)) {
            resizing_.startDescendantRects[d->uuid] = d->rect;
        }
    }
}

void InputHandler::updateResize(int screenX, int screenY) {
    auto node = canvas_.getNode(resizing_.nodeUuid);
    if (!node) return;

    double dsx = screenX - resizing_.startScreenX;
    double dsy = screenY - resizing_.startScreenY;
    double dx, dy;
    if (resizing_.isChild) {
        dx = resizing_.parentSrW > 0 ? dsx * resizing_.parentRectW / resizing_.parentSrW : 0;
        dy = resizing_.parentSrH > 0 ? dsy * resizing_.parentRectH / resizing_.parentSrH : 0;
    } else {
        dx = dsx / resizing_.screenScaleX;
        dy = dsy / resizing_.screenScaleY;
    }

    // Absolute floor, raised by the children's own minimum size when the
    // node has any - whichever is larger wins.
    double minW = std::max(6.0, resizing_.minContentW);
    double minH = std::max(3.0, resizing_.minContentH);
    double x = resizing_.startRectX, y = resizing_.startRectY;
    double w = resizing_.startRectW, h = resizing_.startRectH;

    bool left = (resizing_.corner == ResizeCorner::TopLeft || resizing_.corner == ResizeCorner::BottomLeft);
    bool top = (resizing_.corner == ResizeCorner::TopLeft || resizing_.corner == ResizeCorner::TopRight);
    bool right = (resizing_.corner == ResizeCorner::TopRight || resizing_.corner == ResizeCorner::BottomRight);
    bool bottom = (resizing_.corner == ResizeCorner::BottomLeft || resizing_.corner == ResizeCorner::BottomRight);

    // The edge(s) opposite the grabbed corner stay fixed - only the grabbed
    // corner follows the mouse. When a min size clamps growth in the
    // negative direction, the opposite edge (x/y) stops moving too instead
    // of overshooting past it.
    if (right) w = std::max(minW, resizing_.startRectW + dx);
    if (bottom) h = std::max(minH, resizing_.startRectH + dy);
    if (left) {
        w = std::max(minW, resizing_.startRectW - dx);
        x = resizing_.startRectX + (resizing_.startRectW - w);
    }
    if (top) {
        h = std::max(minH, resizing_.startRectH - dy);
        y = resizing_.startRectY + (resizing_.startRectH - h);
    }

    node->rect.x = x;
    node->rect.y = y;
    node->rect.width = w;
    node->rect.height = h;

    // Rescale the whole subtree to match, so children visibly shrink/grow
    // and reposition together with the node instead of spilling outside it
    // or leaving dead space as it's dragged smaller/larger.
    if (!resizing_.startDescendantRects.empty()) {
        double sx = resizing_.startRectW > 0 ? w / resizing_.startRectW : 1.0;
        double sy = resizing_.startRectH > 0 ? h / resizing_.startRectH : 1.0;
        for (const auto& [duuid, startRect] : resizing_.startDescendantRects) {
            auto dnode = canvas_.getNode(duuid);
            if (!dnode) continue;
            dnode->rect.x = startRect.x * sx;
            dnode->rect.y = startRect.y * sy;
            dnode->rect.width = startRect.width * sx;
            dnode->rect.height = startRect.height * sy;
            canvas_.markChanged(duuid);
        }
    }

    canvas_.expandParentToFit(resizing_.nodeUuid, 2.0);
    canvas_.markChanged(resizing_.nodeUuid);
}

void InputHandler::endResize() {
    if (!resizing_.active) return;
    auto node = canvas_.getNode(resizing_.nodeUuid);
    if (node) {
        canvas_.markChanged(resizing_.nodeUuid);
        if (!node->parentId.empty()) {
            canvas_.resolveOverlaps(node->parentId);
        }
    }
    resizing_ = ResizeState{};
}

double InputHandler::panScaleAtScreen(int screenX, int screenY) const {
    if (auto node = findNodeAtScreen(screenX, screenY)) {
        return rootPanScale(canvas_, *node);
    }

    double bestDistance = 1e18;
    double bestScale = canvas_.viewport_.textScale;

    for (const auto& [uuid, sr] : screenRects_) {
        if (!sr.valid()) continue;

        double dx = 0.0;
        if (screenX < sr.x) dx = sr.x - screenX;
        else if (screenX > sr.x + sr.w) dx = screenX - (sr.x + sr.w);

        double dy = 0.0;
        if (screenY < sr.y) dy = sr.y - screenY;
        else if (screenY > sr.y + sr.h) dy = screenY - (sr.y + sr.h);

        double distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            auto node = canvas_.getNode(uuid);
            if (!node) continue;
            bestDistance = distance;
            bestScale = rootPanScale(canvas_, *node);
        }
    }

    return std::max(0.0001, bestScale);
}

// ============================================================
// COORDINATE CONVERSION (screen cells РІвЂ вЂќ logical canvas)
// ============================================================
double InputHandler::screenToLogicalX(double sx) const {
    return canvas_.viewport_.centerX + (sx - canvasArea_.centerX()) / canvas_.viewport_.textScale;
}
double InputHandler::screenToLogicalY(double sy) const {
    return canvas_.viewport_.centerY + (sy - canvasArea_.centerY()) / canvas_.viewport_.textScale;
}
double InputHandler::logicalToScreenX(double lx) const {
    return (lx - canvas_.viewport_.centerX) * canvas_.viewport_.textScale + canvasArea_.centerX();
}
double InputHandler::logicalToScreenY(double ly) const {
    return (ly - canvas_.viewport_.centerY) * canvas_.viewport_.textScale + canvasArea_.centerY();
}

int InputHandler::utf8CharLen(const std::string& s, int bytePos) {
    if (bytePos < 0 || bytePos >= static_cast<int>(s.size())) return 1;
    unsigned char b = static_cast<unsigned char>(s[bytePos]);
    if (b < 0x80) return 1;
    if (b < 0xE0) return 2;
    if (b < 0xF0) return 3;
    return 4;
}

int InputHandler::utf8CharToByte(const std::string& s, int charIdx) {
    int pos = 0, ch = 0;
    while (pos < static_cast<int>(s.size()) && ch < charIdx) {
        pos += utf8CharLen(s, pos);
        ch++;
    }
    return pos;
}

int InputHandler::utf8CharCount(const std::string& s) {
    int count = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) count++;
    }
    return count;
}

} // namespace ic
