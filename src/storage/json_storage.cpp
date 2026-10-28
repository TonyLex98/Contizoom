#include "storage/json_storage.h"
#include "model/canvas.h"
#include "storage/document_json.h"
#include "util/json.h"
#include "util/path.h"
#include "version/version.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace ic {
namespace fs = std::filesystem;

namespace {

const char* kConfigFile = "config.json";
const char* kNodesDir = "nodes";

fs::path configPath(const std::string& canvasPath) {
    return path::fromUtf8(canvasPath) / kConfigFile;
}

fs::path nodesDir(const std::string& canvasPath) {
    return path::fromUtf8(canvasPath) / kNodesDir;
}

fs::path nodePath(const std::string& canvasPath, const std::string& uuid) {
    return nodesDir(canvasPath) / path::fromUtf8(uuid + ".json");
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Write-to-temp-then-rename so readers only ever see a complete file.
bool writeFileAtomic(const fs::path& p, const std::string& content, std::string& error) {
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + path::toUtf8(tmp);
            return false;
        }
        out << content;
        out.flush();
        if (!out) {
            error = "write failed (disk full?): " + path::toUtf8(tmp);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        // Some platforms refuse to rename over an existing file.
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    if (ec) {
        error = "cannot replace " + path::toUtf8(p) + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// "1.1" -> {1, 1}; missing field means the original 1.0 format.
std::pair<int, int> parseFormat(const json::Value& cfg) {
    const std::string f = cfg.getString("format", "1.0");
    const size_t dot = f.find('.');
    try {
        return {std::stoi(f.substr(0, dot)), dot == std::string::npos ? 0 : std::stoi(f.substr(dot + 1))};
    } catch (const std::exception&) {
        return {-1, -1};
    }
}

bool readConfig(const std::string& canvasPath, json::Value& cfg, std::string& error) {
    std::string text;
    if (!readFile(configPath(canvasPath), text)) {
        error = "cannot open " + path::toUtf8(configPath(canvasPath));
        return false;
    }
    try {
        cfg = json::parse(text);
    } catch (const json::ParseError& e) {
        error = std::string(kConfigFile) + ": " + e.what();
        return false;
    }
    if (!cfg.isObject()) {
        error = std::string(kConfigFile) + ": not a JSON object";
        return false;
    }
    if (parseFormat(cfg).first != IC_STORAGE_FORMAT_MAJOR) {
        error = "unsupported project format " + cfg.getString("format", "?") + " (this build reads " +
                std::to_string(IC_STORAGE_FORMAT_MAJOR) + ".x)";
        return false;
    }
    return true;
}

}  // namespace

// ====================================================================
// Save
// ====================================================================

bool JsonStorage::saveNode(const Node& node, const std::string& canvasPath) {
    std::error_code ec;
    fs::create_directories(nodesDir(canvasPath), ec);
    if (ec) {
        lastError_ = "cannot create " + path::toUtf8(nodesDir(canvasPath)) + ": " + ec.message();
        return false;
    }
    return writeFileAtomic(nodePath(canvasPath, node.uuid), json::serialize(doc::nodeToJson(node)), lastError_);
}

bool JsonStorage::removeNode(const std::string& uuid, const std::string& canvasPath) {
    std::error_code ec;
    fs::remove(nodePath(canvasPath, uuid), ec);  // already gone is fine
    if (ec) {
        lastError_ = "cannot delete node file: " + ec.message();
        return false;
    }
    return true;
}

bool JsonStorage::saveConfig(const Canvas& canvas, const std::string& canvasPath) {
    std::error_code ec;
    fs::create_directories(path::fromUtf8(canvasPath), ec);
    if (ec) {
        lastError_ = "cannot create " + canvasPath + ": " + ec.message();
        return false;
    }
    return writeFileAtomic(configPath(canvasPath), json::serialize(doc::configToJson(canvas)), lastError_);
}

bool JsonStorage::saveCanvas(const Canvas& canvas, const std::string& canvasPath) {
    std::error_code ec;
    fs::create_directories(nodesDir(canvasPath), ec);
    if (ec) {
        lastError_ = "cannot create " + canvasPath + ": " + ec.message();
        return false;
    }

    std::set<std::string> liveFiles;
    for (const auto& [uuid, node] : canvas.allNodes()) {
        if (!saveNode(*node, canvasPath)) return false;
        liveFiles.insert(uuid + ".json");
    }

    // Remove files of nodes that were deleted since the last save, and any
    // temporary files a crash may have left behind.
    for (const auto& entry : fs::directory_iterator(nodesDir(canvasPath), ec)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = path::toUtf8(entry.path().filename());
        const bool stale = entry.path().extension() == ".tmp" ||
                           (entry.path().extension() == ".json" && !liveFiles.count(name));
        if (stale) {
            std::error_code rmErr;
            fs::remove(entry.path(), rmErr);
        }
    }

    // config.json last: its presence is what marks the directory as a
    // valid project, so it only appears once all nodes are on disk.
    return saveConfig(canvas, canvasPath);
}

// ====================================================================
// Load
// ====================================================================

bool JsonStorage::loadCanvas(Canvas& canvas, const std::string& canvasPath) {
    skippedNodeFiles_ = 0;

    json::Value cfg;
    if (!readConfig(canvasPath, cfg, lastError_)) return false;

    // Parse everything first; only touch `canvas` once nothing can fail.
    std::vector<std::shared_ptr<Node>> nodes;
    std::error_code ec;
    if (fs::is_directory(nodesDir(canvasPath), ec)) {
        for (const auto& entry : fs::directory_iterator(nodesDir(canvasPath), ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            std::string nodeText;
            std::shared_ptr<Node> node;
            if (readFile(entry.path(), nodeText)) {
                try {
                    node = doc::nodeFromJson(json::parse(nodeText));
                } catch (const json::ParseError&) {
                    node = nullptr;
                }
            }
            // A single damaged node file shouldn't make the whole project
            // unopenable; skip it and report the count.
            if (node) nodes.push_back(node);
            else ++skippedNodeFiles_;
        }
    }

    std::vector<std::shared_ptr<Connection>> connections;
    if (const json::Value* arr = cfg.find("connections"); arr && arr->isArray()) {
        for (const auto& cv : arr->asArray()) {
            if (auto conn = doc::connectionFromJson(cv)) connections.push_back(conn);
        }
    }

    canvas.clear();
    canvas.canvasName = cfg.getString("canvas_name");
    canvas.createdAt = cfg.getUInt64("created_at");
    canvas.modifiedAt = cfg.getUInt64("modified_at");
    canvas.viewport = Viewport{};
    if (const json::Value* vp = cfg.find("viewport"); vp && vp->isObject()) {
        canvas.viewport.centerX = vp->getNumber("centerX", 0.0);
        canvas.viewport.centerY = vp->getNumber("centerY", 0.0);
        const double scale = vp->getNumber("textScale", 1.0);
        canvas.viewport.scale = scale >= Viewport::kMinScale && scale <= Viewport::kMaxScale ? scale : 1.0;
    }

    for (auto& n : nodes) canvas.insertNode(n);
    canvas.rebuildHierarchy();
    for (auto& c : connections) {
        // Connections to nodes that didn't load would dangle.
        if (canvas.getNode(c->fromUuid) && canvas.getNode(c->toUuid)) canvas.insertConnection(c);
    }
    canvas.discardPendingChanges();

    lastError_.clear();
    return true;
}

bool JsonStorage::isValid(const std::string& canvasPath) const {
    std::error_code ec;
    return fs::is_directory(path::fromUtf8(canvasPath), ec) && fs::is_regular_file(configPath(canvasPath), ec);
}

bool JsonStorage::readSummary(const std::string& canvasPath, ProjectSummary& out) const {
    json::Value cfg;
    std::string error;
    if (!readConfig(canvasPath, cfg, error)) return false;
    out.path = canvasPath;
    out.name = cfg.getString("canvas_name");
    if (out.name.empty()) out.name = path::toUtf8(path::fromUtf8(canvasPath).stem());
    out.modifiedAt = cfg.getUInt64("modified_at");
    out.nodeCount = cfg.getInt("node_count", -1);
    return true;
}

std::vector<std::string> JsonStorage::listCanvases(const std::string& dir) const {
    std::vector<std::string> result;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(path::fromUtf8(dir), ec)) {
        if (entry.is_directory() && isValid(path::toUtf8(entry.path()))) result.push_back(path::toUtf8(entry.path()));
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace ic
