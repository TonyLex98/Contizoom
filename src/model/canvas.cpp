#include "model/canvas.h"
#include <chrono>
#include <algorithm>

namespace ic {

// ====================================================================
// Auto-save throttling
// ====================================================================

void Canvas::markChanged(const std::string& uuid) {
    if (uuid.empty()) return;
    changedNodes_.insert(uuid);
    lastChangeTime_ = std::chrono::steady_clock::now();
}

void Canvas::markCanvasChanged() {
    canvasChanged_ = true;
    lastChangeTime_ = std::chrono::steady_clock::now();
}

void Canvas::notifyChanged(bool force) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeTime_);
    if (!force && elapsed < throttleDelay_) return; // Still throttling

    if ((changedNodes_.empty() && !canvasChanged_) || !changed_) return;

    changed_(changedNodes_, canvasChanged_);
    changedNodes_.clear();
    canvasChanged_ = false;
}

void Canvas::processPendingChanges() {
    notifyChanged(false);
}

void Canvas::flushChanges() {
    notifyChanged(true);
}

void Canvas::clear() {
    nodes_.clear();
    connections_.clear();
    changedNodes_.clear();
    canvasChanged_ = false;
    selection_.clear();
    editOverlay_ = EditOverlay{};
}

void Canvas::rebuildHierarchy() {
    for (auto& [_, node] : nodes_) {
        node->childrenUuids.clear();
    }

    for (auto& [uuid, node] : nodes_) {
        if (node->parentId.empty()) {
            node->zoomLevel = 0;
            continue;
        }
        auto parentIt = nodes_.find(node->parentId);
        if (parentIt == nodes_.end()) {
            node->parentId.clear();
            node->zoomLevel = 0;
            continue;
        }
        node->zoomLevel = parentIt->second->zoomLevel + 1;
        auto& children = parentIt->second->childrenUuids;
        if (std::find(children.begin(), children.end(), uuid) == children.end()) {
            children.push_back(uuid);
        }
    }
}

// ====================================================================
// Node management
// ====================================================================

std::string Canvas::createNode(const std::string& text, const Rect& rect,
                                const std::string& name,
                                const std::string& parentId, int level) {
    if (parentId.empty()) level = 0;

    std::string uuid = Uuid::generate();
    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto node = std::make_shared<Node>(uuid, text, rect, level);
    node->name = name;
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
    markChanged(uuid);
    if (!parentId.empty()) markChanged(parentId);
    markCanvasChanged();
    notifyChanged();
    return uuid;
}

bool Canvas::removeNode(const std::string& uuid, bool recursive) {
    auto it = nodes_.find(uuid);
    if (it == nodes_.end()) return false;

    if (recursive && !it->second->childrenUuids.empty()) {
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

    if (!it->second->parentId.empty()) {
        auto parentIt = nodes_.find(it->second->parentId);
        if (parentIt != nodes_.end()) {
            auto& children = parentIt->second->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), uuid),
                children.end());
            markChanged(parentIt->first);
        }
    }

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
    markCanvasChanged();
    notifyChanged();
    return true;
}

std::shared_ptr<Node> Canvas::getNode(const std::string& uuid) const {
    auto it = nodes_.find(uuid);
    return it != nodes_.end() ? it->second : nullptr;
}

void Canvas::insertNode(const std::string& uuid, std::shared_ptr<Node> node) {
    nodes_[uuid] = node;
    if (!node->parentId.empty()) {
        auto parentIt = nodes_.find(node->parentId);
        if (parentIt != nodes_.end()) {
            auto& children = parentIt->second->childrenUuids;
            if (std::find(children.begin(), children.end(), uuid) == children.end()) {
                children.push_back(uuid);
            }
        }
    }
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
    if (childRight + padding > parent->rect.width) {
        parent->rect.width = childRight + padding;
        expanded = true;
    }
    if (childBottom + padding > parent->rect.height) {
        parent->rect.height = childBottom + padding;
        expanded = true;
    }

    if (expanded) {
        markChanged(parent->uuid);
        if (!parent->parentId.empty()) {
            expandParentToFit(parent->uuid, padding);
        }
    }
}

void Canvas::preventOverlap(const std::string& nodeUuid) {
    auto node = getNode(nodeUuid);
    if (!node || node->parentId.empty()) return;
    pushSiblings(nodeUuid, 1.0);
}

// ====================================================================
// Overlap resolution
// ====================================================================

bool Canvas::resolveOverlaps(const std::string& parentUuid) {
    auto parent = getNode(parentUuid);
    if (!parent || parent->childrenUuids.empty()) return false;

    int textLines = 1;
    for (char c : parent->text) {
        if (c == '\n') textLines++;
    }
    double textZoneY = textLines * 1.2 + 2.0;
    double marginX = 3.0;
    double marginY = 2.0;

    bool resolved = false;
    double gap = std::max(2.0, std::min(15.0, std::max(parent->rect.width, parent->rect.height) * 0.06));

    for (const auto& cid : parent->childrenUuids) {
        auto child = getNode(cid);
        if (!child) continue;
        if (child->rect.y < textZoneY) {
            child->rect.y = textZoneY;
            markChanged(cid);
            resolved = true;
        }
    }

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

                double ax1g = ax1 - gap / 2.0, ay1g = ay1 - gap / 2.0;
                double ax2g = ax2 + gap / 2.0, ay2g = ay2 + gap / 2.0;
                double bx1g = bx1 - gap / 2.0, by1g = by1 - gap / 2.0;
                double bx2g = bx2 + gap / 2.0, by2g = by2 + gap / 2.0;

                if (ax2g <= bx1g || ax1g >= bx2g || ay2g <= by1g || ay1g >= by2g) continue;

                double overlapX = std::min(ax2 - bx1, bx2 - ax1);
                double overlapY = std::min(ay2 - by1, by2 - ay1);

                if (overlapX < overlapY) {
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
                    double aCenter = a->rect.centerY();
                    double bCenter = b->rect.centerY();
                    if (aCenter < bCenter) {
                        a->rect.y -= (overlapY + gap) / 2.0;
                        b->rect.y += (overlapY + gap) / 2.0;
                    } else {
                        a->rect.y += (overlapY + gap) / 2.0;
                        b->rect.y -= (overlapY + gap) / 2.0;
                    }
                }

                if (a->rect.y < textZoneY) a->rect.y = textZoneY;
                if (b->rect.y < textZoneY) b->rect.y = textZoneY;
                if (a->rect.x < marginX) a->rect.x = marginX;
                if (b->rect.x < marginX) b->rect.x = marginX;
                anyOverlap = true;
            }
        }
        if (!anyOverlap) break;
        resolved = true;
    }

    // Mark all children as changed (they may have been repositioned)
    for (const auto& cid : parent->childrenUuids) {
        auto child = getNode(cid);
        if (child) {
            child->rect.x = std::max(marginX, child->rect.x);
            child->rect.y = std::max(textZoneY, child->rect.y);
            markChanged(cid);
        }
    }

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
        markChanged(parentUuid);
        resolved = true;
        if (!parent->parentId.empty()) resolveOverlaps(parent->parentId);
    }

    if (resolved) {
        auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        modifiedAt = now;
    }
    return resolved;
}

// ====================================================================
// Push siblings
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
        markChanged(sid);
    }

    expandParentToFit(nodeUuid, 2.0);
}

// ====================================================================
// Transfer node
// ====================================================================

bool Canvas::transferNode(const std::string& nodeUuid,
                           const std::string& newParentUuid,
                           PlacementMode mode) {
    auto node = getNode(nodeUuid);
    auto newParent = getNode(newParentUuid);
    if (!node || !newParent) return false;
    if (node->parentId == newParentUuid) return true;

    std::string oldParentId = node->parentId;
    if (!oldParentId.empty()) {
        auto oldParent = getNode(oldParentId);
        if (oldParent) {
            auto& children = oldParent->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), nodeUuid),
                children.end());
            markChanged(oldParentId);
        }
    }

    double targetX, targetY;
    double gap = 5.0;

    if (newParent->childrenUuids.empty()) {
        targetX = (newParent->rect.width - node->rect.width) / 2.0;
        targetY = (newParent->rect.height - node->rect.height) / 2.0;
        if (targetX < 2) targetX = 2;
        if (targetY < 2) targetY = 2;
    } else {
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

    markChanged(nodeUuid);
    markChanged(newParentUuid);
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
    double vl = vp.left(viewWidth) - 500;
    double vr = vp.right(viewWidth) + 500;
    double vt = vp.top(viewHeight) - 500;
    double vb = vp.bottom(viewHeight) + 500;

    for (const auto& [uuid, node] : nodes_) {
        Rect world = computeWorldRect(uuid);
        if (world.x + world.width > vl &&
            world.x < vr &&
            world.y + world.height > vt &&
            world.y < vb) {
            result.push_back(node);
        }
    }
    return result;
}

std::shared_ptr<Node> Canvas::findNodeAt(double x, double y) const {
    std::shared_ptr<Node> topmost;
    double topmostLevel = -1000;

    for (const auto& [uuid, node] : nodes_) {
        Rect world = computeWorldRect(uuid);
        if (world.contains(x, y)) {
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
    double minX = std::min(x1, x2), maxX = std::max(x1, x2);
    double minY = std::min(y1, y2), maxY = std::max(y1, y2);

    for (const auto& [uuid, node] : nodes_) {
        Rect world = computeWorldRect(uuid);
        if (world.x + world.width > minX &&
            world.x < maxX &&
            world.y + world.height > minY &&
            world.y < maxY) {
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
                                      const std::string& name,
                                      ConnType type) {
    std::string connUuid = Uuid::generate();
    auto conn = std::make_shared<Connection>(connUuid, fromUuid, toUuid, type);
    conn->name = name;
    connections_[connUuid] = conn;

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    markCanvasChanged();
    notifyChanged();
    return connUuid;
}

void Canvas::insertConnection(const std::string& uuid, std::shared_ptr<Connection> connection) {
    if (!connection) return;
    connection->uuid = uuid;
    connections_[uuid] = connection;
}

bool Canvas::removeConnection(const std::string& connUuid) {
    auto it = connections_.find(connUuid);
    if (it == connections_.end()) return false;
    connections_.erase(it);

    auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    modifiedAt = now;
    markCanvasChanged();
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
    if (node->zoomLevel <= 0) return true;

    if (!node->parentId.empty()) {
        auto parentIt = nodes_.find(node->parentId);
        if (parentIt != nodes_.end()) {
            auto& children = parentIt->second->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), uuid),
                children.end());
            markChanged(parentIt->first);
        }
    }

    node->zoomLevel--;
    node->parentId = "";
    markChanged(uuid);

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

    if (!node->parentId.empty() && node->parentId != newParentUuid) {
        auto oldParentIt = nodes_.find(node->parentId);
        if (oldParentIt != nodes_.end()) {
            auto& children = oldParentIt->second->childrenUuids;
            children.erase(
                std::remove(children.begin(), children.end(), uuid),
                children.end());
            markChanged(oldParentIt->first);
        }
    }

    node->parentId = newParentUuid;
    node->zoomLevel = parentIt->second->zoomLevel + 1;

    if (std::find(parentIt->second->childrenUuids.begin(),
                  parentIt->second->childrenUuids.end(), uuid)
        == parentIt->second->childrenUuids.end()) {
        parentIt->second->childrenUuids.push_back(uuid);
    }

    markChanged(uuid);
    markChanged(newParentUuid);

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
        markChanged(uuid);

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

    double padding = 10.0;
    Rect frameRect;
    frameRect.x = minX - padding;
    frameRect.y = minY - padding;
    frameRect.width = (maxX - minX) + 2 * padding;
    frameRect.height = (maxY - minY) + 2 * padding;

    std::string frameUuid = createNode(label, frameRect, label, "", 0);
    if (!frameUuid.empty()) {
        auto frameNode = getNode(frameUuid);
        if (frameNode) {
            frameNode->hasFrame = true;
            frameNode->childrenUuids = nodeUuids;
            for (const auto& uuid : nodeUuids) {
                auto childIt = nodes_.find(uuid);
                if (childIt != nodes_.end()) {
                    childIt->second->parentId = frameUuid;
                    markChanged(uuid);
                }
            }
            markChanged(frameUuid);
            markCanvasChanged();
        }
    }

    return frameUuid;
}

// ====================================================================
// Internal
// ====================================================================

void Canvas::autoLayout() {
    {
        std::vector<std::string> ordered;
        for (const auto& [uuid, node] : nodes_) {
            if (!node->parentId.empty()) ordered.push_back(uuid);
        }
        std::sort(ordered.begin(), ordered.end(), [this](const std::string& a, const std::string& b) {
            auto na = getNode(a), nb = getNode(b);
            return na->zoomLevel > nb->zoomLevel;
        });
        for (const auto& uuid : ordered) {
            expandParentToFit(uuid, 3.0);
        }
    }

    {
        std::vector<std::string> parents;
        for (const auto& [uuid, node] : nodes_) {
            if (!node->childrenUuids.empty()) {
                if (std::find(parents.begin(), parents.end(), uuid) == parents.end())
                    parents.push_back(uuid);
            }
        }
        std::sort(parents.begin(), parents.end(), [this](const std::string& a, const std::string& b) {
            auto na = getNode(a), nb = getNode(b);
            return na->zoomLevel < nb->zoomLevel;
        });
        for (const auto& parentId : parents) {
            resolveOverlaps(parentId);
        }
    }
    notifyChanged();
}

Rect Canvas::computeWorldRect(const std::string& uuid) const {
    auto it = nodes_.find(uuid);
    if (it == nodes_.end()) return Rect{};
    const Node* node = it->second.get();

    if (node->parentId.empty()) {
        return node->rect;
    }

    auto parentIt = nodes_.find(node->parentId);
    if (parentIt == nodes_.end()) return node->rect;

    Rect parentWorld = computeWorldRect(node->parentId);
    Rect world;
    world.x = parentWorld.x + node->rect.x;
    world.y = parentWorld.y + node->rect.y;
    world.width = node->rect.width;
    world.height = node->rect.height;
    return world;
}

} // namespace ic
