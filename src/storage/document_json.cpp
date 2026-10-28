#include "storage/document_json.h"
#include "version/version.h"

#include <algorithm>

namespace ic::doc {

namespace {

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

}  // namespace

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

json::Value connectionToJson(const Connection& c) {
    json::Value v = json::Value::object();
    v.set("uuid", c.uuid);
    v.set("name", c.name);
    v.set("fromUuid", c.fromUuid);
    v.set("toUuid", c.toUuid);
    v.set("type", static_cast<int>(c.type));
    v.set("fromAnchorEdge", static_cast<int>(c.fromAnchor.edge));
    v.set("fromAnchorIndex", c.fromAnchor.index);
    v.set("toAnchorEdge", static_cast<int>(c.toAnchor.edge));
    v.set("toAnchorIndex", c.toAnchor.index);
    return v;
}

std::shared_ptr<Connection> connectionFromJson(const json::Value& v) {
    if (!v.isObject()) return nullptr;
    const std::string from = v.getString("fromUuid");
    const std::string to = v.getString("toUuid");
    if (from.empty() || to.empty()) return nullptr;

    std::string uuid = v.getString("uuid");
    if (uuid.empty()) uuid = from + "-" + to;
    const int type = v.getInt("type", 0);
    const ConnType t = type >= 0 && type <= static_cast<int>(ConnType::DashedLine) ? static_cast<ConnType>(type)
                                                                                    : ConnType::Arrow;
    auto conn = std::make_shared<Connection>(uuid, from, to, t);
    conn->name = v.getString("name");
    conn->fromAnchor = readAnchor(v, "fromAnchorEdge", "fromAnchorIndex");
    conn->toAnchor = readAnchor(v, "toAnchorEdge", "toAnchorIndex");
    return conn;
}

json::Value configToJson(const Canvas& canvas) {
    json::Value cfg = json::Value::object();
    cfg.set("format", IC_STORAGE_FORMAT_STRING);
    cfg.set("app_version", IC_VERSION_STRING);
    cfg.set("canvas_name", canvas.canvasName);
    cfg.set("created_at", canvas.createdAt);
    cfg.set("modified_at", canvas.modifiedAt);
    cfg.set("node_count", static_cast<uint64_t>(canvas.allNodes().size()));

    json::Value vp = json::Value::object();
    vp.set("centerX", canvas.viewport.centerX);
    vp.set("centerY", canvas.viewport.centerY);
    vp.set("textScale", canvas.viewport.scale);
    cfg.set("viewport", vp);

    json::Value conns = json::Value::array();
    for (const auto& [_, c] : canvas.allConnections()) conns.push(connectionToJson(*c));
    cfg.set("connections", conns);
    return cfg;
}

json::Value contentToJson(const Canvas& canvas) {
    json::Value nodes = json::Value::array();
    for (const auto& [_, n] : canvas.allNodes()) nodes.push(nodeToJson(*n));
    json::Value conns = json::Value::array();
    for (const auto& [_, c] : canvas.allConnections()) conns.push(connectionToJson(*c));

    json::Value v = json::Value::object();
    v.set("canvas_name", canvas.canvasName);
    v.set("nodes", nodes);
    v.set("connections", conns);
    return v;
}

void contentFromJson(const json::Value& v, std::vector<std::shared_ptr<Node>>& nodes,
                     std::vector<std::shared_ptr<Connection>>& connections) {
    nodes.clear();
    connections.clear();
    if (const json::Value* arr = v.find("nodes"); arr && arr->isArray()) {
        for (const auto& n : arr->asArray()) {
            if (auto node = nodeFromJson(n)) nodes.push_back(node);
        }
    }
    if (const json::Value* arr = v.find("connections"); arr && arr->isArray()) {
        for (const auto& c : arr->asArray()) {
            if (auto conn = connectionFromJson(c)) connections.push_back(conn);
        }
    }
}

}  // namespace ic::doc
