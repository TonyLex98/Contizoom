#include "model/canvas.h"
#include <chrono>
#include <algorithm>

namespace ic {

// ====================================================================
// Node management
// ====================================================================

std::string Canvas::createNode(const std::string& text, const Rect& rect,
                               const std::string& parentId, int level) {
    std::string uuid = Uuid::generate();
    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto node = std::make_shared<Node>(uuid, text, rect, level);
    node->parentId = parentId;
    node->createdAt = now;
    node->modifiedAt = now;
    node->autoResize();

    nodes_[uuid] = node;

    if (!parentId.empty()) {
        auto parentIt = nodes_.find(parentId);
        if (parentIt != nodes_.end()) {
            parentIt->second->childrenUuids.push_back(uuid);
        }
    }

    modifiedAt = now;
    notifyChanged();
    return uuid;
}

bool Canvas::removeNode(const std::string& uuid, bool recursive) {
    auto it = nodes_.find(uuid);
    if (it == nodes_.end()) return false;

    if (recursive && !it->second->childrenUuids.empty()) {
        // Collect all descendants
        std::vector<std::string> toRemove;
        std::vector<std::string> stack = it->second->childrenUuids;
        while (!stack.empty()) {
            std::string child = stack.back();
            stack.pop_back();
            toRemove.push_back(child);
            auto childIt = nodes_.find(child);
            if (childIt != nodes_.end()) {
                for (const auto& gc : childIt->second->childrenUuids) {
                    stack.push_back(gc);
                }
            }
        }
        for (const auto& desc : toRemove) {
            removeNode(desc, false);
        }
    }

    // Remove from parent's children list
    if (!it->second->parentId.empty()) {
        auto parentIt = nodes_.find(it->second->parentId);
        if (parentIt != nodes_.end()) {
            auto& children = parentIt->second->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), uuid),
                children.end());
        }
    }

    // Remove connections involving this node
    std::vector<std::string> connsToRemove;
    for (const auto& [connUuid, conn] : connections_) {
        if (conn->fromUuid == uuid || conn->toUuid == uuid) {
            connsToRemove.push_back(connUuid);
        }
    }
    for (const auto& c : connsToRemove) {
        connections_.erase(c);
    }

    nodes_.erase(it);

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    notifyChanged();
    return true;
}

std::shared_ptr<Node> Canvas::getNode(const std::string& uuid) const {
    auto it = nodes_.find(uuid);
    return it != nodes_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Node>> Canvas::getNodesAtLevel(int level) const {
    std::vector<std::shared_ptr<Node>> result;
    for (const auto& [uuid, node] : nodes_) {
        if (node->zoomLevel == level) {
            result.push_back(node);
        }
    }
    return result;
}

std::vector<std::shared_ptr<Node>> Canvas::getDescendants(const std::string& uuid) const {
    std::vector<std::shared_ptr<Node>> result;
    auto it = nodes_.find(uuid);
    if (it == nodes_.end()) return result;

    std::vector<std::string> stack = it->second->childrenUuids;
    while (!stack.empty()) {
        std::string child = stack.back();
        stack.pop_back();
        auto childIt = nodes_.find(child);
        if (childIt != nodes_.end()) {
            result.push_back(childIt->second);
            for (const auto& gc : childIt->second->childrenUuids) {
                stack.push_back(gc);
            }
        }
    }
    return result;
}

void Canvas::expandParentToFit(const std::string& nodeUuid, double padding) {
    auto node = getNode(nodeUuid);
    if (!node || node->parentId.empty()) return;

    auto parent = getNode(node->parentId);
    if (!parent) return;

    double childRight = node->rect.x + node->rect.width;
    double childBottom = node->rect.y + node->rect.height;

    bool expanded = false;
    // Только растим размеры, никогда не сдвигаем детей
    // Репозиционирование сделает resolveOverlaps на mouseUp
    if (childRight + padding > parent->rect.width) {
        parent->rect.width = childRight + padding;
        expanded = true;
    }
    if (childBottom + padding > parent->rect.height) {
        parent->rect.height = childBottom + padding;
        expanded = true;
    }

    if (expanded && !parent->parentId.empty()) {
        expandParentToFit(parent->uuid, padding);
    }
}

void Canvas::preventOverlap(const std::string& nodeUuid) {
    auto node = getNode(nodeUuid);
    if (!node || node->parentId.empty()) return;
    pushSiblings(nodeUuid, 1.0);
}

// ====================================================================
// Overlap resolution — X+Y, iterative, with parent text zone
// ====================================================================

bool Canvas::resolveOverlaps(const std::string& parentUuid) {
    auto parent = getNode(parentUuid);
    if (!parent || parent->childrenUuids.empty()) return false;

    // Вычисляем зону текста родителя (зона сверху, где рендерится текст)
    int textLines = 1;
    for (char c : parent->text) {
        if (c == '\n') textLines++;
    }
    double textZoneY = textLines * 1.2 + 2.0; // высота текстовой зоны с отступом
    double marginX = 3.0; // margin от левого/правого края
    double marginY = 2.0; // margin от нижнего края

    bool resolved = false;

    // Шаг 1: Выталкиваем детей из текстовой зоны
    for (const auto& cid : parent->childrenUuids) {
        auto child = getNode(cid);
        if (!child) continue;
        if (child->rect.y < textZoneY) {
            child->rect.y = textZoneY;
            resolved = true;
        }
    }

    // Шаг 2: Разрешаем пересечения сиблингов (итеративно)
    for (int iter = 0; iter < 50; ++iter) {
        bool anyOverlap = false;

        for (size_t i = 0; i < parent->childrenUuids.size(); ++i) {
            auto a = getNode(parent->childrenUuids[i]);
            if (!a) continue;

            for (size_t j = i + 1; j < parent->childrenUuids.size(); ++j) {
                auto b = getNode(parent->childrenUuids[j]);
                if (!b) continue;

                double ax1 = a->rect.x, ay1 = a->rect.y;
                double ax2 = a->rect.x + a->rect.width, ay2 = a->rect.y + a->rect.height;
                double bx1 = b->rect.x, by1 = b->rect.y;
                double bx2 = b->rect.x + b->rect.width, by2 = b->rect.y + b->rect.height;

                if (ax2 <= bx1 || ax1 >= bx2 || ay2 <= by1 || ay1 >= by2) continue;

                double overlapX = std::min(ax2 - bx1, bx2 - ax1);
                double overlapY = std::min(ay2 - by1, by2 - ay1);
                double gap = 2.0;

                if (overlapX < overlapY) {
                    // Разделяем по X
                    double aCenter = a->rect.centerX();
                    double bCenter = b->rect.centerX();
                    if (aCenter < bCenter) {
                        a->rect.x -= (overlapX + gap) / 2.0;
                        b->rect.x += (overlapX + gap) / 2.0;
                    } else {
                        a->rect.x += (overlapX + gap) / 2.0;
                        b->rect.x -= (overlapX + gap) / 2.0;
                    }
                } else {
                    // Разделяем по Y
                    double aCenter = a->rect.centerY();
                    double bCenter = b->rect.centerY();
                    if (aCenter < bCenter) {
                        a->rect.y -= (overlapY + gap) / 2.0;
                        b->rect.y += (overlapY + gap) / 2.0;
                    } else {
                        a->rect.y += (overlapY + gap) / 2.0;
                        b->rect.y -= (overlapY + gap) / 2.0;
                    }
                    // Не поднимать выше текстовой зоны
                    if (a->rect.y < textZoneY) a->rect.y = textZoneY;
                    if (b->rect.y < textZoneY) b->rect.y = textZoneY;
                }
                anyOverlap = true;
            }
        }
        if (!anyOverlap) break;
        resolved = true;
    }

    // Шаг 3: Клампим детей в границы родителя
    for (const auto& cid : parent->childrenUuids) {
        auto child = getNode(cid);
        if (!child) continue;
        child->rect.x = std::max(marginX, child->rect.x);
        child->rect.y = std::max(textZoneY, child->rect.y);
    }

    // Шаг 4: Расширяем родителя под всех детей
    double maxX = marginX, maxY = textZoneY;
    for (const auto& cid : parent->childrenUuids) {
        auto child = getNode(cid);
        if (!child) continue;
        maxX = std::max(maxX, child->rect.x + child->rect.width + marginX);
        maxY = std::max(maxY, child->rect.y + child->rect.height + marginY);
    }
    if (maxX > parent->rect.width || maxY > parent->rect.height) {
        parent->rect.width = maxX;
        parent->rect.height = maxY;
        resolved = true;
        if (!parent->parentId.empty()) resolveOverlaps(parent->parentId);
    }

    if (resolved) {
        auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        modifiedAt = now;
        notifyChanged();
    }
    return resolved;
}

// ====================================================================
// Push siblings — realtime collision during drag
// ====================================================================

void Canvas::pushSiblings(const std::string& nodeUuid, double margin) {
    auto node = getNode(nodeUuid);
    if (!node || node->parentId.empty()) return;

    auto parent = getNode(node->parentId);
    if (!parent) return;

    double nx1 = node->rect.x, ny1 = node->rect.y;
    double nx2 = node->rect.x + node->rect.width;
    double ny2 = node->rect.y + node->rect.height;

    for (const auto& sid : parent->childrenUuids) {
        if (sid == nodeUuid) continue;
        auto sib = getNode(sid);
        if (!sib) continue;

        double sx1 = sib->rect.x, sy1 = sib->rect.y;
        double sx2 = sib->rect.x + sib->rect.width;
        double sy2 = sib->rect.y + sib->rect.height;

        if (nx2 <= sx1 || nx1 >= sx2 || ny2 <= sy1 || ny1 >= sy2) continue;

        double overlapX = std::min(nx2 - sx1, sx2 - nx1);
        double overlapY = std::min(ny2 - sy1, sy2 - ny1);
        double push = std::max(overlapX, overlapY) + margin;

        // Толкаем сиблинга (не двигаем перетаскиваемый node)
        double nodeCenterX = node->rect.centerX();
        double sibCenterX = sib->rect.centerX();
        double nodeCenterY = node->rect.centerY();
        double sibCenterY = sib->rect.centerY();

        if (overlapX < overlapY) {
            if (sibCenterX < nodeCenterX)
                sib->rect.x -= push;
            else
                sib->rect.x += push;
        } else {
            if (sibCenterY < nodeCenterY)
                sib->rect.y -= push;
            else
                sib->rect.y += push;
        }
    }

    // Расширяем родителя если дети вышли за границы
    expandParentToFit(nodeUuid, 2.0);
}

// ====================================================================
// Transfer node — move to new parent with auto-positioning
// ====================================================================

bool Canvas::transferNode(const std::string& nodeUuid,
                           const std::string& newParentUuid,
                           PlacementMode mode) {
    auto node = getNode(nodeUuid);
    auto newParent = getNode(newParentUuid);
    if (!node || !newParent) return false;
    if (node->parentId == newParentUuid) return true; // Already there

    // Remove from old parent
    std::string oldParentId = node->parentId;
    if (!oldParentId.empty()) {
        auto oldParent = getNode(oldParentId);
        if (oldParent) {
            auto& children = oldParent->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), nodeUuid),
                children.end());
        }
    }

    // Compute placement
    double targetX, targetY;
    double gap = 5.0;

    if (newParent->childrenUuids.empty()) {
        // Первый ребёнок — центрируем
        targetX = (newParent->rect.width - node->rect.width) / 2.0;
        targetY = (newParent->rect.height - node->rect.height) / 2.0;
        if (targetX < 2) targetX = 2;
        if (targetY < 2) targetY = 2;
    } else {
        // Вычисляем bounding box существующих детей
        double maxR = 0, maxB = 0;
        for (const auto& cid : newParent->childrenUuids) {
            auto sib = getNode(cid);
            if (!sib) continue;
            maxR = std::max(maxR, sib->rect.x + sib->rect.width);
            maxB = std::max(maxB, sib->rect.y + sib->rect.height);
        }

        switch (mode) {
            case PlacementMode::AutoRight:
                targetX = maxR + gap;
                targetY = node->rect.y;
                break;
            case PlacementMode::AutoBottom:
                targetX = node->rect.x;
                targetY = maxB + gap;
                break;
            case PlacementMode::Center: {
                double centerX = (newParent->rect.width - node->rect.width) / 2.0;
                double centerY = (newParent->rect.height - node->rect.height) / 2.0;
                targetX = std::max(2.0, centerX);
                targetY = std::max(2.0, centerY);
                break;
            }
            case PlacementMode::Free:
                targetX = node->rect.x;
                targetY = node->rect.y;
                break;
        }
    }

    // Clamp to parent bounds (min)
    targetX = std::max(2.0, std::min(targetX, newParent->rect.width - node->rect.width - 2.0));
    targetY = std::max(2.0, std::min(targetY, newParent->rect.height - node->rect.height - 2.0));
    if (targetX < 2.0) targetX = 2.0;
    if (targetY < 2.0) targetY = 2.0;

    node->rect.x = targetX;
    node->rect.y = targetY;
    node->parentId = newParentUuid;
    node->zoomLevel = newParent->zoomLevel + 1;

    if (std::find(newParent->childrenUuids.begin(),
                  newParent->childrenUuids.end(), nodeUuid)
        == newParent->childrenUuids.end()) {
        newParent->childrenUuids.push_back(nodeUuid);
    }

    // Expand parent and resolve overlaps
    expandParentToFit(nodeUuid, 5.0);
    resolveOverlaps(newParentUuid);

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    notifyChanged();
    return true;
}

 std::vector<std::shared_ptr<Node>> Canvas::getVisibleNodes(
    const Viewport& vp, double viewWidth, double viewHeight) const {

    std::vector<std::shared_ptr<Node>> result;

    // Fractal zoom: all nodes are visible, scaled by depth difference.
    // Compute expanded viewport bounds to include nodes at other zoom levels
    // (child nodes near parent are always nearby in logical space)
    double vl = vp.left(viewWidth) - 500;
    double vr = vp.right(viewWidth) + 500;
    double vt = vp.top(viewHeight) - 500;
    double vb = vp.bottom(viewHeight) + 500;

    for (const auto& [uuid, node] : nodes_) {
        if (node->rect.x + node->rect.width > vl &&
            node->rect.x < vr &&
            node->rect.y + node->rect.height > vt &&
            node->rect.y < vb) {
            result.push_back(node);
        }
    }
    return result;
}

std::shared_ptr<Node> Canvas::findNodeAt(double x, double y) const {
    std::shared_ptr<Node> topmost;
    double topmostLevel = -1000;

    for (const auto& [uuid, node] : nodes_) {
        if (node->rect.contains(x, y)) {
            if (node->zoomLevel > topmostLevel) {
                topmost = node;
                topmostLevel = node->zoomLevel;
            }
        }
    }
    return topmost;
}

std::vector<std::shared_ptr<Node>> Canvas::findNodesInRect(
    double x1, double y1, double x2, double y2) const {

    std::vector<std::shared_ptr<Node>> result;
    double minX = std::min(x1, x2);
    double maxX = std::max(x1, x2);
    double minY = std::min(y1, y2);
    double maxY = std::max(y1, y2);

    for (const auto& [uuid, node] : nodes_) {
        if (node->rect.x + node->rect.width > minX &&
            node->rect.x < maxX &&
            node->rect.y + node->rect.height > minY &&
            node->rect.y < maxY) {
            result.push_back(node);
        }
    }
    return result;
}

// ====================================================================
// Connection management
// ====================================================================

std::string Canvas::createConnection(const std::string& fromUuid,
                                      const std::string& toUuid,
                                      ConnType type) {
    std::string connUuid = Uuid::generate();
    auto conn = std::make_shared<Connection>(connUuid, fromUuid, toUuid, type);
    connections_[connUuid] = conn;

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    notifyChanged();
    return connUuid;
}

bool Canvas::removeConnection(const std::string& connUuid) {
    auto it = connections_.find(connUuid);
    if (it == connections_.end()) return false;
    connections_.erase(it);

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    notifyChanged();
    return true;
}

std::shared_ptr<Connection> Canvas::getConnection(const std::string& uuid) const {
    auto it = connections_.find(uuid);
    return it != connections_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Connection>> Canvas::getNodeConnections(
    const std::string& nodeUuid) const {

    std::vector<std::shared_ptr<Connection>> result;
    for (const auto& [uuid, conn] : connections_) {
        if (conn->fromUuid == nodeUuid || conn->toUuid == nodeUuid) {
            result.push_back(conn);
        }
    }
    return result;
}

// ====================================================================
// Hierarchy operations
// ====================================================================

bool Canvas::promoteNode(const std::string& uuid) {
    auto it = nodes_.find(uuid);
    if (it == nodes_.end()) return false;

    Node* node = it->second.get();
    if (node->zoomLevel <= 0) return true; // Already at top

    // Remove from parent's children
    if (!node->parentId.empty()) {
        auto parentIt = nodes_.find(node->parentId);
        if (parentIt != nodes_.end()) {
            auto& children = parentIt->second->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), uuid),
                children.end());
        }
    }

    // Set new level and parent
    node->zoomLevel--;
    node->parentId = ""; // Move to root level of its parent's level

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    notifyChanged();
    return true;
}

bool Canvas::demoteNode(const std::string& uuid,
                        const std::string& newParentUuid) {
    auto it = nodes_.find(uuid);
    if (it == nodes_.end()) return false;

    auto parentIt = nodes_.find(newParentUuid);
    if (parentIt == nodes_.end()) return false;

    Node* node = it->second.get();

    // Remove from old parent
    if (!node->parentId.empty() && node->parentId != newParentUuid) {
        auto oldParentIt = nodes_.find(node->parentId);
        if (oldParentIt != nodes_.end()) {
            auto& children = oldParentIt->second->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), uuid),
                children.end());
        }
    }

    // Add to new parent
    node->parentId = newParentUuid;
    node->zoomLevel = parentIt->second->zoomLevel + 1;

    if (std::find(parentIt->second->childrenUuids.begin(),
                  parentIt->second->childrenUuids.end(), uuid)
        == parentIt->second->childrenUuids.end()) {
        parentIt->second->childrenUuids.push_back(uuid);
    }

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    notifyChanged();
    return true;
}

void Canvas::setNodeLevel(const std::string& uuid, int level) {
    auto it = nodes_.find(uuid);
    if (it != nodes_.end()) {
        it->second->zoomLevel = level;

        auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        modifiedAt = now;
        notifyChanged();
    }
}

// ====================================================================
// Frame operations
// ====================================================================

std::string Canvas::createFrame(const std::vector<std::string>& nodeUuids,
                                 const std::string& label) {
    if (nodeUuids.empty()) return "";

    double minX = 1e9, minY = 1e9;
    double maxX = -1e9, maxY = -1e9;

    for (const auto& uuid : nodeUuids) {
        auto it = nodes_.find(uuid);
        if (it != nodes_.end()) {
            const auto& r = it->second->rect;
            minX = std::min(minX, r.x);
            minY = std::min(minY, r.y);
            maxX = std::max(maxX, r.x + r.width);
            maxY = std::max(maxY, r.y + r.height);
        }
    }

    // Create frame node with padding
    double padding = 10.0;
    Rect frameRect;
    frameRect.x = minX - padding;
    frameRect.y = minY - padding;
    frameRect.width = (maxX - minX) + 2 * padding;
    frameRect.height = (maxY - minY) + 2 * padding;

    std::string frameUuid = createNode(label, frameRect, "", 0);
    if (!frameUuid.empty()) {
        auto frameNode = getNode(frameUuid);
        if (frameNode) {
            frameNode->hasFrame = true;
            frameNode->childrenUuids = nodeUuids;
            // Set parent of children to frame
            for (const auto& uuid : nodeUuids) {
                auto childIt = nodes_.find(uuid);
                if (childIt != nodes_.end()) {
                    childIt->second->parentId = frameUuid;
                }
            }
        }
    }

    return frameUuid;
}

// ====================================================================
// Internal
// ====================================================================

void Canvas::autoLayout() {
    // 1. Expand all parents to fit children (bottom-up: deepest first)
    {
        std::vector<std::string> ordered;
        for (const auto& [uuid, node] : nodes_) {
            if (!node->parentId.empty()) ordered.push_back(uuid);
        }
        // Сортируем по zoomLevel descending - deepest first
        std::sort(ordered.begin(), ordered.end(), [this](const std::string& a, const std::string& b) {
            auto na = getNode(a), nb = getNode(b);
            return na->zoomLevel > nb->zoomLevel;
        });
        for (const auto& uuid : ordered) {
            expandParentToFit(uuid, 3.0);
        }
    }

    // 2. Resolve overlaps for all parents (top-down: shallowest first)
    {
        std::vector<std::string> parents;
        for (const auto& [uuid, node] : nodes_) {
            if (!node->childrenUuids.empty()) {
                if (std::find(parents.begin(), parents.end(), uuid) == parents.end())
                    parents.push_back(uuid);
            }
        }
        // Сортируем по zoomLevel ascending - shallowest first
        std::sort(parents.begin(), parents.end(), [this](const std::string& a, const std::string& b) {
            auto na = getNode(a), nb = getNode(b);
            return na->zoomLevel < nb->zoomLevel;
        });
        for (const auto& parentId : parents) {
            resolveOverlaps(parentId);
        }
    }
}

void Canvas::notifyChanged() {
    if (changed_) changed_();
}

} // namespace ic
