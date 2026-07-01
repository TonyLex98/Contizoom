#ifndef IC_CONNECTION_H
#define IC_CONNECTION_H

#include <string>
#include <memory>
#include <map>

namespace ic {

class Node;

// Connection types
enum class ConnType {
    Arrow,        // Directed: A -> B
    Line,         // Undirected: A - B
    DottedArrow,  // A -.- B
    DottedLine,   // A . B
    DashedArrow,  // A --> B
    DashedLine    // A -- B
};

// Color index for console (0-7 for ANSI colors)
enum class ConnColor {
    Default = 0,
    Red = 1,
    Green = 2,
    Yellow = 3,
    Blue = 4,
    Magenta = 5,
    Cyan = 6,
    White = 7
};

class Connection {
public:
    std::string uuid;
    std::string name;           // Display name of the connection
    std::string fromUuid;
    std::string toUuid;

    // Can be UUID, relative path (./, ../), or label
    std::string label;
    ConnType type = ConnType::Arrow;
    ConnColor color = ConnColor::Default;

    // Offset from node center in pixels
    double fromOffsetX = 0.0;
    double fromOffsetY = 0.0;
    double toOffsetX = 0.0;
    double toOffsetY = 0.0;

    Connection() = default;

    Connection(const std::string& uuid_, const std::string& from,
               const std::string& to, ConnType t = ConnType::Arrow)
        : uuid(uuid_), fromUuid(from), toUuid(to), type(t) {}

    // Resolve relative references to absolute UUIDs
    void resolveRefs(const std::map<std::string, std::shared_ptr<Node>>& allNodes);
};

} // namespace ic

#endif // IC_CONNECTION_H
