#ifndef IC_STORAGE_H
#define IC_STORAGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace ic {

class Canvas;
class Node;

// What the "Open project" list shows without loading the whole project.
struct ProjectSummary {
    std::string path;
    std::string name;
    uint64_t modifiedAt = 0;
    int nodeCount = -1;  // -1 if unknown (older files)
};

// Persistence backend. Paths are UTF-8. On failure methods return false
// and lastError() describes what went wrong (for the status bar).
class IStorage {
public:
    virtual ~IStorage() = default;

    // Writes the whole project and removes files of nodes that no longer exist.
    virtual bool saveCanvas(const Canvas& canvas, const std::string& path) = 0;

    // Incremental writes used by auto-save.
    virtual bool saveConfig(const Canvas& canvas, const std::string& path) = 0;
    virtual bool saveNode(const Node& node, const std::string& path) = 0;
    virtual bool removeNode(const std::string& uuid, const std::string& path) = 0;

    // Replaces `canvas` contents with the project at `path`. On failure the
    // canvas is left untouched.
    virtual bool loadCanvas(Canvas& canvas, const std::string& path) = 0;

    virtual bool isValid(const std::string& path) const = 0;
    virtual bool readSummary(const std::string& path, ProjectSummary& out) const = 0;
    virtual std::vector<std::string> listCanvases(const std::string& dir) const = 0;

    virtual const std::string& lastError() const = 0;
};

}  // namespace ic

#endif  // IC_STORAGE_H
