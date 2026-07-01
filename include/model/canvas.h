#ifndef IC_CANVAS_H
#define IC_CANVAS_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <chrono>
#include "model/node.h"
#include "model/connection.h"
#include "model/selection.h"
#include "util/uuid.h"

namespace ic {

struct Viewport {
    double centerX = 0.0;
    double centerY = 0.0;
    int zoomLevel = 0;
    double textScale = 1.0;

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

struct EditOverlay {
    bool active = false;
    std::string nodeUuid;
    std::string text;
    Rect nodeRect;
    int cursorCol = 0;
    int cursorRow = 0;
};

class Canvas {
public:
    Canvas() = default;

    // Node management
    std::string createNode(const std::string& text, const Rect& rect,
                            const std::string& name = "",
                            const std::string& parentId = "", int level = 0);

    bool removeNode(const std::string& uuid, bool recursive = false);
    std::shared_ptr<Node> getNode(const std::string& uuid) const;
    // Insert node with given UUID (used by loader, bypasses UUID generation)
    void insertNode(const std::string& uuid, std::shared_ptr<Node> node);
    std::vector<std::shared_ptr<Node>> getDescendants(const std::string& uuid) const;
    void expandParentToFit(const std::string& nodeUuid, double padding = 2.0);
    void preventOverlap(const std::string& nodeUuid);
    std::vector<std::shared_ptr<Node>> getNodesAtLevel(int level) const;
    std::vector<std::shared_ptr<Node>> getVisibleNodes(const Viewport& vp,
                                                        double viewWidth,
                                                        double viewHeight) const;
    std::shared_ptr<Node> findNodeAt(double x, double y) const;
    std::vector<std::shared_ptr<Node>> findNodesInRect(double x1, double y1,
                                                        double x2, double y2) const;

    // Connection management
    std::string createConnection(const std::string& fromUuid,
                                    const std::string& toUuid,
                                    const std::string& name = "",
                                    ConnType type = ConnType::Arrow);
    void insertConnection(const std::string& uuid, std::shared_ptr<Connection> connection);

    bool removeConnection(const std::string& connUuid);
    std::shared_ptr<Connection> getConnection(const std::string& uuid) const;
    std::vector<std::shared_ptr<Connection>> getNodeConnections(
        const std::string& nodeUuid) const;

    // Hierarchy operations
    bool promoteNode(const std::string& uuid);
    bool demoteNode(const std::string& uuid, const std::string& newParentUuid);
    void setNodeLevel(const std::string& uuid, int level);

    enum class PlacementMode { Free, AutoRight, AutoBottom, Center };
    bool transferNode(const std::string& nodeUuid, const std::string& newParentUuid,
                      PlacementMode mode = PlacementMode::AutoBottom);

    bool resolveOverlaps(const std::string& parentUuid);
    void pushSiblings(const std::string& nodeUuid, double margin = 1.0);
    void autoLayout();

    std::string createFrame(const std::vector<std::string>& nodeUuids,
                            const std::string& label = "Frame");

    Viewport viewport_;
    Selection selection_;
    EditOverlay editOverlay_;

    std::string canvasName;
    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    // Callback: triggered on change (throttled, called once after changes settle).
    // node UUIDs are saved incrementally; canvasChanged requires config/full save.
    using ChangedCallback = std::function<void(const std::set<std::string>& changedUuids,
                                               bool canvasChanged)>;
    void onChanged(ChangedCallback cb) { changed_ = cb; }

    // Set project path (used by auto-save to locate nodes directory)
    void setProjectPath(const std::string& path) { projectPath_ = path; }
    const std::string& projectPath() const { return projectPath_; }

    // Mark a node as changed (will be auto-saved after throttle)
    void markChanged(const std::string& uuid);

    // Mark canvas-level data as changed (viewport, connections, node set)
    void markCanvasChanged();

    // Process pending auto-save if the throttle delay has elapsed
    void processPendingChanges();

    // Force immediate save of all pending changes (bypass throttle)
    void flushChanges();

    void clear();
    void rebuildHierarchy();

    const std::map<std::string, std::shared_ptr<Node>>& allNodes() const { return nodes_; }
    const std::map<std::string, std::shared_ptr<Connection>>& allConnections() const { return connections_; }

    Rect computeWorldRect(const std::string& uuid) const;

private:
    std::map<std::string, std::shared_ptr<Node>> nodes_;
    std::map<std::string, std::shared_ptr<Connection>> connections_;
    ChangedCallback changed_;
    std::string projectPath_;

    // Throttled auto-save: collects changed UUIDs, fires callback after delay
    std::set<std::string> changedNodes_;
    bool canvasChanged_ = false;
    std::chrono::steady_clock::time_point lastChangeTime_;
    std::chrono::milliseconds throttleDelay_{500}; // 500ms throttle

    void notifyChanged(bool force = false);
};

} // namespace ic

#endif // IC_CANVAS_H
