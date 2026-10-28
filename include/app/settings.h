#ifndef IC_SETTINGS_H
#define IC_SETTINGS_H

#include <cstdint>
#include <string>
#include <vector>

namespace ic {

struct RecentProject {
    std::string path;
    std::string name;
    uint64_t openedAt = 0;  // unix seconds
};

// User preferences, stored in ~/.infinite_canvas/settings.json next to the
// projects folder. Unknown fields are ignored and missing ones keep their
// defaults, so the file stays compatible across versions.
struct Settings {
    static constexpr size_t kMaxRecent = 12;

    std::string language;   // language code ("ru"); empty = follow the system
    int sidebarWidth = 26;  // outline sidebar width in columns (1 = collapsed)
    std::vector<RecentProject> recent;  // most recent first

    // Moves `path` to the front of the recent list (adding it if needed).
    void touchRecent(const std::string& path, const std::string& name, uint64_t when);
    // Recent projects whose folder still exists, most recent first.
    std::vector<RecentProject> existingRecent() const;

    static std::string path();
    static Settings load();
    bool save() const;
};

}  // namespace ic

#endif  // IC_SETTINGS_H
