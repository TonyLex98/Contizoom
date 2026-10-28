#ifndef IC_JSON_STORAGE_H
#define IC_JSON_STORAGE_H

#include "storage/storage.h"

#include <string>
#include <vector>

namespace ic {

// One project = one directory; one node = one file, so auto-saving an edit
// rewrites a few hundred bytes instead of the whole document:
//
//   project.canvas/
//   ├── config.json      format version, name, viewport, connections
//   └── nodes/
//       ├── {uuid}.json  one box: name, text, parent, rect, ...
//       └── ...
//
// Every file is written to a temporary file first and then renamed over
// the old one, so a crash mid-save never leaves a half-written file.
class JsonStorage : public IStorage {
public:
    bool saveCanvas(const Canvas& canvas, const std::string& path) override;
    bool saveNode(const Node& node, const std::string& canvasPath) override;
    bool loadCanvas(Canvas& canvas, const std::string& path) override;
    bool isValid(const std::string& path) const override;
    std::vector<std::string> listCanvases(const std::string& dir) const override;
    const std::string& lastError() const override { return lastError_; }

    // Number of node files skipped as unreadable by the last loadCanvas().
    int skippedNodeFiles() const { return skippedNodeFiles_; }

private:
    std::string lastError_;
    int skippedNodeFiles_ = 0;
};

}  // namespace ic

#endif  // IC_JSON_STORAGE_H
