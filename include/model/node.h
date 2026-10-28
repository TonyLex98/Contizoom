#ifndef IC_NODE_H
#define IC_NODE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ic {

// Axis-aligned rectangle in canvas units.
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 100.0;
    double height = 50.0;

    double left() const { return x; }
    double right() const { return x + width; }
    double top() const { return y; }
    double bottom() const { return y + height; }
    double centerX() const { return x + width / 2.0; }
    double centerY() const { return y + height / 2.0; }

    bool contains(double px, double py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    bool intersects(const Rect& other) const {
        return !(right() < other.left() || left() > other.right() || bottom() < other.top() ||
                 top() > other.bottom());
    }
};

// A box on the canvas. Boxes nest: a child's `rect` is relative to its
// parent's top-left corner, so moving a parent moves its whole subtree.
// Root boxes (empty parentId) are positioned in world coordinates.
class Node {
public:
    std::string uuid;
    std::string name;  // header label
    std::string text;  // body, may span several lines
    std::string parentId;
    std::vector<std::string> childrenUuids;
    Rect rect;

    // Nesting depth (0 for roots). Derived from parentId and kept up to date
    // by Canvas; it's never read back from disk.
    int depth = 0;

    std::string tag;
    bool hasFrame = false;

    uint64_t createdAt = 0;
    uint64_t modifiedAt = 0;

    Node() = default;
    Node(std::string uuid_, std::string text_, const Rect& rect_)
        : uuid(std::move(uuid_)), text(std::move(text_)), rect(rect_) {}

    // Grows the box (never shrinks it) so its text fits, wrapping lines
    // longer than `charsPerLine`. Widths are measured in characters, not
    // bytes, so non-ASCII text isn't over-measured.
    void autoResize(int charsPerLine = 80);
};

}  // namespace ic

#endif  // IC_NODE_H
