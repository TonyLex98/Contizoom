#include "storage/json_storage.h"
#include "model/canvas.h"
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
            error = "write failed: " + path::toUtf8(tmp);
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

json::Value anchorEdgeValue(const Anchor& a) {
    return json::Value(static_cast<int>(a.edge));
}

Anchor readAnchor(const json::Value& obj, const char* edgeKey, const char* indexKey) {
    Anchor a;
    const int edge = obj.getInt(edgeKey, -1);
    const int index = obj.getInt(indexKey, -1);
    if (edge >= 0 && edge <= 2 && index >= 0 && index <= 2) {
        a.edge = static_cast<AnchorEdge>(edge);
        a.index = index;
    }
    return a;
}

json::Value nodeToJson(const Node& node) {
    json::Value v = json::Value::object();
    v.set("uuid", node.uuid);
    v.set("name", node.name);
    v.set("text", node.text);
    v.set("parentId", node.parentId);
    v.set("x", node.rect.x);
    v.set("y", node.rect.y);
    v.set("width", node.rect.width);
    v.set("height", node.rect.height);
    v.set("tag", node.tag);
    v.set("hasFrame", node.hasFrame);
    v.set("createdAt", node.createdAt);
    v.set("modifiedAt", node.modifiedAt);
    return v;
}

std::shared_ptr<Node> nodeFromJson(const json::Value& v) {
    if (!v.isObject()) return nullptr;
    const std::string uuid = v.getString("uuid");
    if (uuid.empty()) return nullptr;

    Rect rect;
    rect.x = v.getNumber("x", rect.x);
    rect.y = v.getNumber("y", rect.y);
    rect.width = std::max(1.0, v.getNumber("width", rect.width));
    rect.height = std::max(1.0, v.getNumber("height", rect.height));

    auto node = std::make_shared<Node>(uuid, v.getString("text"), rect);
    node->name = v.getString("name");
    node->parentId = v.getString("parentId");
    node->tag = v.getString("tag");
    node->hasFrame = v.getBool("hasFrame");
    node->createdAt = v.getUInt64("createdAt");
    node->modifiedAt = v.getUInt64("modifiedAt");
    return node;
}

json::Value configToJson(const Canvas& canvas) {
    json::Value cfg = json::Value::object();
    cfg.set("format", IC_STORAGE_FORMAT_STRING);
    cfg.set("canvas_name", canvas.canvasName);
    cfg.set("created_at", canvas.createdAt);
    cfg.set("modified_at", canvas.modifiedAt);

    json::Value vp = json::Value::object();
    vp.set("centerX", canvas.viewport.centerX);
    vp.set("centerY", canvas.viewport.centerY);
    vp.set("textScale", canvas.viewport.scale);
    cfg.set("viewport", vp);

    json::Value conns = json::Value::array();
    for (const auto& [_, c] : canvas.allConnections()) {
        json::Value cv = json::Value::object();
        cv.set("uuid", c->uuid);
        cv.set("name", c->name);
        cv.set("fromUuid", c->fromUuid);
        cv.set("toUuid", c->toUuid);
        cv.set("type", static_cast<int>(c->type));
        cv.set("fromAnchorEdge", anchorEdgeValue(c->fromAnchor));
        cv.set("fromAnchorIndex", c->fromAnchor.index);
        cv.set("toAnchorEdge", anchorEdgeValue(c->toAnchor));
        cv.set("toAnchorIndex", c->toAnchor.index);
        conns.push(cv);
    }
    cfg.set("connections", conns);
    return cfg;
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
    return writeFileAtomic(nodesDir(canvasPath) / (node.uuid + ".json"), json::serialize(nodeToJson(node)),
                           lastError_);
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

    // Remove files of nodes that were deleted since the last save.
    for (const auto& entry : fs::directory_iterator(nodesDir(canvasPath), ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        if (!liveFiles.count(path::toUtf8(entry.path().filename()))) {
            std::error_code rmErr;
            fs::remove(entry.path(), rmErr);
        }
    }

    // config.json last: its presence is what marks the directory as a
    // valid project, so it only appears once all nodes are on disk.
    return writeFileAtomic(configPath(canvasPath), json::serialize(configToJson(canvas)), lastError_);
}

// ====================================================================
// Load
// ====================================================================

bool JsonStorage::loadCanvas(Canvas& canvas, const std::string& canvasPath) {
    skippedNodeFiles_ = 0;

    std::string text;
    if (!readFile(configPath(canvasPath), text)) {
        lastError_ = "cannot open " + path::toUtf8(configPath(canvasPath));
        return false;
    }

    json::Value cfg;
    try {
        cfg = json::parse(text);
    } catch (const json::ParseError& e) {
        lastError_ = std::string(kConfigFile) + ": " + e.what();
        return false;
    }
    if (!cfg.isObject()) {
        lastError_ = std::string(kConfigFile) + ": not a JSON object";
        return false;
    }

    const auto [major, minor] = parseFormat(cfg);
    if (major != IC_STORAGE_FORMAT_MAJOR) {
        lastError_ = "unsupported project format " + cfg.getString("format", "?") + " (this build reads " +
                     std::to_string(IC_STORAGE_FORMAT_MAJOR) + ".x)";
        return false;
    }
    (void)minor;

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
                    node = nodeFromJson(json::parse(nodeText));
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
            const std::string from = cv.getString("fromUuid");
            const std::string to = cv.getString("toUuid");
            if (from.empty() || to.empty()) continue;
            std::string uuid = cv.getString("uuid");
            if (uuid.empty()) uuid = from + "-" + to;
            const int type = cv.getInt("type", 0);
            auto conn = std::make_shared<Connection>(
                uuid, from, to,
                type >= 0 && type <= static_cast<int>(ConnType::DashedLine) ? static_cast<ConnType>(type)
                                                                             : ConnType::Arrow);
            conn->name = cv.getString("name");
            conn->fromAnchor = readAnchor(cv, "fromAnchorEdge", "fromAnchorIndex");
            conn->toAnchor = readAnchor(cv, "toAnchorEdge", "toAnchorIndex");
            connections.push_back(conn);
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

    lastError_.clear();
    return true;
}

bool JsonStorage::isValid(const std::string& canvasPath) const {
    std::error_code ec;
    return fs::is_directory(path::fromUtf8(canvasPath), ec) && fs::is_regular_file(configPath(canvasPath), ec);
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
