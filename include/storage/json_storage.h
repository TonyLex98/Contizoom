#ifndef IC_JSON_STORAGE_H
#define IC_JSON_STORAGE_H

#include "storage/storage.h"
#include <string>
#include <vector>

namespace ic {

// JSON-based storage with format versioning and migration.
//
// File structure:
//   project.canvas/
//   ├── index.json      (metadata, nodes tree, connections)
//   ├── manifest.json   (format version, canvas info)
//   └── nodes/
//       ├── {uuid}.txt  (text content of each node)
//       └── ...
//
// Manifest format:
//   {
//     "format_version": "1.0",
//     "canvas_name": "My Project",
//     "created_at": 1234567890,
//     "modified_at": 1234567890
//   }
//
// Migration:
//   If loaded format_version < current IC_STORAGE_FORMAT,
//   the storage will attempt to migrate to the latest version.
//   Migration history is kept in index.json under "migration_history".

class JsonStorage : public IStorage {
public:
    JsonStorage() = default;

    bool saveCanvas(const Canvas& canvas, const std::string& path) override;
    bool loadCanvas(Canvas& canvas, const std::string& path) override;
    std::pair<int, int> getFormatVersion(const std::string& path) const override;
    bool isValid(const std::string& path) const override;
    std::vector<std::string> listCanvases(const std::string& dir) const override;

    // Save just the node text file
    bool saveNodeText(const std::string& canvasPath, const std::string& uuid,
                      const std::string& text);

    // Incremental node save (metadata only)
    bool saveNode(const Node* node, const std::string& canvasPath) override;

    // Load just the node text file
    std::string loadNodeText(const std::string& canvasPath,
                             const std::string& uuid) const;

private:
    // Internal migration functions
    // Currently only 1.0 -> 1.0 (no migration needed)
    // Future: migrate_1_0_to_1_1, migrate_1_1_to_2_0, etc.
    bool migrateToCurrent(const std::string& path,
                          std::pair<int, int> fromVersion);

    // Parse version string "1.2" -> {1, 2}
    static std::pair<int, int> parseVersion(const std::string& str);

    // Build path to nodes directory
    static std::string nodesDir(const std::string& canvasPath);

    // Build path to node text file
    static std::string nodeTextPath(const std::string& canvasPath,
                                    const std::string& uuid);

    // Build path to manifest
    static std::string manifestPath(const std::string& canvasPath);
};

} // namespace ic

#endif // IC_JSON_STORAGE_H
