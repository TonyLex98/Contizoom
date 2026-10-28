#ifndef IC_SETTINGS_H
#define IC_SETTINGS_H

#include <string>

namespace ic {

// User preferences, stored in ~/.infinite_canvas/settings.json next to the
// projects folder. Unknown fields are ignored and missing ones keep their
// defaults, so the file stays compatible across versions.
struct Settings {
    std::string language;  // language code ("ru"); empty = follow the system

    static std::string path();
    static Settings load();
    bool save() const;
};

}  // namespace ic

#endif  // IC_SETTINGS_H
