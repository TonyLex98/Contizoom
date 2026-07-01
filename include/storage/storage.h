#ifndef IC_STORAGE_H
#define IC_STORAGE_H

#include <string>
#include <memory>
#include <vector>

namespace ic {

class Canvas;
class Node;

// Abstract storage interface
class IStorage {
public:
    virtual ~IStorage() = default;

    // Save entire canvas to disk
    virtual bool saveCanvas(const Canvas& canvas, const std::string& path) = 0;

    // Load canvas from disk (with automatic migration)
    virtual bool loadCanvas(Canvas& canvas, const std::string& path) = 0;

    // Get format version of a saved file (without loading)
    virtual std::pair<int, int> getFormatVersion(const std::string& path) const = 0;

    // Check if file exists and is valid
    virtual bool isValid(const std::string& path) const = 0;

    // List all saved canvases in a directory
    virtual std::vector<std::string> listCanvases(const std::string& dir) const = 0;

    // Save a single node (incremental save) — returns true if supported
    virtual bool saveNode(const Node* node, const std::string& canvasPath) = 0;
};

} // namespace ic

#endif // IC_STORAGE_H
