#ifndef IC_STORAGE_H
#define IC_STORAGE_H

#include <string>
#include <vector>

namespace ic {

class Canvas;
class Node;

// Persistence backend. Paths are UTF-8. On failure methods return false
// and lastError() describes what went wrong (for the status bar).
class IStorage {
public:
    virtual ~IStorage() = default;

    // Writes the whole project: canvas config plus every node.
    virtual bool saveCanvas(const Canvas& canvas, const std::string& path) = 0;

    // Writes a single node (incremental auto-save).
    virtual bool saveNode(const Node& node, const std::string& canvasPath) = 0;

    // Replaces `canvas` contents with the project at `path`. On failure the
    // canvas is left untouched.
    virtual bool loadCanvas(Canvas& canvas, const std::string& path) = 0;

    virtual bool isValid(const std::string& path) const = 0;
    virtual std::vector<std::string> listCanvases(const std::string& dir) const = 0;

    virtual const std::string& lastError() const = 0;
};

}  // namespace ic

#endif  // IC_STORAGE_H
