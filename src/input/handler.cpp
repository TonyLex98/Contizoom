#include "input/handler.h"
#include "i18n/i18n.h"
#include "util/clock.h"
#include "util/utf8.h"

#include <algorithm>
#include <cmath>

namespace ic {

namespace {

constexpr uint64_t kDoubleClickMs = 400;
constexpr int kDragThreshold = 2;  // cells of movement before a click becomes a drag
constexpr double kPanCells = 8.0;
constexpr double kZoomStep = 1.15;

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines(1);
    for (const char c : text) {
        if (c == '\n') lines.emplace_back();
        else if (c != '\r') lines.back() += c;
    }
    return lines;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

// Style cycle for T: arrows first, then plain lines.
ConnType nextStyle(ConnType t) {
    switch (t) {
        case ConnType::Arrow: return ConnType::DashedArrow;
        case ConnType::DashedArrow: return ConnType::DottedArrow;
        case ConnType::DottedArrow: return ConnType::Line;
        case ConnType::Line: return ConnType::DashedLine;
        case ConnType::DashedLine: return ConnType::DottedLine;
        case ConnType::DottedLine: return ConnType::Arrow;
    }
    return ConnType::Arrow;
}

}  // namespace

InputHandler::InputHandler(Canvas& canvas, UiState& ui) : canvas_(canvas), ui_(ui) {}

void InputHandler::setLayout(const std::map<std::string, ScreenRect>& screenRects,
                             const std::map<std::string, ScreenRect>& sidebarRects, const CanvasArea& area,
                             const ScreenRect& editorRect) {
    screenRects_ = screenRects;
    sidebarRects_ = sidebarRects;
    area_ = area;
    editorRect_ = editorRect;
}

InputResult InputHandler::process(const InputEvent& e) {
    sidebarClicked_.clear();
    switch (e.type) {
        case InputType::KeyPress: return onKey(e);
        case InputType::MouseDown: return onMouseDown(e);
        case InputType::MouseMove: return onMouseMove(e);
        case InputType::MouseUp: return onMouseUp(e);
        case InputType::MouseScroll: return onScroll(e);
        case InputType::Quit: return InputResult::RequestQuit;
        case InputType::KeyRelease:
        case InputType::Resize: break;
    }
    return InputResult::NotHandled;
}

// ====================================================================
// Undo
// ====================================================================

void InputHandler::edit(const std::function<void()>& change) {
    const std::string before = history_.snapshot(canvas_);
    change();
    history_.record(before, canvas_);
}

void InputHandler::afterContentReplaced() {
    ui_.nodeEditor = NodeEditor{};
    ui_.connEditor = ConnectionEditor{};
    ui_.prune(canvas_);
}

// ====================================================================
// Coordinates
// ====================================================================

double InputHandler::toWorldX(double sx) const {
    return canvas_.viewport.centerX + (sx - area_.centerX()) / canvas_.viewport.scale;
}

double InputHandler::toWorldY(double sy) const {
    return canvas_.viewport.centerY + (sy - area_.centerY()) / canvas_.viewport.scale;
}

// How many of the node's own (parent-relative) units one screen cell is.
// Children are laid out proportionally inside their parent's on-screen
// box, so the ratio comes from the parent's last drawn size. If the parent
// isn't on screen there's no reliable ratio and the caller should decline
// rather than guess (a wrong guess can fling the box far away).
bool InputHandler::unitsPerCell(const Node& node, double& ux, double& uy) const {
    auto parent = canvas_.getNode(node.parentId);
    if (!parent) {
        ux = uy = 1.0 / canvas_.viewport.scale;
        return true;
    }
    auto it = screenRects_.find(parent->uuid);
    if (it == screenRects_.end() || it->second.w <= 1.0 || it->second.h <= 1.0) return false;
    ux = parent->rect.width / it->second.w;
    uy = parent->rect.height / it->second.h;
    return true;
}

// ====================================================================
// Hit testing
// ====================================================================

std::shared_ptr<Node> InputHandler::nodeAt(int x, int y) const {
    std::shared_ptr<Node> best;
    for (const auto& [uuid, sr] : screenRects_) {
        if (!sr.valid()) continue;
        const CellBox b = cellBox(sr);
        const int w = std::max(b.w, 1), h = std::max(b.h, 1);
        if (x < b.x || x >= b.x + w || y < b.y || y >= b.y + h) continue;
        auto node = canvas_.getNode(uuid);
        if (node && (!best || node->depth > best->depth)) best = node;  // innermost wins
    }
    return best;
}

// A connection whose drawn route passes through (x, y). Lines are drawn
// over boxes, so a visible line wins over the box beneath it. The end
// cells belong to the boxes' borders and don't count.
std::string InputHandler::connectionAt(int x, int y) const {
    for (const auto& [uuid, conn] : canvas_.allConnections()) {
        auto from = screenRects_.find(conn->fromUuid);
        auto to = screenRects_.find(conn->toUuid);
        if (from == screenRects_.end() || to == screenRects_.end()) continue;
        const CellPath path = routeConnection(from->second, to->second, *conn);
        if (path.empty() || std::make_pair(x, y) == path.front() || std::make_pair(x, y) == path.back()) continue;
        if (distanceToPath(path, x, y) <= 0.5) return uuid;
    }
    return {};
}

ResizeCorner InputHandler::resizeCornerAt(int x, int y, std::string& uuid) const {
    for (const auto& id : ui_.selected) {
        auto it = screenRects_.find(id);
        if (it == screenRects_.end()) continue;
        const ResizeCorner c = cornerAt(it->second, x, y);
        if (c != ResizeCorner::None) {
            uuid = id;
            return c;
        }
    }
    return ResizeCorner::None;
}

// Anchors only count where they're visible (see UiState::showsAnchors),
// plus on the box under the mouse; `anyBox` while dragging a connection.
Anchor InputHandler::anchorAt(int x, int y, std::string& uuid, bool anyBox) const {
    const auto under = nodeAt(x, y);
    std::shared_ptr<Node> best;
    Anchor bestAnchor;
    for (const auto& [id, sr] : screenRects_) {
        if (!anyBox && !ui_.showsAnchors(id) && !(under && under->uuid == id)) continue;
        const Anchor a = ic::anchorAt(sr, x, y);
        if (!a.valid()) continue;
        auto node = canvas_.getNode(id);
        if (node && (!best || node->depth > best->depth)) {  // a child's anchor wins over its parent's
            best = node;
            bestAnchor = a;
        }
    }
    if (best) uuid = best->uuid;
    return bestAnchor;
}

bool InputHandler::isDoubleClick(const std::string& target, int x, int y, uint64_t now) const {
    return !target.empty() && target == lastClickTarget_ && now - lastClickTime_ < kDoubleClickMs &&
           std::abs(x - lastClickX_) <= 1 && std::abs(y - lastClickY_) <= 1;
}

void InputHandler::updateHover(int x, int y) {
    auto node = nodeAt(x, y);
    ui_.hovered = node ? node->uuid : std::string();

    ui_.cornerHover = resizeCornerAt(x, y, ui_.cornerHoverNode);
    if (ui_.cornerHover == ResizeCorner::None) ui_.cornerHoverNode.clear();

    ui_.anchorHover = anchorAt(x, y, ui_.anchorHoverNode, gesture_ == Gesture::Connect);
    if (!ui_.anchorHover.valid()) ui_.anchorHoverNode.clear();
}

// ====================================================================
// Keyboard
// ====================================================================

InputResult InputHandler::onKey(const InputEvent& e) {
    if (ui_.connEditor.active) {
        connectionEditorKey(e);
        return InputResult::Handled;
    }
    if (ui_.nodeEditor.active) {
        editorKey(e);
        return InputResult::Handled;
    }

    if (e.mods.ctrl) {
        switch (e.key) {
            case 'Q': return InputResult::RequestQuit;
            case 'S': return InputResult::RequestSave;
            case 'N': return InputResult::RequestNew;
            case 'Z': perform(e.mods.shift ? Action::Redo : Action::Undo); return InputResult::Handled;
            case 'Y': perform(Action::Redo); return InputResult::Handled;
            default: return InputResult::NotHandled;
        }
    }

    // A selected connection takes its own keys.
    if (!ui_.selectedConnection.empty()) {
        switch (e.key) {
            case 'T': perform(Action::ConnectionNextStyle); return InputResult::Handled;
            case 'V': perform(Action::ConnectionReverse); return InputResult::Handled;
            case key::Enter:
            case key::F2:
            case 'R': perform(Action::ConnectionRename); return InputResult::Handled;
            case 'X':
            case key::Delete: perform(Action::ConnectionDelete); return InputResult::Handled;
            default: break;
        }
    }

    // Camera.
    if (e.key == key::Up || e.key == 'W' || e.key == 'K') pan(0, -kPanCells);
    else if (e.key == key::Down || e.key == 'S' || e.key == 'J') pan(0, kPanCells);
    else if (e.key == key::Left || e.key == 'A' || e.key == 'H') pan(-kPanCells, 0);
    else if (e.key == key::Right || e.key == 'D' || e.key == 'L') pan(kPanCells, 0);
    else if (e.ch == U'+' || e.ch == U'=' || e.key == key::Add || e.key == key::OemPlus)
        zoomAt(kZoomStep, area_.centerX(), area_.centerY());
    else if (e.ch == U'-' || e.key == key::Subtract || e.key == key::OemMinus)
        zoomAt(1.0 / kZoomStep, area_.centerX(), area_.centerY());
    else if (e.ch == U']') zoomAt(2.0, area_.centerX(), area_.centerY());
    else if (e.ch == U'[') zoomAt(0.5, area_.centerX(), area_.centerY());
    else if (e.key == '0' || e.key == key::Home) perform(Action::FitAll);
    else if (e.key == key::Enter) perform(Action::ZoomTo);

    // Editing.
    else if (e.key == key::Escape) {
        ui_.clearSelection();
        ui_.connectionSource.clear();
        ui_.connectPreview = false;
        if (gesture_ == Gesture::Connect || gesture_ == Gesture::AreaSelect) gesture_ = Gesture::None;
        ui_.area.clear();
    } else if (e.key == 'N') perform(Action::NewBox);
    else if (e.key == 'R' || e.key == key::F2) perform(Action::Rename);
    else if (e.key == 'E') perform(Action::EditText);
    else if (e.key == 'X' || e.key == key::Delete) perform(Action::Delete);
    else if (e.key == 'F') perform(Action::Frame);
    else if (e.key == key::Tab) perform(e.mods.shift ? Action::Demote : Action::Promote);
    else return InputResult::NotHandled;
    return InputResult::Handled;
}

// ====================================================================
// Actions
// ====================================================================

void InputHandler::perform(Action action) {
    const std::string first = ui_.selected.empty() ? std::string() : *ui_.selected.begin();
    auto conn = canvas_.getConnection(ui_.selectedConnection);

    switch (action) {
        case Action::NewBox: createBox(); break;
        case Action::NewBoxHere:
            createBoxAt(context_.x, context_.y,
                        context_.kind == ContextTarget::Kind::Node ? context_.uuid : std::string());
            break;
        case Action::EditText:
            if (!first.empty()) startEditing(first, false);
            break;
        case Action::Rename:
            if (!first.empty()) startEditing(first, true);
            break;
        case Action::Delete:
            if (conn) {
                perform(Action::ConnectionDelete);
                break;
            }
            edit([&] {
                for (const auto& uuid : topLevelSelection()) canvas_.removeSubtree(uuid);
            });
            ui_.prune(canvas_);
            break;
        case Action::Frame: {
            const auto members = topLevelSelection();
            if (members.empty()) break;
            std::string frame;
            edit([&] { frame = canvas_.createFrame(members, tr(Msg::DefaultFrame)); });
            if (!frame.empty()) select(frame, false);
            break;
        }
        case Action::Promote:
            edit([&] {
                for (const auto& uuid : topLevelSelection()) canvas_.promoteNode(uuid);
            });
            break;
        case Action::Demote:
            edit([&] {
                for (const auto& uuid : topLevelSelection()) canvas_.demoteNode(uuid);
            });
            break;
        case Action::ZoomTo:
            if (!first.empty()) zoomToNode(first);
            break;
        case Action::FitAll: fitAll(); break;

        case Action::ConnectionRename:
            if (conn) {
                const auto [x, y] = connectionLabelCell(conn->uuid);
                startConnectionEdit(conn->uuid, x, y);
            }
            break;
        case Action::ConnectionReverse:
            if (conn) {
                edit([&] {
                    std::swap(conn->fromUuid, conn->toUuid);
                    std::swap(conn->fromAnchor, conn->toAnchor);
                    canvas_.markConfigChanged();
                });
            }
            break;
        case Action::ConnectionNextStyle:
            if (conn) setConnectionStyle(nextStyle(conn->type));
            break;
        case Action::ConnectionDelete:
            if (conn) {
                edit([&] { canvas_.removeConnection(conn->uuid); });
                ui_.selectedConnection.clear();
            }
            break;

        case Action::Undo:
            if (history_.undo(canvas_)) afterContentReplaced();
            break;
        case Action::Redo:
            if (history_.redo(canvas_)) afterContentReplaced();
            break;
    }
}

void InputHandler::setConnectionStyle(ConnType type) {
    auto conn = canvas_.getConnection(ui_.selectedConnection);
    if (!conn || conn->type == type) return;
    edit([&] {
        conn->type = type;
        canvas_.markConfigChanged();
    });
}

void InputHandler::select(const std::string& uuid, bool additive) {
    if (!additive) ui_.selected.clear();
    ui_.selectedConnection.clear();
    if (canvas_.getNode(uuid)) ui_.selected.insert(uuid);
}

void InputHandler::selectConnection(const std::string& uuid) {
    ui_.selected.clear();
    ui_.selectedConnection = uuid;
}

// Selected nodes without a selected ancestor. Moving or deleting a parent
// already covers its subtree; acting on both would apply it twice.
std::vector<std::string> InputHandler::topLevelSelection() const {
    std::vector<std::string> result;
    for (const auto& uuid : ui_.selected) {
        auto node = canvas_.getNode(uuid);
        if (!node) continue;
        bool covered = false;
        for (std::string p = node->parentId; !p.empty() && !covered;) {
            covered = ui_.isSelected(p);
            auto parent = canvas_.getNode(p);
            p = parent ? parent->parentId : std::string();
        }
        if (!covered) result.push_back(uuid);
    }
    return result;
}

void InputHandler::createBox() {
    std::string parentId = ui_.selected.size() == 1 ? *ui_.selected.begin() : std::string();
    if (parentId.empty()) parentId = ui_.hovered;
    auto parent = canvas_.getNode(parentId);

    Rect r{0, 0, 20, 4};
    if (parent) {
        // Below the parent's existing content.
        double y = 3.0;
        for (const auto& cid : parent->childrenUuids) {
            if (auto c = canvas_.getNode(cid)) y = std::max(y, c->rect.bottom() + 2.0);
        }
        r.x = 3.0;
        r.y = y;
    } else {
        parentId.clear();
        r.x = toWorldX(area_.centerX()) - r.width / 2;
        r.y = toWorldY(area_.centerY()) - r.height / 2;
    }

    std::string uuid;
    edit([&] {
        uuid = canvas_.createNode("", r, tr(Msg::DefaultBox), parentId);
        if (!uuid.empty() && !parentId.empty()) {
            canvas_.expandParentToFit(uuid, 2.0);
            canvas_.resolveOverlaps(parentId);
        }
    });
    if (uuid.empty()) return;
    select(uuid, false);
    startEditing(uuid, true);
}

// A new box with its top-left at a screen cell: a root box on empty canvas,
// or a child of `parentId` at that spot inside it.
void InputHandler::createBoxAt(int screenX, int screenY, const std::string& parentId) {
    auto parent = canvas_.getNode(parentId);
    const double wx = toWorldX(screenX), wy = toWorldY(screenY);
    Rect r{wx, wy, 20, 4};
    if (parent) {
        const Rect origin = canvas_.computeWorldRect(parentId);
        r.x = wx - origin.x;
        r.y = wy - origin.y;
    }
    std::string uuid;
    edit([&] {
        uuid = canvas_.createNode("", r, tr(Msg::DefaultBox), parent ? parentId : std::string());
        if (!uuid.empty() && parent) {
            canvas_.expandParentToFit(uuid, 2.0);
            canvas_.resolveOverlaps(parentId);
        }
    });
    if (uuid.empty()) return;
    select(uuid, false);
    startEditing(uuid, true);
}

// ====================================================================
// Mouse
// ====================================================================

InputResult InputHandler::onMouseDown(const InputEvent& e) {
    const int x = e.mouseX, y = e.mouseY;

    if (e.button == MouseButton::Right) {
        if (ui_.nodeEditor.active) commitEdit();
        if (ui_.connEditor.active) commitConnectionEdit();
        // Context: a connection line, a box, or empty canvas.
        context_ = ContextTarget{};
        context_.x = x;
        context_.y = y;
        if (area_.contains(x, y)) {
            if (std::string conn = connectionAt(x, y); !conn.empty()) {
                context_.kind = ContextTarget::Kind::Connection;
                context_.uuid = conn;
                selectConnection(conn);
            } else if (auto node = nodeAt(x, y)) {
                context_.kind = ContextTarget::Kind::Node;
                context_.uuid = node->uuid;
                if (!ui_.isSelected(node->uuid)) select(node->uuid, false);
            }
        }
        return InputResult::RequestMenu;
    }
    if (e.button != MouseButton::Left) return InputResult::NotHandled;

    // Outline in the sidebar: jump to the box.
    for (const auto& [uuid, sr] : sidebarRects_) {
        const CellBox b = cellBox(sr);
        if (y == b.y && x >= b.x && x <= b.right()) {
            sidebarClicked_ = uuid;
            select(uuid, false);
            return InputResult::SidebarNodeClick;
        }
    }
    if (!area_.contains(x, y)) return InputResult::Handled;

    const uint64_t now = clock::monotonicMillis();
    auto node = nodeAt(x, y);
    const std::string conn = connectionAt(x, y);
    // Empty canvas counts as a target too, so double-clicking it can create a box.
    static const std::string kEmptyCanvas = "#canvas";
    const std::string target = !conn.empty() ? conn : node ? node->uuid : kEmptyCanvas;
    const bool doubleClick = isDoubleClick(target, x, y, now);
    lastClickTarget_ = target;
    lastClickTime_ = doubleClick ? 0 : now;  // a third click starts over
    lastClickX_ = x;
    lastClickY_ = y;

    // A click inside the editor keeps editing; anywhere else saves it.
    if (ui_.nodeEditor.active && editorRect_.valid()) {
        const CellBox eb = cellBox(editorRect_);
        if (x >= eb.x && x <= eb.right() && y >= eb.y && y <= eb.bottom()) return InputResult::Handled;
    }
    if (ui_.nodeEditor.active) commitEdit();
    if (ui_.connEditor.active) commitConnectionEdit();

    pressX_ = lastX_ = x;
    pressY_ = lastY_ = y;
    gesture_ = Gesture::None;

    // Double-click: a line's label, or a box's name (header row) / text.
    if (doubleClick) {
        if (!conn.empty()) {
            selectConnection(conn);
            startConnectionEdit(conn, x, y);
            return InputResult::Handled;
        }
        if (node) {
            select(node->uuid, false);
            auto sr = screenRects_.find(node->uuid);
            const bool onHeader = sr != screenRects_.end() && y == cellBox(sr->second).y;
            startEditing(node->uuid, onHeader);
            return InputResult::Handled;
        }
        if (!e.mods.shift) {
            createBoxAt(x, y, "");  // double-click on empty canvas
            return InputResult::Handled;
        }
    }

    // Corner of a selected box: resize.
    std::string cornerNode;
    if (const ResizeCorner corner = resizeCornerAt(x, y, cornerNode); corner != ResizeCorner::None) {
        beginResize(cornerNode, corner);
        if (gesture_ == Gesture::Resize) return InputResult::Handled;
    }

    // Anchor point (or Ctrl+drag a box): drag to another box to connect.
    std::string anchorNode;
    const Anchor anchor = anchorAt(x, y, anchorNode, false);
    if (anchor.valid() || (e.mods.ctrl && node)) {
        ui_.connectionSource = anchor.valid() ? anchorNode : node->uuid;
        connectFromAnchor_ = anchor;
        ui_.connectPreview = true;
        ui_.previewX = x;
        ui_.previewY = y;
        ui_.previewFromAnchor = anchor;
        gesture_ = Gesture::Connect;
        return InputResult::Handled;
    }

    if (!conn.empty()) {
        selectConnection(conn);
        return InputResult::Handled;
    }

    if (node) {
        pressNode_ = node->uuid;
        ui_.selectedConnection.clear();
        if (e.mods.shift) {
            // Shift+click toggles membership in the selection.
            if (ui_.isSelected(node->uuid)) ui_.selected.erase(node->uuid);
            else ui_.selected.insert(node->uuid);
        } else {
            if (!ui_.isSelected(node->uuid)) select(node->uuid, false);
            gesture_ = Gesture::PendingClick;  // becomes a drag if the mouse moves
        }
        return InputResult::Handled;
    }

    // Empty canvas.
    if (e.mods.shift) {
        ui_.area.begin(toWorldX(x), toWorldY(y));
        gesture_ = Gesture::AreaSelect;
        return InputResult::Handled;
    }
    ui_.clearSelection();
    gesture_ = Gesture::Pan;
    return InputResult::Handled;
}

InputResult InputHandler::onMouseMove(const InputEvent& e) {
    const int x = e.mouseX, y = e.mouseY;

    switch (gesture_) {
        case Gesture::Resize:
            updateResize(x, y);
            break;

        case Gesture::Connect:
            ui_.previewX = x;
            ui_.previewY = y;
            updateHover(x, y);
            break;

        case Gesture::AreaSelect:
            ui_.area.update(toWorldX(x), toWorldY(y));
            break;

        case Gesture::Pan:
            pan(-(x - lastX_), -(y - lastY_));
            break;

        case Gesture::PendingClick:
            if (std::abs(x - pressX_) + std::abs(y - pressY_) <= kDragThreshold) break;
            beginDrag();
            gesture_ = Gesture::DragNodes;
            [[fallthrough]];

        case Gesture::DragNodes:
            for (const auto& [uuid, start] : drag_) {
                auto node = canvas_.getNode(uuid);
                if (!node) continue;
                placeAtWorld(*node, start.world.x + (x - pressX_) * start.unitsPerCellX,
                             start.world.y + (y - pressY_) * start.unitsPerCellY);
                canvas_.markChanged(uuid);
                canvas_.expandParentToFit(uuid, 2.0);
            }
            break;

        case Gesture::None:
            updateHover(x, y);
            break;
    }
    lastX_ = x;
    lastY_ = y;
    return InputResult::Handled;
}

InputResult InputHandler::onMouseUp(const InputEvent& e) {
    if (e.button != MouseButton::Left) return InputResult::Handled;
    const int x = e.mouseX, y = e.mouseY;

    switch (gesture_) {
        case Gesture::DragNodes: {
            std::set<std::string> parents;
            for (const auto& [uuid, _] : drag_) {
                if (auto n = canvas_.getNode(uuid)) {
                    canvas_.markChanged(uuid);
                    if (!n->parentId.empty()) parents.insert(n->parentId);
                }
            }
            for (const auto& p : parents) canvas_.resolveOverlaps(p);
            history_.record(gestureSnapshot_, canvas_);
            drag_.clear();
            break;
        }

        case Gesture::Resize:
            if (auto n = canvas_.getNode(resize_.uuid)) {
                canvas_.markChanged(n->uuid);
                if (!n->parentId.empty()) canvas_.resolveOverlaps(n->parentId);
            }
            history_.record(gestureSnapshot_, canvas_);
            resize_ = ResizeStart{};
            break;

        case Gesture::Connect: finishConnect(x, y); break;
        case Gesture::AreaSelect: finishAreaSelect(); break;
        case Gesture::Pan: canvas_.markConfigChanged(); break;
        case Gesture::PendingClick:  // a plain click only selects; editing is a double-click
        case Gesture::None: break;
    }
    gesture_ = Gesture::None;
    updateHover(x, y);
    return InputResult::Handled;
}

InputResult InputHandler::onScroll(const InputEvent& e) {
    if (e.scroll == 0) return InputResult::NotHandled;
    zoomAt(e.scroll > 0 ? kZoomStep : 1.0 / kZoomStep, e.mouseX, e.mouseY);
    return InputResult::Handled;
}

// ====================================================================
// Gestures
// ====================================================================

void InputHandler::beginDrag() {
    gestureSnapshot_ = history_.snapshot(canvas_);
    drag_.clear();
    for (const auto& uuid : topLevelSelection()) {
        auto node = canvas_.getNode(uuid);
        DragStart s;
        s.world = canvas_.computeWorldRect(uuid);
        if (unitsPerCell(*node, s.unitsPerCellX, s.unitsPerCellY)) drag_[uuid] = s;
    }
}

// Sets the node's parent-relative position so its top-left lands at the
// given world point, using the parent's *current* position.
void InputHandler::placeAtWorld(Node& node, double worldX, double worldY) {
    const Rect origin = node.parentId.empty() ? Rect{0, 0, 0, 0} : canvas_.computeWorldRect(node.parentId);
    node.rect.x = worldX - origin.x;
    node.rect.y = worldY - origin.y;
}

void InputHandler::beginResize(const std::string& uuid, ResizeCorner corner) {
    auto node = canvas_.getNode(uuid);
    if (!node) return;
    ResizeStart r;
    if (!unitsPerCell(*node, r.unitsPerCellX, r.unitsPerCellY)) return;
    r.uuid = uuid;
    r.corner = corner;
    r.world = canvas_.computeWorldRect(uuid);

    // A box can't shrink past the point where its children would have to go
    // below their own minimum size. The whole subtree is snapshotted so it
    // scales in lockstep with the box.
    if (!node->childrenUuids.empty()) {
        constexpr double kMinChildW = 6.0, kMinChildH = 3.0;
        double fx = 0, fy = 0;
        for (const auto& cid : node->childrenUuids) {
            auto c = canvas_.getNode(cid);
            if (!c || c->rect.width <= 0 || c->rect.height <= 0) continue;
            fx = std::max(fx, kMinChildW / c->rect.width);
            fy = std::max(fy, kMinChildH / c->rect.height);
        }
        r.minW = std::max(r.minW, r.world.width * fx);
        r.minH = std::max(r.minH, r.world.height * fy);
        for (const auto& d : canvas_.getDescendants(uuid)) r.descendants[d->uuid] = d->rect;
    }
    gestureSnapshot_ = history_.snapshot(canvas_);
    resize_ = r;
    gesture_ = Gesture::Resize;
}

void InputHandler::updateResize(int x, int y) {
    auto node = canvas_.getNode(resize_.uuid);
    if (!node) return;
    const double dx = (x - pressX_) * resize_.unitsPerCellX;
    const double dy = (y - pressY_) * resize_.unitsPerCellY;
    const Rect& s = resize_.world;
    Rect r = s;  // new world rect

    const ResizeCorner c = resize_.corner;
    const bool left = c == ResizeCorner::TopLeft || c == ResizeCorner::BottomLeft;
    const bool top = c == ResizeCorner::TopLeft || c == ResizeCorner::TopRight;

    // The corner opposite the grabbed one stays put.
    if (left) {
        r.width = std::max(resize_.minW, s.width - dx);
        r.x = s.x + (s.width - r.width);
    } else {
        r.width = std::max(resize_.minW, s.width + dx);
    }
    if (top) {
        r.height = std::max(resize_.minH, s.height - dy);
        r.y = s.y + (s.height - r.height);
    } else {
        r.height = std::max(resize_.minH, s.height + dy);
    }
    node->rect.width = r.width;
    node->rect.height = r.height;
    placeAtWorld(*node, r.x, r.y);

    const double fx = s.width > 0 ? r.width / s.width : 1.0;
    const double fy = s.height > 0 ? r.height / s.height : 1.0;
    for (const auto& [duuid, start] : resize_.descendants) {
        if (auto d = canvas_.getNode(duuid)) {
            d->rect = Rect{start.x * fx, start.y * fy, start.width * fx, start.height * fy};
            canvas_.markChanged(duuid);
        }
    }
    canvas_.expandParentToFit(resize_.uuid, 2.0);
    canvas_.markChanged(resize_.uuid);
}

void InputHandler::finishConnect(int x, int y) {
    const std::string source = ui_.connectionSource;
    ui_.connectionSource.clear();
    ui_.connectPreview = false;

    auto target = nodeAt(x, y);
    if (!target || target->uuid == source || !canvas_.getNode(source)) return;

    // The same link twice is almost always a slip: select the existing one.
    for (const auto& [id, c] : canvas_.allConnections()) {
        if (c->fromUuid == source && c->toUuid == target->uuid) {
            selectConnection(id);
            return;
        }
    }

    std::string anchorNode;
    const Anchor toAnchor = anchorAt(x, y, anchorNode, true);
    std::string id;
    edit([&] {
        id = canvas_.createConnection(source, target->uuid);
        if (auto conn = canvas_.getConnection(id)) {
            conn->fromAnchor = connectFromAnchor_;
            if (toAnchor.valid() && anchorNode == target->uuid) conn->toAnchor = toAnchor;
        }
    });
    if (!id.empty()) selectConnection(id);
    connectFromAnchor_ = Anchor{};
}

void InputHandler::finishAreaSelect() {
    const AreaSelection& a = ui_.area;
    ui_.clearSelection();
    for (const auto& [uuid, _] : screenRects_) {  // only what's actually visible
        const Rect w = canvas_.computeWorldRect(uuid);
        if (w.right() > a.left() && w.left() < a.right() && w.bottom() > a.top() && w.top() < a.bottom())
            ui_.selected.insert(uuid);
    }
    ui_.area.clear();
}

// Where a connection's label sits on screen (for the rename field).
std::pair<int, int> InputHandler::connectionLabelCell(const std::string& connUuid) const {
    auto conn = canvas_.getConnection(connUuid);
    if (conn) {
        auto from = screenRects_.find(conn->fromUuid);
        auto to = screenRects_.find(conn->toUuid);
        if (from != screenRects_.end() && to != screenRects_.end()) {
            const CellPath p = routeConnection(from->second, to->second, *conn);
            if (!p.empty()) return p[p.size() / 2];
        }
    }
    return {context_.x, context_.y};
}

// ====================================================================
// Camera
// ====================================================================

void InputHandler::pan(double cellsX, double cellsY) {
    canvas_.viewport.centerX += cellsX / canvas_.viewport.scale;
    canvas_.viewport.centerY += cellsY / canvas_.viewport.scale;
    canvas_.markConfigChanged();
}

// Zooms keeping the world point under (sx, sy) fixed on screen.
void InputHandler::zoomAt(double factor, double sx, double sy) {
    Viewport& vp = canvas_.viewport;
    const double newScale = std::clamp(vp.scale * factor, Viewport::kMinScale, Viewport::kMaxScale);
    if (newScale == vp.scale) return;
    const double wx = toWorldX(sx), wy = toWorldY(sy);
    vp.scale = newScale;
    vp.centerX = wx - (sx - area_.centerX()) / newScale;
    vp.centerY = wy - (sy - area_.centerY()) / newScale;
    canvas_.markConfigChanged();
}

void InputHandler::zoomToNode(const std::string& uuid) {
    if (!canvas_.getNode(uuid)) return;
    const Rect r = canvas_.computeWorldRect(uuid);
    Viewport& vp = canvas_.viewport;
    vp.centerX = r.centerX();
    vp.centerY = r.centerY();
    const double sx = area_.width() * 0.8 / std::max(1e-9, r.width);
    const double sy = area_.height() * 0.8 / std::max(1e-9, r.height);
    vp.scale = std::clamp(std::min(sx, sy), Viewport::kMinScale, Viewport::kMaxScale);
    canvas_.markConfigChanged();
}

void InputHandler::fitAll() {
    const auto roots = canvas_.getChildren("");
    Viewport& vp = canvas_.viewport;
    if (roots.empty()) {
        vp = Viewport{};
        canvas_.markConfigChanged();
        return;
    }
    double x0 = roots.front()->rect.left(), y0 = roots.front()->rect.top();
    double x1 = roots.front()->rect.right(), y1 = roots.front()->rect.bottom();
    for (const auto& r : roots) {
        x0 = std::min(x0, r->rect.left());
        y0 = std::min(y0, r->rect.top());
        x1 = std::max(x1, r->rect.right());
        y1 = std::max(y1, r->rect.bottom());
    }
    vp.centerX = (x0 + x1) / 2;
    vp.centerY = (y0 + y1) / 2;
    const double sx = area_.width() * 0.9 / std::max(1e-9, x1 - x0);
    const double sy = area_.height() * 0.9 / std::max(1e-9, y1 - y0);
    vp.scale = std::clamp(std::min(sx, sy), Viewport::kMinScale, Viewport::kMaxScale);
    canvas_.markConfigChanged();
}

// ====================================================================
// Node editor
// ====================================================================

void InputHandler::startEditing(const std::string& uuid, bool name) {
    auto node = canvas_.getNode(uuid);
    if (!node) return;
    NodeEditor& ed = ui_.nodeEditor;
    ed = NodeEditor{};
    ed.active = true;
    ed.editingName = name;
    ed.nodeUuid = uuid;
    ed.text = name ? node->name : node->text;
    const auto lines = splitLines(ed.text);
    ed.cursorRow = static_cast<int>(lines.size()) - 1;
    ed.cursorCol = utf8::length(lines.back());
}

void InputHandler::commitEdit() {
    const NodeEditor ed = ui_.nodeEditor;
    ui_.nodeEditor = NodeEditor{};
    if (!ed.active) return;
    auto node = canvas_.getNode(ed.nodeUuid);
    if (!node || (ed.editingName ? node->name : node->text) == ed.text) return;

    edit([&] {
        if (ed.editingName) {
            node->name = ed.text;
            // Make room for the label.
            node->rect.width = std::max(node->rect.width, static_cast<double>(utf8::width(ed.text) + 4));
        } else {
            node->text = ed.text;
            node->autoResize();
        }
        canvas_.markChanged(node->uuid);
        canvas_.expandParentToFit(node->uuid, 2.0);
        if (!node->parentId.empty()) canvas_.resolveOverlaps(node->parentId);
    });
}

void InputHandler::editorKey(const InputEvent& e) {
    NodeEditor& ed = ui_.nodeEditor;
    auto lines = splitLines(ed.text);
    ed.cursorRow = std::clamp(ed.cursorRow, 0, static_cast<int>(lines.size()) - 1);
    std::string* line = &lines[ed.cursorRow];
    ed.cursorCol = std::clamp(ed.cursorCol, 0, utf8::length(*line));

    switch (e.key) {
        case key::Escape: ui_.nodeEditor = NodeEditor{}; return;  // cancel
        case key::Enter:
            if (ed.editingName || e.mods.ctrl) {
                commitEdit();
                return;
            }
            {
                const size_t at = utf8::byteOffset(*line, ed.cursorCol);
                std::string rest = line->substr(at);
                line->erase(at);
                lines.insert(lines.begin() + ed.cursorRow + 1, rest);
                ++ed.cursorRow;
                ed.cursorCol = 0;
            }
            break;
        case key::Backspace:
            if (ed.cursorCol > 0) {
                utf8::eraseAt(*line, --ed.cursorCol);
            } else if (ed.cursorRow > 0) {
                ed.cursorCol = utf8::length(lines[ed.cursorRow - 1]);
                lines[ed.cursorRow - 1] += *line;
                lines.erase(lines.begin() + ed.cursorRow);
                --ed.cursorRow;
            }
            break;
        case key::Delete:
            if (ed.cursorCol < utf8::length(*line)) {
                utf8::eraseAt(*line, ed.cursorCol);
            } else if (ed.cursorRow + 1 < static_cast<int>(lines.size())) {
                *line += lines[ed.cursorRow + 1];
                lines.erase(lines.begin() + ed.cursorRow + 1);
            }
            break;
        case key::Left:
            if (ed.cursorCol > 0) {
                --ed.cursorCol;
            } else if (ed.cursorRow > 0) {
                --ed.cursorRow;
                ed.cursorCol = utf8::length(lines[ed.cursorRow]);
            }
            break;
        case key::Right:
            if (ed.cursorCol < utf8::length(*line)) {
                ++ed.cursorCol;
            } else if (ed.cursorRow + 1 < static_cast<int>(lines.size())) {
                ++ed.cursorRow;
                ed.cursorCol = 0;
            }
            break;
        case key::Up:
            if (ed.cursorRow > 0) --ed.cursorRow;
            break;
        case key::Down:
            if (ed.cursorRow + 1 < static_cast<int>(lines.size())) ++ed.cursorRow;
            break;
        case key::Home: ed.cursorCol = 0; break;
        case key::End: ed.cursorCol = utf8::length(*line); break;
        default:
            if (e.ch < 0x20 || e.mods.ctrl) return;
            utf8::insertAt(*line, ed.cursorCol++, e.ch);
            break;
    }
    ed.text = joinLines(lines);
    ed.cursorCol = std::min(ed.cursorCol, utf8::length(lines[ed.cursorRow]));
}

// ====================================================================
// Connection label editor
// ====================================================================

void InputHandler::startConnectionEdit(const std::string& connUuid, int x, int y) {
    auto conn = canvas_.getConnection(connUuid);
    if (!conn) return;
    ConnectionEditor& ed = ui_.connEditor;
    ed = ConnectionEditor{};
    ed.active = true;
    ed.connUuid = connUuid;
    ed.text = conn->name;
    ed.cursorCol = utf8::length(ed.text);
    ed.screenX = x;
    ed.screenY = y;
}

void InputHandler::commitConnectionEdit() {
    const ConnectionEditor ed = ui_.connEditor;
    ui_.connEditor = ConnectionEditor{};
    if (!ed.active) return;
    auto conn = canvas_.getConnection(ed.connUuid);
    if (!conn || conn->name == ed.text) return;
    edit([&] {
        conn->name = ed.text;
        canvas_.markConfigChanged();
    });
}

void InputHandler::connectionEditorKey(const InputEvent& e) {
    ConnectionEditor& ed = ui_.connEditor;
    const int len = utf8::length(ed.text);
    switch (e.key) {
        case key::Escape: ed = ConnectionEditor{}; return;
        case key::Enter: commitConnectionEdit(); return;
        case key::Backspace:
            if (ed.cursorCol > 0) utf8::eraseAt(ed.text, --ed.cursorCol);
            break;
        case key::Delete:
            if (ed.cursorCol < len) utf8::eraseAt(ed.text, ed.cursorCol);
            break;
        case key::Left: ed.cursorCol = std::max(0, ed.cursorCol - 1); break;
        case key::Right: ed.cursorCol = std::min(len, ed.cursorCol + 1); break;
        case key::Home: ed.cursorCol = 0; break;
        case key::End: ed.cursorCol = len; break;
        default:
            if (e.ch < 0x20 || e.mods.ctrl) return;
            utf8::insertAt(ed.text, ed.cursorCol++, e.ch);
            break;
    }
}

}  // namespace ic
