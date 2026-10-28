#include "model/canvas.h"
#include "util/clock.h"
#include "util/uuid.h"

#include <algorithm>
#include <unordered_set>

namespace ic {

namespace {

// Vertical room a parent reserves above its children for its own text.
double textZoneHeight(const Node& parent) {
    int textLines = 1;
    for (const char c : parent.text) {
        if (c == '\n') ++textLines;
    }
    return textLines * 1.2 + 2.0;
}

// Reading order: top to bottom, then left to right.
bool readingOrder(const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
    if (a->rect.y != b->rect.y) return a->rect.y < b->rect.y;
    if (a->rect.x != b->rect.x) return a->rect.x < b->rect.x;
    return a->uuid < b->uuid;
}

}  // namespace

// ====================================================================
// Change tracking
// ====================================================================

void Canvas::touch() {
    modifiedAt = clock::unixSeconds();
}

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
    if (!hasPendingChanges() || !changed_) return;
    const auto elapsed = std::chrono::steady_clock::now() - lastChangeTime_;
    if (!force && elapsed < throttleDelay_) return;

    // Swap out first: the callback may trigger further changes.
    std::set<std::string> nodes;
    nodes.swap(changedNodes_);
    const bool canvas = canvasChanged_;
    canvasChanged_ = false;
    changed_(nodes, canvas);
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
}

// ====================================================================
// Hierarchy bookkeeping
// ====================================================================

void Canvas::rebuildHierarchy() {
    for (auto& [_, node] : nodes_) node->childrenUuids.clear();

    // Drop links to missing parents and break cycles.
    for (auto& [uuid, node] : nodes_) {
        if (node->parentId.empty()) continue;
        if (!nodes_.count(node->parentId)) {
            node->parentId.clear();
            continue;
        }
        // Walk up; if we come back to this node it's part of a cycle, so cut
        // its parent link. A cycle further up the chain that doesn't include
        // this node is broken when one of its own members is visited.
        std::unordered_set<std::string> seen{uuid};
        for (std::string p = node->parentId; !p.empty();) {
            if (p == uuid) {
                node->parentId.clear();
                break;
            }
            if (!seen.insert(p).second) break;
            auto it = nodes_.find(p);
            p = it != nodes_.end() ? it->second->parentId : std::string();
        }
    }

    for (auto& [uuid, node] : nodes_) {
        if (!node->parentId.empty()) nodes_.at(node->parentId)->childrenUuids.push_back(uuid);
    }
    for (auto& [uuid, node] : nodes_) {
        if (node->parentId.empty()) updateDepths(uuid);
    }
}

void Canvas::updateDepths(const std::string& uuid) {
    auto node = getNode(uuid);
    if (!node) return;
    auto parent = getNode(node->parentId);
    node->depth = parent ? parent->depth + 1 : 0;
    for (const auto& child : node->childrenUuids) updateDepths(child);
}

void Canvas::detachFromParent(Node& node) {
    if (node.parentId.empty()) return;
    if (auto parent = getNode(node.parentId)) {
        auto& children = parent->childrenUuids;
        children.erase(std::remove(children.begin(), children.end(), node.uuid), children.end());
        markChanged(parent->uuid);
    }
    node.parentId.clear();
}

bool Canvas::isAncestorOf(const std::string& ancestor, const std::string& uuid) const {
    if (ancestor.empty()) return false;
    std::string current = uuid;
    for (size_t guard = 0; !current.empty() && guard <= nodes_.size(); ++guard) {
        if (current == ancestor) return true;
        auto it = nodes_.find(current);
        if (it == nodes_.end()) return false;
        current = it->second->parentId;
    }
    return false;
}

// ====================================================================
// Nodes
// ====================================================================

std::string Canvas::createNode(const std::string& text, const Rect& rect, const std::string& name,
                               const std::string& parentId) {
    std::shared_ptr<Node> parent;
    if (!parentId.empty()) {
        parent = getNode(parentId);
        if (!parent) return "";
    }

    const uint64_t now = clock::unixSeconds();
    auto node = std::make_shared<Node>(Uuid::generate(), text, rect);
    node->name = name;
    node->parentId = parentId;
    node->depth = parent ? parent->depth + 1 : 0;
    node->createdAt = now;
    node->modifiedAt = now;
    node->autoResize();

    nodes_[node->uuid] = node;
    if (parent) {
        parent->childrenUuids.push_back(node->uuid);
        markChanged(parentId);
    }

    modifiedAt = now;
    markChanged(node->uuid);
    markCanvasChanged();
    return node->uuid;
}

void Canvas::insertNode(std::shared_ptr<Node> node) {
    if (!node || node->uuid.empty()) return;
    nodes_[node->uuid] = std::move(node);
}

bool Canvas::removeSubtree(const std::string& uuid) {
    auto node = getNode(uuid);
    if (!node) return false;

    detachFromParent(*node);

    std::set<std::string> removed{uuid};
    for (const auto& d : getDescendants(uuid)) removed.insert(d->uuid);
    for (const auto& r : removed) nodes_.erase(r);

    for (auto it = connections_.begin(); it != connections_.end();) {
        if (removed.count(it->second->fromUuid) || removed.count(it->second->toUuid)) it = connections_.erase(it);
        else ++it;
    }

    touch();
    markCanvasChanged();
    return true;
}

std::shared_ptr<Node> Canvas::getNode(const std::string& uuid) const {
    if (uuid.empty()) return nullptr;
    auto it = nodes_.find(uuid);
    return it != nodes_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Node>> Canvas::getDescendants(const std::string& uuid) const {
    std::vector<std::shared_ptr<Node>> result;
    auto root = getNode(uuid);
    if (!root) return result;

    std::vector<std::string> stack = root->childrenUuids;
    while (!stack.empty()) {
        auto child = getNode(stack.back());
        stack.pop_back();
        if (!child) continue;
        result.push_back(child);
        stack.insert(stack.end(), child->childrenUuids.begin(), child->childrenUuids.end());
    }
    return result;
}

std::vector<std::shared_ptr<Node>> Canvas::getChildren(const std::string& parentUuid) const {
    std::vector<std::shared_ptr<Node>> result;
    if (parentUuid.empty()) {
        for (const auto& [_, node] : nodes_) {
            if (node->parentId.empty()) result.push_back(node);
        }
    } else if (auto parent = getNode(parentUuid)) {
        for (const auto& cid : parent->childrenUuids) {
            if (auto child = getNode(cid)) result.push_back(child);
        }
    }
    std::sort(result.begin(), result.end(), readingOrder);
    return result;
}

std::vector<std::shared_ptr<Node>> Canvas::getVisibleNodes(const Viewport& vp, double viewWidth,
                                                           double viewHeight) const {
    // Generous margin so boxes partly off-screen (and connection routes to
    // them) still get laid out.
    const double margin = 50.0 / vp.scale;
    const double vl = vp.left(viewWidth) - margin;
    const double vr = vp.right(viewWidth) + margin;
    const double vt = vp.top(viewHeight) - margin;
    const double vb = vp.bottom(viewHeight) + margin;

    std::vector<std::shared_ptr<Node>> result;
    for (const auto& [uuid, node] : nodes_) {
        const Rect world = computeWorldRect(uuid);
        if (world.right() > vl && world.left() < vr && world.bottom() > vt && world.top() < vb) {
            result.push_back(node);
        }
    }
    return result;
}

Rect Canvas::computeWorldRect(const std::string& uuid) const {
    auto node = getNode(uuid);
    if (!node) return Rect{};

    Rect world = node->rect;
    std::string p = node->parentId;
    for (size_t guard = 0; !p.empty() && guard <= nodes_.size(); ++guard) {
        auto parent = getNode(p);
        if (!parent) break;
        world.x += parent->rect.x;
        world.y += parent->rect.y;
        p = parent->parentId;
    }
    return world;
}

// ====================================================================
// Hierarchy operations
// ====================================================================

bool Canvas::reparent(const std::string& uuid, const std::string& newParentUuid) {
    auto node = getNode(uuid);
    if (!node) return false;
    if (node->parentId == newParentUuid) return true;

    std::shared_ptr<Node> newParent;
    if (!newParentUuid.empty()) {
        newParent = getNode(newParentUuid);
        // Moving a node into itself or its own subtree would create a cycle.
        if (!newParent || isAncestorOf(uuid, newParentUuid)) return false;
    }

    const Rect world = computeWorldRect(uuid);
    const Rect origin = newParent ? computeWorldRect(newParentUuid) : Rect{0, 0, 0, 0};

    detachFromParent(*node);
    node->parentId = newParentUuid;
    if (newParent) {
        newParent->childrenUuids.push_back(uuid);
        markChanged(newParentUuid);
    }
    node->rect.x = world.x - origin.x;
    node->rect.y = world.y - origin.y;

    updateDepths(uuid);
    markChanged(uuid);
    touch();
    return true;
}

bool Canvas::promoteNode(const std::string& uuid) {
    auto node = getNode(uuid);
    if (!node || node->parentId.empty()) return false;
    auto parent = getNode(node->parentId);
    if (!parent) return false;

    const std::string grandparent = parent->parentId;
    if (!reparent(uuid, grandparent)) return false;

    // Land just to the right of the former parent instead of on top of it.
    constexpr double kGap = 3.0;
    node->rect.x = parent->rect.right() + kGap;
    node->rect.y = parent->rect.y;

    if (!grandparent.empty()) {
        expandParentToFit(uuid, 2.0);
        resolveOverlaps(grandparent);
    }
    return true;
}

bool Canvas::demoteNode(const std::string& uuid) {
    auto node = getNode(uuid);
    if (!node) return false;

    const auto siblings = getChildren(node->parentId);
    auto self = std::find(siblings.begin(), siblings.end(), node);
    if (self == siblings.end() || self == siblings.begin()) return false;
    const auto target = *(self - 1);

    if (!reparent(uuid, target->uuid)) return false;

    // Place below the target's existing content.
    constexpr double kGap = 2.0;
    double y = textZoneHeight(*target);
    for (const auto& cid : target->childrenUuids) {
        if (cid == uuid) continue;
        if (auto c = getNode(cid)) y = std::max(y, c->rect.bottom() + kGap);
    }
    node->rect.x = 3.0;
    node->rect.y = y;

    expandParentToFit(uuid, 2.0);
    resolveOverlaps(target->uuid);
    return true;
}

std::string Canvas::createFrame(const std::vector<std::string>& nodeUuids, const std::string& label) {
    std::vector<std::shared_ptr<Node>> members;
    for (const auto& u : nodeUuids) {
        if (auto n = getNode(u)) members.push_back(n);
    }
    if (members.empty()) return "";

    const std::string parentId = members.front()->parentId;
    for (const auto& m : members) {
        if (m->parentId != parentId) return "";
    }

    double minX = members.front()->rect.x, minY = members.front()->rect.y;
    double maxX = members.front()->rect.right(), maxY = members.front()->rect.bottom();
    for (const auto& m : members) {
        minX = std::min(minX, m->rect.x);
        minY = std::min(minY, m->rect.y);
        maxX = std::max(maxX, m->rect.right());
        maxY = std::max(maxY, m->rect.bottom());
    }

    constexpr double kPadding = 3.0;
    constexpr double kHeader = 2.0;
    Rect frameRect{minX - kPadding, minY - kPadding - kHeader, (maxX - minX) + 2 * kPadding,
                   (maxY - minY) + 2 * kPadding + kHeader};

    const std::string frameUuid = createNode("", frameRect, label, parentId);
    if (frameUuid.empty()) return "";
    auto frame = getNode(frameUuid);
    frame->hasFrame = true;

    for (const auto& m : members) reparent(m->uuid, frameUuid);

    if (!parentId.empty()) {
        expandParentToFit(frameUuid, 2.0);
        resolveOverlaps(parentId);
    }
    markCanvasChanged();
    return frameUuid;
}

// ====================================================================
// Layout
// ====================================================================

void Canvas::expandParentToFit(const std::string& nodeUuid, double padding) {
    auto node = getNode(nodeUuid);
    if (!node) return;
    auto parent = getNode(node->parentId);
    if (!parent) return;

    bool expanded = false;
    if (node->rect.right() + padding > parent->rect.width) {
        parent->rect.width = node->rect.right() + padding;
        expanded = true;
    }
    if (node->rect.bottom() + padding > parent->rect.height) {
        parent->rect.height = node->rect.bottom() + padding;
        expanded = true;
    }
    if (expanded) {
        markChanged(parent->uuid);
        expandParentToFit(parent->uuid, padding);
    }
}

bool Canvas::resolveOverlaps(const std::string& parentUuid) {
    auto parent = getNode(parentUuid);
    if (!parent || parent->childrenUuids.empty()) return false;

    const double textZoneY = textZoneHeight(*parent);
    constexpr double marginX = 3.0;
    constexpr double marginY = 2.0;
    const double gap = std::max(2.0, std::min(15.0, std::max(parent->rect.width, parent->rect.height) * 0.06));

    std::vector<std::shared_ptr<Node>> kids;
    for (const auto& cid : parent->childrenUuids) {
        if (auto c = getNode(cid)) kids.push_back(c);
    }

    bool resolved = false;
    auto clampInside = [&](Node& n) {
        if (n.rect.y < textZoneY) {
            n.rect.y = textZoneY;
            resolved = true;
        }
        if (n.rect.x < marginX) {
            n.rect.x = marginX;
            resolved = true;
        }
    };
    for (auto& k : kids) clampInside(*k);

    // Pairwise push-apart along the axis of least overlap. Bounded so a
    // pathological layout can't spin forever.
    for (int iter = 0; iter < 50; ++iter) {
        bool anyOverlap = false;
        for (size_t i = 0; i < kids.size(); ++i) {
            for (size_t j = i + 1; j < kids.size(); ++j) {
                Node& a = *kids[i];
                Node& b = *kids[j];
                const double half = gap / 2.0;
                if (a.rect.right() + half <= b.rect.left() - half || a.rect.left() - half >= b.rect.right() + half ||
                    a.rect.bottom() + half <= b.rect.top() - half || a.rect.top() - half >= b.rect.bottom() + half) {
                    continue;
                }

                const double overlapX = std::min(a.rect.right() - b.rect.left(), b.rect.right() - a.rect.left());
                const double overlapY = std::min(a.rect.bottom() - b.rect.top(), b.rect.bottom() - a.rect.top());
                if (overlapX < overlapY) {
                    const double push = (overlapX + gap) / 2.0;
                    const double dir = a.rect.centerX() < b.rect.centerX() ? 1.0 : -1.0;
                    a.rect.x -= dir * push;
                    b.rect.x += dir * push;
                } else {
                    const double push = (overlapY + gap) / 2.0;
                    const double dir = a.rect.centerY() < b.rect.centerY() ? 1.0 : -1.0;
                    a.rect.y -= dir * push;
                    b.rect.y += dir * push;
                }
                clampInside(a);
                clampInside(b);
                anyOverlap = true;
            }
        }
        if (!anyOverlap) break;
        resolved = true;
    }

    for (auto& k : kids) markChanged(k->uuid);

    double maxX = marginX, maxY = textZoneY;
    for (const auto& k : kids) {
        maxX = std::max(maxX, k->rect.right() + marginX);
        maxY = std::max(maxY, k->rect.bottom() + marginY);
    }
    if (maxX > parent->rect.width || maxY > parent->rect.height) {
        parent->rect.width = std::max(parent->rect.width, maxX);
        parent->rect.height = std::max(parent->rect.height, maxY);
        markChanged(parentUuid);
        resolved = true;
        if (!parent->parentId.empty()) resolveOverlaps(parent->parentId);
    }

    if (resolved) touch();
    return resolved;
}

void Canvas::autoLayout() {
    // Grow parents bottom-up so every child fits...
    std::vector<std::shared_ptr<Node>> children;
    for (const auto& [_, node] : nodes_) {
        if (!node->parentId.empty()) children.push_back(node);
    }
    std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) { return a->depth > b->depth; });
    for (const auto& c : children) expandParentToFit(c->uuid, 3.0);

    // ...then untangle siblings top-down.
    std::vector<std::shared_ptr<Node>> parents;
    for (const auto& [_, node] : nodes_) {
        if (!node->childrenUuids.empty()) parents.push_back(node);
    }
    std::sort(parents.begin(), parents.end(), [](const auto& a, const auto& b) { return a->depth < b->depth; });
    for (const auto& p : parents) resolveOverlaps(p->uuid);
}

// ====================================================================
// Connections
// ====================================================================

std::string Canvas::createConnection(const std::string& fromUuid, const std::string& toUuid,
                                     const std::string& name, ConnType type) {
    if (fromUuid == toUuid || !getNode(fromUuid) || !getNode(toUuid)) return "";

    auto conn = std::make_shared<Connection>(Uuid::generate(), fromUuid, toUuid, type);
    conn->name = name;
    connections_[conn->uuid] = conn;

    touch();
    markCanvasChanged();
    return conn->uuid;
}

void Canvas::insertConnection(std::shared_ptr<Connection> connection) {
    if (!connection || connection->uuid.empty()) return;
    connections_[connection->uuid] = std::move(connection);
}

bool Canvas::removeConnection(const std::string& connUuid) {
    if (connections_.erase(connUuid) == 0) return false;
    touch();
    markCanvasChanged();
    return true;
}

std::shared_ptr<Connection> Canvas::getConnection(const std::string& uuid) const {
    auto it = connections_.find(uuid);
    return it != connections_.end() ? it->second : nullptr;
}

}  // namespace ic
