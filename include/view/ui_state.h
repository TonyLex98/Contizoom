#ifndef IC_UI_STATE_H
#define IC_UI_STATE_H

#include "model/connection.h"
#include "model/node.h"

#include <algorithm>
#include <set>
#include <string>

namespace ic {

// How a box is highlighted. Derived from UiState, not stored on the node.
enum class NodeState { Normal, Hovered, Selected, InConnection };

// Rubber-band selection rectangle, in world coordinates.
struct AreaSelection {
    bool active = false;
    double startX = 0.0, startY = 0.0;
    double endX = 0.0, endY = 0.0;

    void begin(double x, double y) {
        active = true;
        startX = endX = x;
        startY = endY = y;
    }
    void update(double x, double y) {
        if (!active) return;
        endX = x;
        endY = y;
    }
    void clear() { *this = AreaSelection{}; }

    double left() const { return std::min(startX, endX); }
    double right() const { return std::max(startX, endX); }
    double top() const { return std::min(startY, endY); }
    double bottom() const { return std::max(startY, endY); }
};

// In-place editor for a node's name (header) or text (body).
struct NodeEditor {
    bool active = false;
    bool editingName = false;
    std::string nodeUuid;
    std::string text;
    int cursorCol = 0;  // in code points
    int cursorRow = 0;
};

// In-place editor for a connection's label, drawn at a screen position
// since connections have no box of their own.
struct ConnectionEditor {
    bool active = false;
    std::string connUuid;
    std::string text;
    int cursorCol = 0;
    int screenX = 0;
    int screenY = 0;
};

enum class ResizeCorner { None, TopLeft, TopRight, BottomLeft, BottomRight };

// Transient editor state shared between the input handler (which mutates
// it) and the renderer (which draws it). Kept apart from the Canvas so the
// document model stays free of view concerns.
struct UiState {
    std::set<std::string> selected;
    std::string hovered;
    std::string connectionSource;  // node a connection is being dragged from
    std::string selectedConnection;

    AreaSelection area;
    NodeEditor nodeEditor;
    ConnectionEditor connEditor;

    // Handle under the mouse, highlighted so it reads as grabbable.
    std::string cornerHoverNode;
    ResizeCorner cornerHover = ResizeCorner::None;
    std::string anchorHoverNode;
    Anchor anchorHover;

    bool isSelected(const std::string& uuid) const { return selected.count(uuid) > 0; }

    NodeState stateOf(const std::string& uuid) const {
        if (isSelected(uuid)) return NodeState::Selected;
        if (uuid == connectionSource) return NodeState::InConnection;
        if (uuid == hovered) return NodeState::Hovered;
        return NodeState::Normal;
    }

    void clearSelection() {
        selected.clear();
        selectedConnection.clear();
    }

    // Drops references to nodes/connections that no longer exist (after a
    // delete, revert or project switch).
    template <typename CanvasT>
    void prune(const CanvasT& canvas) {
        for (auto it = selected.begin(); it != selected.end();) {
            it = canvas.getNode(*it) ? std::next(it) : selected.erase(it);
        }
        if (!canvas.getNode(hovered)) hovered.clear();
        if (!canvas.getNode(connectionSource)) connectionSource.clear();
        if (!canvas.getNode(cornerHoverNode)) cornerHoverNode.clear();
        if (!canvas.getNode(anchorHoverNode)) anchorHoverNode.clear();
        if (!selectedConnection.empty() && !canvas.getConnection(selectedConnection)) selectedConnection.clear();
        if (nodeEditor.active && !canvas.getNode(nodeEditor.nodeUuid)) nodeEditor = NodeEditor{};
        if (connEditor.active && !canvas.getConnection(connEditor.connUuid)) connEditor = ConnectionEditor{};
    }
};

}  // namespace ic

#endif  // IC_UI_STATE_H
