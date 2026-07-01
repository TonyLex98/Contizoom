#ifndef IC_STORAGE_H
#define IC_STORAGE_H

#include <string>
#include <memory>
#include <vector>

namespace ic {

class Canvas;
class Node;

class IStorage {
public:
    virtual ~IStorage() = default;

    // Save entire canvas: writes config.json + all node files
    virtual bool saveCanvas(const Canvas& canvas, const std::string& path) = 0;

    // Load canvas from disk
    virtual bool loadCanvas(Canvas& canvas, const std::string& path) = 0;

    // Get format version
    virtual std::pair<int, int> getFormatVersion(const std::string& path) const = 0;

    // Check if path is a valid project
    virtual bool isValid(const std::string& path) const = 0;

    // List all saved projects in a directory
    virtual std::vector<std::string> listCanvases(const std::string& dir) const = 0;

    // Save a single node: writes nodes/{uuid}.json
    virtual bool saveNode(const Node* node, const std::string& canvasPath) = 0;
};

} // namespace ic

#endif // IC_STORAGE_H
