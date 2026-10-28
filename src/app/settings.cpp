#include "app/settings.h"
#include "app/project_manager.h"
#include "util/file.h"
#include "util/json.h"
#include "util/path.h"

namespace ic {

std::string Settings::path() {
    return path::toUtf8(path::fromUtf8(ProjectManager::defaultProjectsDir()).parent_path() / "settings.json");
}

Settings Settings::load() {
    Settings s;
    std::string text;
    if (!file::read(path::fromUtf8(path()), text)) return s;
    try {
        const json::Value v = json::parse(text);
        s.language = v.getString("language");
        s.sidebarWidth = v.getInt("sidebar_width", s.sidebarWidth);
    } catch (const json::ParseError&) {
        // A broken settings file just means defaults.
    }
    return s;
}

bool Settings::save() const {
    json::Value v = json::Value::object();
    v.set("language", language);
    v.set("sidebar_width", sidebarWidth);
    return file::write(path::fromUtf8(path()), json::serialize(v));
}

}  // namespace ic
