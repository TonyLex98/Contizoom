#ifndef IC_NODE_H
#define IC_NODE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>

namespace ic {

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

enum class NodeState {
    Normal,
    Selected,
    Hovered,
    Active,
    InConnection
};

class Node {
public:
    std::string uuid;
    std::string name;
    std::string text;
    std::string parentId;
    std::vector<std::string> childrenUuids;
    Rect rect;

    int zoomLevel = 0;
    NodeState state = NodeState::Normal;
    std::string tag;
    bool hasFrame = false;

    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    Node() = default;

    Node(const std::string& uuid_, const std::string& text_,
         const Rect& rect_, int level = 0)
        : uuid(uuid_), text(text_), rect(rect_), zoomLevel(level) {}

    std::optional<std::string> resolvePath(const std::string& path,
                                           const std::map<std::string, std::shared_ptr<Node>>& allNodes) const;

    void autoResize(int charsPerLine = 80);
};

} // namespace ic

#endif // IC_NODE_H
