#ifndef IC_JSON_STORAGE_H
#define IC_JSON_STORAGE_H

#include "storage/storage.h"
#include <string>
#include <vector>

namespace ic {

// One node = one file: nodes/{uuid}.json
// Canvas config: config.json (viewport, connections, canvas name)
//
// Directory structure:
//   project.canvas/
//   ├── config.json    (viewport, connections, canvas name)
//   └── nodes/
//       ├── {uuid}.json  (all node data including text)
//       └── ...

class JsonStorage : public IStorage {
public:
    JsonStorage() = default;

    bool saveCanvas(const Canvas& canvas, const std::string& path) override;
    bool loadCanvas(Canvas& canvas, const std::string& path) override;
    std::pair<int, int> getFormatVersion(const std::string& path) const override;
    bool isValid(const std::string& path) const override;
    std::vector<std::string> listCanvases(const std::string& dir) const override;

    // Save a single node to its own file: nodes/{uuid}.json
    bool saveNode(const Node* node, const std::string& canvasPath) override;

private:
    static std::string nodesDir(const std::string& canvasPath);
    static std::string nodeFilePath(const std::string& canvasPath, const std::string& uuid);
    static std::string configPath(const std::string& canvasPath);
};

} // namespace ic

#endif // IC_JSON_STORAGE_H
