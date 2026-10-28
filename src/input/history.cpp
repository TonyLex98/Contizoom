#include "input/history.h"
#include "model/canvas.h"
#include "storage/document_json.h"
#include "util/json.h"

#include <map>

namespace ic {

namespace {

// uuid -> serialized item, for comparing two snapshots item by item.
std::map<std::string, std::string> itemsOf(const json::Value& doc, const char* key) {
    std::map<std::string, std::string> out;
    if (const json::Value* arr = doc.find(key); arr && arr->isArray()) {
        for (const auto& item : arr->asArray()) out[item.getString("uuid")] = json::serialize(item, 0);
    }
    return out;
}

// Boxes that differ between two documents and exist in `after`: changed
// boxes, plus both ends of every changed connection.
std::set<std::string> changedNodes(const json::Value& before, const json::Value& after) {
    std::set<std::string> changed;
    const auto nodesBefore = itemsOf(before, "nodes");
    for (const auto& [uuid, data] : itemsOf(after, "nodes")) {
        auto it = nodesBefore.find(uuid);
        if (it == nodesBefore.end() || it->second != data) changed.insert(uuid);
    }
    const auto connsBefore = itemsOf(before, "connections");
    const auto connsAfter = itemsOf(after, "connections");
    auto addEnds = [&](const std::string& serialized) {
        const json::Value c = json::parse(serialized);
        changed.insert(c.getString("fromUuid"));
        changed.insert(c.getString("toUuid"));
    };
    for (const auto& [uuid, data] : connsAfter) {
        auto it = connsBefore.find(uuid);
        if (it == connsBefore.end() || it->second != data) addEnds(data);
    }
    for (const auto& [uuid, data] : connsBefore) {
        if (!connsAfter.count(uuid)) addEnds(data);
    }
    const auto nodesAfter = itemsOf(after, "nodes");
    for (auto it = changed.begin(); it != changed.end();) {
        it = nodesAfter.count(*it) ? std::next(it) : changed.erase(it);
    }
    return changed;
}

}  // namespace

std::string History::snapshot(const Canvas& canvas) const {
    json::Value v = doc::contentToJson(canvas);
    v.set("canvas_name", "");  // see the class comment
    return json::serialize(v, 0);
}

bool History::record(const std::string& before, const Canvas& canvas, Msg label) {
    if (before == snapshot(canvas)) return false;
    undo_.push_back({before, label});
    bytes_ += before.size();
    for (const auto& s : redo_) bytes_ -= s.snapshot.size();
    redo_.clear();
    trim();
    ++version_;
    return true;
}

void History::trim() {
    while (undo_.size() > 1 && (undo_.size() > limit_ || bytes_ > maxBytes_)) {
        bytes_ -= undo_.front().snapshot.size();
        undo_.erase(undo_.begin());
    }
}

bool History::undo(Canvas& canvas) {
    return travel(canvas, undo_, redo_);
}

bool History::redo(Canvas& canvas) {
    return travel(canvas, redo_, undo_);
}

// Moves one step from `from` to `to`: the current document goes onto `to`
// under the same label, and the canvas becomes the stored snapshot.
bool History::travel(Canvas& canvas, std::vector<Step>& from, std::vector<Step>& to) {
    if (from.empty()) return false;
    Step step = std::move(from.back());
    from.pop_back();
    std::string current = snapshot(canvas);

    lastLabel_ = step.label;
    lastChanged_ = changedNodes(json::parse(current), json::parse(step.snapshot));
    bytes_ += current.size();
    bytes_ -= step.snapshot.size();
    rollback(canvas, step.snapshot);
    to.push_back({std::move(current), step.label});
    ++version_;
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
    bytes_ = 0;
    ++version_;
}

void History::rollback(Canvas& canvas, const std::string& snapshot) const {
    const json::Value v = json::parse(snapshot);
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<std::shared_ptr<Connection>> connections;
    doc::contentFromJson(v, nodes, connections);
    canvas.replaceContent(std::move(nodes), std::move(connections));
}

}  // namespace ic
