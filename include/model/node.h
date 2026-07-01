#ifndef IC_NODE_H
#define IC_NODE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>

namespace ic {

// Position and size in logical coordinates
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 100.0;
    double height = 50.0;

    double left()   const { return x; }
    double right()  const { return x + width; }
    double top()    const { return y; }
    double bottom() const { return y + height; }
    double centerX() const { return x + width / 2.0; }
    double centerY() const { return y + height / 2.0; }

    bool contains(double px, double py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    bool intersects(const Rect& other) const {
        return !(right() < other.left() || left() > other.right() ||
                 bottom() < other.top() || top() > other.bottom());
    }
};

// Node color/attributes for console rendering
enum class NodeState {
    Normal,
    Selected,
    Hovered,
    Active,
    InConnection
};

// A single node on the infinite canvas.
// Each node contains text and may have children visible at deeper zoom levels.
class Node {
public:
    std::string uuid;
    std::string text;           // The text content of this node
    std::string parentId;       // Empty if root node
    std::vector<std::string> childrenUuids;
    Rect rect;                  // Position and size on the canvas

    int zoomLevel = 0;          // The zoom level at which this node is visible
    // At zoomLevel=0: root-level nodes
    // At zoomLevel=1: children of root, etc.
    // Negative zoom levels are allowed for overview

    NodeState state = NodeState::Normal;
    std::string tag;            // Optional tag for categorization
    bool hasFrame = false;      // Whether this node has a frame border

    // Creation/modification tracking
    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    Node() = default;

    Node(const std::string& uuid_, const std::string& text_,
         const Rect& rect_, int level = 0)
        : uuid(uuid_), text(text_), rect(rect_), zoomLevel(level) {}

    // Relative path navigation
    // Returns UUID of target node, empty if not found
    // Supports: "@uuid", "./sibling", "../parent/child", "../../ancestor"
    std::optional<std::string> resolvePath(const std::string& path,
                                           const std::map<std::string, std::shared_ptr<Node>>& allNodes) const;

    // Auto-resize based on text content (console: chars per line)
    void autoResize(int charsPerLine = 80);
};

} // namespace ic

#endif // IC_NODE_H
