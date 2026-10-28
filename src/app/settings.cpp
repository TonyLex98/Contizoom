#include "app/settings.h"
#include "app/project_manager.h"
#include "util/file.h"
#include "util/json.h"
#include "util/path.h"

#include <algorithm>
#include <filesystem>

namespace ic {

namespace {

// Paths compare case-insensitively and slash-agnostic on Windows.
std::string normalized(const std::string& p) {
    std::string s = path::toUtf8(path::fromUtf8(p).lexically_normal());
#ifdef _WIN32
    for (char& c : s) {
        if (c == '/') c = '\\';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
#endif
    while (s.size() > 1 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
}

}  // namespace

void Settings::touchRecent(const std::string& projectPath, const std::string& name, uint64_t when) {
    if (projectPath.empty()) return;
    const std::string key = normalized(projectPath);
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const RecentProject& r) { return normalized(r.path) == key; }),
                 recent.end());
    recent.insert(recent.begin(), RecentProject{projectPath, name, when});
    if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
}

std::vector<RecentProject> Settings::existingRecent() const {
    std::vector<RecentProject> out;
    for (const auto& r : recent) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path::fromUtf8(r.path) / "config.json", ec)) out.push_back(r);
    }
    return out;
}

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
        if (const json::Value* arr = v.find("recent"); arr && arr->isArray()) {
            for (const auto& r : arr->asArray()) {
                RecentProject rp{r.getString("path"), r.getString("name"), r.getUInt64("opened_at")};
                if (!rp.path.empty() && s.recent.size() < kMaxRecent) s.recent.push_back(rp);
            }
        }
    } catch (const json::ParseError&) {
        // A broken settings file just means defaults.
    }
    return s;
}

bool Settings::save() const {
    json::Value v = json::Value::object();
    v.set("language", language);
    v.set("sidebar_width", sidebarWidth);
    json::Value arr = json::Value::array();
    for (const auto& r : recent) {
        json::Value item = json::Value::object();
        item.set("path", r.path);
        item.set("name", r.name);
        item.set("opened_at", r.openedAt);
        arr.push(item);
    }
    v.set("recent", arr);
    return file::write(path::fromUtf8(path()), json::serialize(v));
}

}  // namespace ic
