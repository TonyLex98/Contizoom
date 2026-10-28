#ifndef IC_CANVAS_H
#define IC_CANVAS_H

#include "model/connection.h"
#include "model/node.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ic {

// Camera over the canvas: which world point is at the center of the canvas
// area and how many screen cells one world unit takes.
struct Viewport {
    double centerX = 0.0;
    double centerY = 0.0;
    double scale = 1.0;

    static constexpr double kMinScale = 1e-6;
    static constexpr double kMaxScale = 1e6;

    double left(double viewWidth) const { return centerX - viewWidth / (2.0 * scale); }
    double right(double viewWidth) const { return centerX + viewWidth / (2.0 * scale); }
    double top(double viewHeight) const { return centerY - viewHeight / (2.0 * scale); }
    double bottom(double viewHeight) const { return centerY + viewHeight / (2.0 * scale); }
};

// The document model: a forest of nested boxes plus connections between
// them. Canvas owns the hierarchy invariants (parent/child links, depth, no
// cycles) and batches change notifications for auto-save. It knows nothing
// about selection, hover or editing - that lives in UiState.
class Canvas {
public:
    Canvas() = default;

    // ---- Nodes -------------------------------------------------------
    // `rect` is relative to the parent (world coordinates for roots).
    // Returns "" if `parentId` is given but doesn't exist.
    std::string createNode(const std::string& text, const Rect& rect, const std::string& name = "",
                           const std::string& parentId = "");

    // Inserts a fully built node (used by the loader). Call rebuildHierarchy()
    // once everything is inserted.
    void insertNode(std::shared_ptr<Node> node);

    // Removes a node and its whole subtree, plus every connection touching
    // any removed node.
    bool removeSubtree(const std::string& uuid);

    std::shared_ptr<Node> getNode(const std::string& uuid) const;
    std::vector<std::shared_ptr<Node>> getDescendants(const std::string& uuid) const;
    std::vector<std::shared_ptr<Node>> getChildren(const std::string& parentUuid) const;  // "" = roots
    std::vector<std::shared_ptr<Node>> getVisibleNodes(const Viewport& vp, double viewWidth,
                                                       double viewHeight) const;

    // True if `ancestor` is `uuid` itself or one of its ancestors.
    bool isAncestorOf(const std::string& ancestor, const std::string& uuid) const;

    // ---- Hierarchy ---------------------------------------------------
    // Moves a node under a new parent ("" = make it a root) while keeping its
    // world position. Refuses to create a cycle (moving a node into its own
    // subtree). Depth of the whole moved subtree is updated.
    bool reparent(const std::string& uuid, const std::string& newParentUuid);

    // Tab: lifts a node one level up, next to its current parent.
    bool promoteNode(const std::string& uuid);

    // Shift+Tab: nests a node inside the sibling just before it (by reading
    // order: top to bottom, then left to right), below that sibling's
    // existing children.
    bool demoteNode(const std::string& uuid);

    // Wraps nodes that share a parent into a new framed box. Returns the
    // frame's uuid, or "" if the nodes don't all have the same parent.
    std::string createFrame(const std::vector<std::string>& nodeUuids, const std::string& label = "Frame");

    // ---- Layout ------------------------------------------------------
    void expandParentToFit(const std::string& nodeUuid, double padding = 2.0);
    bool resolveOverlaps(const std::string& parentUuid);
    void autoLayout();

    // Box position in world coordinates (parent offsets summed up).
    Rect computeWorldRect(const std::string& uuid) const;

    // ---- Connections -------------------------------------------------
    // Returns "" if either endpoint is missing or both are the same node.
    std::string createConnection(const std::string& fromUuid, const std::string& toUuid,
                                 const std::string& name = "", ConnType type = ConnType::Arrow);
    void insertConnection(std::shared_ptr<Connection> connection);
    bool removeConnection(const std::string& connUuid);
    std::shared_ptr<Connection> getConnection(const std::string& uuid) const;

    // ---- Document ----------------------------------------------------
    Viewport viewport;
    std::string canvasName;
    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    void clear();

    // Recomputes children lists and depths from parentId links. Detaches
    // nodes whose parent is missing or that would form a cycle (both can
    // only come from damaged files).
    void rebuildHierarchy();

    const std::map<std::string, std::shared_ptr<Node>>& allNodes() const { return nodes_; }
    const std::map<std::string, std::shared_ptr<Connection>>& allConnections() const { return connections_; }

    // ---- Change tracking (auto-save) ---------------------------------
    // The callback receives the uuids of nodes whose own data changed and
    // whether canvas-level data (viewport, node set, connections) changed.
    // It fires from processPendingChanges() once edits have settled for the
    // throttle delay, or immediately from flushChanges().
    using ChangedCallback = std::function<void(const std::set<std::string>& changedUuids, bool canvasChanged)>;
    void onChanged(ChangedCallback cb) { changed_ = std::move(cb); }

    void markChanged(const std::string& uuid);
    void markCanvasChanged();
    bool hasPendingChanges() const { return canvasChanged_ || !changedNodes_.empty(); }
    void processPendingChanges();
    void flushChanges();
    void discardPendingChanges() {
        changedNodes_.clear();
        canvasChanged_ = false;
    }

private:
    std::map<std::string, std::shared_ptr<Node>> nodes_;
    std::map<std::string, std::shared_ptr<Connection>> connections_;

    ChangedCallback changed_;
    std::set<std::string> changedNodes_;
    bool canvasChanged_ = false;
    std::chrono::steady_clock::time_point lastChangeTime_;
    std::chrono::milliseconds throttleDelay_{500};

    void touch();
    void notifyChanged(bool force);
    void updateDepths(const std::string& uuid);
    void detachFromParent(Node& node);
};

}  // namespace ic

#endif  // IC_CANVAS_H
