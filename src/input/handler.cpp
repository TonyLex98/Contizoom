#include "input/handler.h"
#include "model/canvas.h"
#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <vector>

namespace ic {

InputHandler::InputHandler(Canvas& canvas) : canvas_(canvas) {}

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
    if (key == 'w' || key == 'k') { canvas_.viewport_.centerY += panAmt; return InputResult::Handled; }
    if (key == 's' || key == 'j') { canvas_.viewport_.centerY -= panAmt; return InputResult::Handled; }
    if (key == 'a' || key == 'h') { canvas_.viewport_.centerX -= panAmt; return InputResult::Handled; }
    if (key == 'd' || key == 'l') { canvas_.viewport_.centerX += panAmt; return InputResult::Handled; }

    // Zoom level
    if (key >= '0' && key <= '9') {
        canvas_.viewport_.zoomLevel = key - '0';
        return InputResult::Handled;
    }
    if (key == '[') {
        canvas_.viewport_.zoomLevel--;
        autoCenterViewportOnRoot();
        return InputResult::Handled;
    }
    if (key == ']') { canvas_.viewport_.zoomLevel++; return InputResult::Handled; }
    if (key == 'z') { zHeld_ = true; return InputResult::Handled; }

    // Actions
    if (key == 27 || key == '\x1b') { // Escape
        canvas_.selection_.clear();
        for (auto& [_, n] : canvas_.allNodes()) n->state = NodeState::Normal;
        cancelConnection();
        return InputResult::Handled;
    }
    if (key == 'f') { createFrame(); return InputResult::Handled; }
    if (key == 'r') { renameSelected(); return InputResult::Handled; }
    if (key == 'x') { deleteSelected(); return InputResult::Handled; }
    if (key == 'n') { createNodeAtCursor(); return InputResult::Handled; }
    if (key == '+') { canvas_.viewport_.textScale = std::min(5.0, canvas_.viewport_.textScale * 1.2); return InputResult::Handled; }
    if (key == '-') { canvas_.viewport_.textScale = std::max(0.1, canvas_.viewport_.textScale / 1.2); return InputResult::Handled; }
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
// MOUSE — all coordinates are SCREEN, convert to LOGICAL
// ============================================================
InputResult InputHandler::handleMouseMove(const InputEvent& event) {
    double lx = screenToLogicalX(event.mouseX);
    double ly = screenToLogicalY(event.mouseY);

    if (canvas_.selection_.active) {
        canvas_.selection_.update(lx, ly);
        return InputResult::Handled;
    }

    // Pending click → drag если мышь сдвинулась
    if (pendingClick_.active) {
        int dx = std::abs(event.mouseX - pendingClick_.screenX);
        int dy = std::abs(event.mouseY - pendingClick_.screenY);
        if (dx + dy > 2) {
            pendingClick_.active = false;
            draggingNodes_ = true;
            dragStates_.clear();
            for (auto& [uuid, n] : canvas_.allNodes()) {
                if (n->state != NodeState::Selected) continue;
                DragState ds;
                auto parent = canvas_.getNode(n->parentId);
                if (parent) {
                    ds.isChild = true;
                    ds.startRelX = n->rect.x;
                    ds.startRelY = n->rect.y;
                    ds.startScreenX = pendingClick_.screenX;
                    ds.startScreenY = pendingClick_.screenY;
                    auto pit = screenRects_.find(parent->uuid);
                    if (pit != screenRects_.end()) {
                        ds.parentSrW = pit->second.w;
                        ds.parentSrH = pit->second.h;
                    }
                    ds.parentRectW = parent->rect.width;
                    ds.parentRectH = parent->rect.height;
                } else {
                    ds.isChild = false;
                    ds.startRelX = n->rect.x;
                    ds.startRelY = n->rect.y;
                    ds.startScreenX = pendingClick_.screenX;
                    ds.startScreenY = pendingClick_.screenY;
                }
                dragStates_[n->uuid] = ds;
            }
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
                double dx = dsx / canvas_.viewport_.textScale;
                double dy = dsy / canvas_.viewport_.textScale;
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
        canvas_.viewport_.centerX -= dsx / canvas_.viewport_.textScale;
        canvas_.viewport_.centerY -= dsy / canvas_.viewport_.textScale;
        lastScreenX_ = event.mouseX;
        lastScreenY_ = event.mouseY;
        return InputResult::Handled;
    }
    // Hover — use screen rects for fractal hit testing
    for (auto& [_, n] : canvas_.allNodes())
        if (n->state == NodeState::Hovered) n->state = NodeState::Normal;
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

        // Double-click detection
        auto nowMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        bool isDoubleClick = (nowMs - lastClickTime_ < 350) &&
                              (std::abs(event.mouseX - lastClickX_) < 2) &&
                              (std::abs(event.mouseY - lastClickY_) < 2) &&
                              node && node->uuid == lastClickedNode_.value_or("");

        if (isDoubleClick && node) {
            // Double click: выбрать, редактировать имя или текст по позиции клика
            selectNode(node->uuid, event.mods.shift);
            auto pit = screenRects_.find(node->uuid);
            if (pit != screenRects_.end()) {
                bool onHeader = (event.mouseY <= pit->second.y + 1);
                if (onHeader) startEditingName(node->uuid);
                else startEditing(node->uuid);
            }
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

        // Если редактируем и кликаем — сохраняем текущее редактирование
        if (editingNode_) {
            commitEdit();
        }

        // Single click
        if (event.mods.ctrl && node) {
            startConnection(node->uuid);
            node->state = NodeState::InConnection;
        } else if (node) {
            if (node->state == NodeState::Selected) {
                // Pending click: если мышь не сдвинется — клик (edit), если сдвинется — drag (move)
                pendingClick_.active = true;
                pendingClick_.screenX = event.mouseX;
                pendingClick_.screenY = event.mouseY;
                pendingClick_.nodeUuid = node->uuid;
            } else {
                // Клик на невыбранную: снять выделение и редактирование
                if (editingNode_) commitEdit();
                for (auto& [_, n] : canvas_.allNodes())
                    if (n->state == NodeState::Selected) n->state = NodeState::Normal;
            }
        } else {
            // Клик в пустоту: снять выделение и редактирование
            if (editingNode_) commitEdit();
            for (auto& [_, n] : canvas_.allNodes())
                if (n->state == NodeState::Selected) n->state = NodeState::Normal;
        }

        mouseDown_.left = true;
        lastLogicalX_ = lx;
        lastLogicalY_ = ly;
        lastScreenX_ = event.mouseX;
        lastScreenY_ = event.mouseY;

        lastClickX_ = event.mouseX;
        lastClickY_ = event.mouseY;
        lastClickTime_ = nowMs;
        lastClickedNode_ = node ? std::optional<std::string>(node->uuid) : std::nullopt;
    } else if (event.button == 1) { // Right — area select
        if (editingNode_) commitEdit();
        canvas_.selection_.begin(lx, ly);
        mouseDown_.right = true;
    }
    return InputResult::Handled;
}

InputResult InputHandler::handleMouseUp(const InputEvent& event) {
    if (event.button == 0) {
        // Pending click → edit (если не конвертировался в drag)
        if (pendingClick_.active) {
            pendingClick_.active = false;
            auto node = canvas_.getNode(pendingClick_.nodeUuid);
            if (node) {
                auto pit = screenRects_.find(node->uuid);
                if (pit != screenRects_.end()) {
                    bool onHeader = (event.mouseY <= pit->second.y + 1);
                    if (onHeader) startEditingName(node->uuid);
                    else startEditing(node->uuid);
                }
            }
            mouseDown_.left = false;
            return InputResult::Handled;
        }

        // При отпускании — финальное разрешение всех пересечений в родителях
        // Собираем уникальных родителей выбранных нод
        std::set<std::string> parents;
        for (auto& [_, n] : canvas_.allNodes()) {
            if (n->state == NodeState::Selected && !n->parentId.empty()) {
                parents.insert(n->parentId);
            }
        }
        for (const auto& pId : parents) {
            canvas_.resolveOverlaps(pId);
        }

        // Notify changed for all selected nodes (incremental save)
        for (auto& [_, n] : canvas_.allNodes()) {
            if (n->state == NodeState::Selected) {
                canvas_.notifyChanged(n->uuid);
            }
        }

        draggingNodes_ = false;
        dragStates_.clear();
    }

    if (event.button == 0 && connectionFrom_) {
        auto target = findNodeAtScreen(event.mouseX, event.mouseY);
        if (target && target->uuid != *connectionFrom_)
            canvas_.createConnection(*connectionFrom_, target->uuid, "", ConnType::Arrow);
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
    double newScale = std::max(0.1, std::min(5.0, vp * factor));

    if (newScale != vp) {
        // Zoom toward cursor: keep logical point under mouse unchanged
        double cx = canvasArea_.centerX();
        double cy = canvasArea_.centerY();
        double lx = canvas_.viewport_.centerX + (event.mouseX - cx) / vp;
        double ly = canvas_.viewport_.centerY + (event.mouseY - cy) / vp;
        canvas_.viewport_.centerX = lx - (event.mouseX - cx) / newScale;
        canvas_.viewport_.centerY = ly - (event.mouseY - cy) / newScale;
        canvas_.viewport_.textScale = newScale;
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
    for (auto& [u,n] : canvas_.allNodes()) if (n->state==NodeState::Selected) canvas_.removeNode(u);
}

void InputHandler::promoteSelected() {
    for (auto& [u,n] : canvas_.allNodes()) if (n->state==NodeState::Selected) { canvas_.promoteNode(u); break; }
}

void InputHandler::demoteSelected() {
    for (auto& [u,n] : canvas_.allNodes()) if (n->state==NodeState::Selected) { canvas_.setNodeLevel(u, n->zoomLevel+1); break; }
}

void InputHandler::createNodeAtCursor() {
    // Если есть выбранный родитель — создаём ребёнка внутри
    std::string parentId;
    for (auto& [_, n] : canvas_.allNodes()) {
        if (n->state == NodeState::Selected) { parentId = n->uuid; break; }
    }
    Rect r;
    int level = 0;
    if (!parentId.empty()) {
        auto parent = canvas_.getNode(parentId);
        if (parent) {
            // Создаём ребёнка в центре родителя (относительные координаты)
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
        level = canvas_.viewport_.zoomLevel;
    }
    std::string uuid = canvas_.createNode("New Node", r, "New Node", parentId, level);
    if (!uuid.empty()) {
        auto node = canvas_.getNode(uuid);
        if (node) {
            node->autoResize();
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

void InputHandler::startConnection(const std::string& f) { connectionFrom_ = f; }
void InputHandler::cancelConnection() {
    if (connectionFrom_) {
        auto n = canvas_.getNode(*connectionFrom_);
        if (n && n->state==NodeState::InConnection) n->state = NodeState::Normal;
        connectionFrom_.reset();
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
    canvas_.editOverlay_.nodeUuid = nodeUuid;
    canvas_.editOverlay_.nodeRect = node->rect;
    canvas_.editOverlay_.text = node->name;
}

void InputHandler::handleEditKey(const InputEvent& event) {
    auto syncOverlay = [this]() {
        canvas_.editOverlay_.text = editText_;
        canvas_.editOverlay_.cursorCol = editCursorCol_;
        canvas_.editOverlay_.cursorRow = editCursorRow_;

        // Recalculate node height to fit text
        auto node = canvas_.getNode(*editingNode_);
        if (node) {
            int lines = 1;
            for (char c : editText_) if (c == '\n') lines++;
            double neededH = std::max(node->rect.height, static_cast<double>(lines + 1) * 1.2);
            double neededW = static_cast<double>(editText_.size() + 2);
            // Also check max line length
            std::istringstream iss(editText_);
            std::string line;
            int maxLen = 0;
            while (std::getline(iss, line)) {
                maxLen = std::max(maxLen, utf8CharCount(line));
            }
            neededW = std::max(node->rect.width, static_cast<double>(maxLen + 2));
            node->rect.height = neededH;
            node->rect.width = std::max(node->rect.width, neededW);
            canvas_.editOverlay_.nodeRect = node->rect;
        }
    };
    int key = event.key;

    // Escape — cancel
    if (key == 27 || key == '\x1b') { cancelEdit(); return; }

    // Ctrl+Enter — confirm
    if (event.mods.ctrl && key == '\r') { commitEdit(); return; }

    // Enter — new line (wraps within node width)
    if (key == '\r') {
        // Split text at cursor, insert newline
        std::vector<std::string> lines;
        std::istringstream iss(editText_);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);

        // If cursor is beyond line count, append empty line
        while (editCursorRow_ >= static_cast<int>(lines.size()))
            lines.push_back("");

        if (editCursorRow_ >= 0 && editCursorRow_ < static_cast<int>(lines.size())) {
            std::string& curLine = lines[editCursorRow_];
            int bytePos = utf8CharToByte(curLine, editCursorCol_);
            std::string before = curLine.substr(0, bytePos);
            std::string after = curLine.substr(bytePos);
            lines[editCursorRow_] = before;
            lines.insert(lines.begin() + editCursorRow_ + 1, after);
        }
        editText_ = "";
        for (size_t i = 0; i < lines.size(); i++) {
            if (i > 0) editText_ += "\n";
            editText_ += lines[i];
        }
        editCursorRow_++;
        editCursorCol_ = 0;
        syncOverlay();
        return;
    }

    // Backspace
    if (key == 8 || key == 0x08) {
        if (editCursorCol_ > 0) {
            std::vector<std::string> lines;
            std::istringstream iss(editText_);
            std::string line;
            while (std::getline(iss, line)) lines.push_back(line);
            if (editCursorRow_ >= 0 && editCursorRow_ < static_cast<int>(lines.size())) {
                auto& cl = lines[editCursorRow_];
                int charCount = utf8CharCount(cl);
                if (editCursorCol_ > 0 && editCursorCol_ <= charCount) {
                    int bytePos = utf8CharToByte(cl, editCursorCol_ - 1);
                    int charLen = utf8CharLen(cl, bytePos);
                    cl.erase(bytePos, charLen);
                    editCursorCol_--;
                }
            }
            editText_ = "";
            for (size_t i = 0; i < lines.size(); i++) {
                if (i > 0) editText_ += "\n";
                editText_ += lines[i];
            }
        } else if (editCursorRow_ > 0) {
            std::vector<std::string> lines;
            std::istringstream iss(editText_);
            std::string line;
            while (std::getline(iss, line)) lines.push_back(line);
            if (editCursorRow_ > 0 && editCursorRow_ < static_cast<int>(lines.size())) {
                std::string prev = lines[editCursorRow_ - 1];
                editCursorCol_ = utf8CharCount(prev);
                lines[editCursorRow_ - 1] += lines[editCursorRow_];
                lines.erase(lines.begin() + editCursorRow_);
                editCursorRow_--;
            }
            editText_ = "";
            for (size_t i = 0; i < lines.size(); i++) {
                if (i > 0) editText_ += "\n";
                editText_ += lines[i];
            }
        }
        syncOverlay();
        return;
    }

    // Delete
    if (key == 0x0D || key == 46) {
        std::vector<std::string> lines;
        std::istringstream iss(editText_);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
        if (editCursorRow_ >= 0 && editCursorRow_ < static_cast<int>(lines.size())) {
            auto& cl = lines[editCursorRow_];
            int charCount = utf8CharCount(cl);
            if (editCursorCol_ < charCount) {
                int bytePos = utf8CharToByte(cl, editCursorCol_);
                int charLen = utf8CharLen(cl, bytePos);
                cl.erase(bytePos, charLen);
            }
        }
        editText_ = "";
        for (size_t i = 0; i < lines.size(); i++) {
            if (i > 0) editText_ += "\n";
            editText_ += lines[i];
        }
        syncOverlay();
        return;
    }

    // Arrow keys
    if (key == 37) { // Left
        if (editCursorCol_ > 0) editCursorCol_--;
        else if (editCursorRow_ > 0) {
            editCursorRow_--;
            std::vector<std::string> lines;
            std::istringstream iss(editText_);
            std::string line;
            while (std::getline(iss, line)) lines.push_back(line);
            editCursorCol_ = utf8CharCount(lines[editCursorRow_]);
        }
        syncOverlay();
        return;
    }
    if (key == 39) { // Right
        std::vector<std::string> lines;
        std::istringstream iss(editText_);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
        if (editCursorRow_ >= 0 && editCursorRow_ < static_cast<int>(lines.size()) &&
            editCursorCol_ < utf8CharCount(lines[editCursorRow_])) {
            editCursorCol_++;
        } else if (editCursorRow_ < static_cast<int>(lines.size()) - 1) {
            editCursorRow_++;
            editCursorCol_ = 0;
        }
        syncOverlay();
        return;
    }
    if (key == 38) { // Up
        if (editCursorRow_ > 0) {
            editCursorRow_--;
            std::vector<std::string> lines;
            std::istringstream iss(editText_);
            std::string line;
            while (std::getline(iss, line)) lines.push_back(line);
            editCursorCol_ = std::min(editCursorCol_, utf8CharCount(lines[editCursorRow_]));
        }
        syncOverlay();
        return;
    }
    if (key == 40) { // Down
        std::vector<std::string> lines;
        std::istringstream iss(editText_);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
        if (editCursorRow_ < static_cast<int>(lines.size()) - 1) {
            editCursorRow_++;
            editCursorCol_ = std::min(editCursorCol_, utf8CharCount(lines[editCursorRow_]));
        }
        syncOverlay();
        return;
    }

    // Printable characters — Unicode (wchar_t)
    wchar_t ch = event.ch;
    if (!ch || ch < 0x20) return; // Non-printable

    // Insert at cursor (convert wchar_t to UTF-8)
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

    std::vector<std::string> lines;
    std::istringstream iss(editText_);
    std::string line;
    while (std::getline(iss, line)) lines.push_back(line);
    while (editCursorRow_ >= static_cast<int>(lines.size())) lines.push_back("");
    if (editCursorRow_ >= 0 && editCursorRow_ < static_cast<int>(lines.size())) {
        auto& cl = lines[editCursorRow_];
        int bytePos = utf8CharToByte(cl, editCursorCol_);
        cl.insert(bytePos, utf8ch);
        editCursorCol_++;
    }
    editText_ = "";
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) editText_ += "\n";
        editText_ += lines[i];
    }

    // Sync to canvas overlay for rendering
    canvas_.editOverlay_.text = editText_;
    canvas_.editOverlay_.cursorCol = editCursorCol_;
    canvas_.editOverlay_.cursorRow = editCursorRow_;
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
    canvas_.notifyChanged(*editingNode_);
    editingNode_.reset();
    canvas_.editOverlay_.active = false;
}

void InputHandler::cancelEdit() {
    editingNode_.reset();
    canvas_.editOverlay_.active = false;
}

// ============================================================
// HIT TESTING (screen coordinates → node)
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

// ============================================================
// COORDINATE CONVERSION (screen cells ↔ logical canvas)
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
