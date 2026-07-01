#ifndef IC_CANVAS_H
#define IC_CANVAS_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "model/node.h"
#include "model/connection.h"
#include "model/selection.h"
#include "util/uuid.h"

namespace ic {

// Viewport state: position, zoom level, text scale
struct Viewport {
    double centerX = 0.0;
    double centerY = 0.0;
    int zoomLevel = 0;        // Hierarchical level: 0=root, 1=children, etc.
    double textScale = 1.0;   // Continuous text scaling (0.1 to 5.0)

    double left(double viewWidth)   const { return centerX - viewWidth / (2.0 * textScale); }
    double right(double viewWidth)  const { return centerX + viewWidth / (2.0 * textScale); }
    double top(double viewHeight)   const { return centerY - viewHeight / (2.0 * textScale); }
    double bottom(double viewHeight) const { return centerY + viewHeight / (2.0 * textScale); }

    double screenToLogicalX(double screenX, double viewWidth) const {
        return centerX + (screenX - viewWidth / 2.0) / textScale;
    }
    double screenToLogicalY(double screenY, double viewHeight) const {
        return centerY + (screenY - viewHeight / 2.0) / textScale;
    }
};

// Inline editing overlay state (view-related, stored in canvas for renderer access)
struct EditOverlay {
    bool active = false;
    std::string nodeUuid;
    std::string text;
    Rect nodeRect;
    int cursorCol = 0;
    int cursorRow = 0;
};

// The canvas is the root model holding all nodes and connections
class Canvas {
public:
    Canvas() = default;

    // ====================================================================
    // Node management
    // ====================================================================
    // Create a new node and return its UUID
    std::string createNode(const std::string& text, const Rect& rect,
                            const std::string& name = "",
                            const std::string& parentId = "", int level = 0);

    // Remove a node and all its descendants (or just the node)
    bool removeNode(const std::string& uuid, bool recursive = false);

    // Get node by UUID
    std::shared_ptr<Node> getNode(const std::string& uuid) const;

    // Get all descendants of a node (children, grandchildren, etc.)
    std::vector<std::shared_ptr<Node>> getDescendants(const std::string& uuid) const;

    // Expand parent bounds if child exceeds them (recursive up to root)
    void expandParentToFit(const std::string& nodeUuid, double padding = 2.0);

    // Prevent overlap: snap node to avoid intersecting siblings at same level
    void preventOverlap(const std::string& nodeUuid);

    // Get all nodes at a specific zoom level
    std::vector<std::shared_ptr<Node>> getNodesAtLevel(int level) const;

    // Get visible nodes within the current viewport
    std::vector<std::shared_ptr<Node>> getVisibleNodes(const Viewport& vp,
                                                        double viewWidth,
                                                        double viewHeight) const;

    // Find node at a given logical position (topmost first)
    std::shared_ptr<Node> findNodeAt(double x, double y) const;

    // Find all nodes within a rectangular region
    std::vector<std::shared_ptr<Node>> findNodesInRect(double x1, double y1,
                                                        double x2, double y2) const;

    // ====================================================================
    // Connection management
    // ====================================================================
    std::string createConnection(const std::string& fromUuid,
                                   const std::string& toUuid,
                                   const std::string& name = "",
                                   ConnType type = ConnType::Arrow);

    bool removeConnection(const std::string& connUuid);

    std::shared_ptr<Connection> getConnection(const std::string& uuid) const;

    // Get all connections involving a specific node
    std::vector<std::shared_ptr<Connection>> getNodeConnections(
        const std::string& nodeUuid) const;

    // ====================================================================
    // Hierarchy operations
    // ====================================================================
    // Promote node up one level (move to parent's parent level)
    bool promoteNode(const std::string& uuid);

    // Demote node down one level (become child of another node)
    bool demoteNode(const std::string& uuid, const std::string& newParentUuid);

    // Set node's zoom level directly
    void setNodeLevel(const std::string& uuid, int level);

    // Transfer node to a new parent, preserving relative coordinates.
    // PlacementMode controls where the node is positioned inside the new parent.
    enum class PlacementMode { Free, AutoRight, AutoBottom, Center };
    bool transferNode(const std::string& nodeUuid, const std::string& newParentUuid,
                      PlacementMode mode = PlacementMode::AutoBottom);

    // Resolve ALL sibling overlaps recursively (X+Y axes).
    // Returns true if any overlap was resolved.
    bool resolveOverlaps(const std::string& parentUuid);

    // Realtime push: when dragging, push siblings out of the way
    void pushSiblings(const std::string& nodeUuid, double margin = 1.0);

    // Auto-layout: expand all parents to fit children + resolve all overlaps
    void autoLayout();

    // ====================================================================
    // Frame operations
    // ====================================================================
    // Create a frame around selected nodes
    std::string createFrame(const std::vector<std::string>& nodeUuids,
                            const std::string& label = "Frame");

    // ====================================================================
    // Viewport
    // ====================================================================
    Viewport viewport_;
    Selection selection_;
    EditOverlay editOverlay_;

    // ====================================================================
    // Meta
    // ====================================================================
    std::string canvasName;
    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    // Callback when canvas changes (without node info)
    using ChangedCallback = std::function<void()>;
    void onChanged(ChangedCallback cb) { changed_ = cb; }

    // Callback when a specific node changes
    using NodeChangedCallback = std::function<void(const std::string& nodeUuid)>;
    void onNodeChanged(NodeChangedCallback cb) { nodeChanged_ = cb; }

    // Notify that a specific node changed (triggers incremental save)
    void notifyChanged(const std::string& uuid);

    // Get all nodes (for serialization)
    const std::map<std::string, std::shared_ptr<Node>>& allNodes() const { return nodes_; }
    const std::map<std::string, std::shared_ptr<Connection>>& allConnections() const { return connections_; }

    // Compute absolute world rect from relative coordinates
    // All node.rect values are relative to parent; this walks the chain up to root.
    Rect computeWorldRect(const std::string& uuid) const;

private:
    std::map<std::string, std::shared_ptr<Node>> nodes_;
    std::map<std::string, std::shared_ptr<Connection>> connections_;
    ChangedCallback changed_;
    NodeChangedCallback nodeChanged_;

    void notifyChanged();
};

} // namespace ic

#endif // IC_CANVAS_H
