#ifndef IC_CONNECTION_H
#define IC_CONNECTION_H

#include <string>
#include <utility>

namespace ic {

enum class ConnType {
    Arrow,        // A -> B
    Line,         // A - B
    DottedArrow,  // A .> B
    DottedLine,   // A .. B
    DashedArrow,  // A --> B
    DashedLine    // A -- B
};

inline bool isArrow(ConnType t) {
    return t == ConnType::Arrow || t == ConnType::DottedArrow || t == ConnType::DashedArrow;
}

// Anchor points sit on a box's left, right and bottom edges (the top row is
// the name label). Each edge has 3 points at 1/4, 1/2 and 3/4 of its length.
enum class AnchorEdge { None = -1, Left = 0, Right = 1, Bottom = 2 };

struct Anchor {
    AnchorEdge edge = AnchorEdge::None;
    int index = -1;  // 0..2 along the edge

    bool valid() const { return edge != AnchorEdge::None && index >= 0 && index <= 2; }
    bool operator==(const Anchor& o) const { return edge == o.edge && index == o.index; }
    bool operator!=(const Anchor& o) const { return !(*this == o); }
};

// A directed link between two boxes. Endpoints are stored as anchor
// fractions rather than pixel offsets so they stay on the same edge point
// however the boxes move, resize or the view zooms; an invalid anchor means
// "the box's center".
class Connection {
public:
    std::string uuid;
    std::string name;
    std::string fromUuid;
    std::string toUuid;
    ConnType type = ConnType::Arrow;
    Anchor fromAnchor;
    Anchor toAnchor;

    Connection() = default;
    Connection(std::string uuid_, std::string from, std::string to, ConnType t = ConnType::Arrow)
        : uuid(std::move(uuid_)), fromUuid(std::move(from)), toUuid(std::move(to)), type(t) {}
};

}  // namespace ic

#endif  // IC_CONNECTION_H
