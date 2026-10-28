#include "input/history.h"
#include "model/canvas.h"
#include "storage/document_json.h"
#include "util/json.h"

namespace ic {

std::string History::snapshot(const Canvas& canvas) const {
    return json::serialize(doc::contentToJson(canvas), 0);
}

bool History::record(const std::string& before, const Canvas& canvas) {
    if (before == snapshot(canvas)) return false;
    undo_.push_back(before);
    if (undo_.size() > limit_) undo_.erase(undo_.begin());
    redo_.clear();
    return true;
}

bool History::undo(Canvas& canvas) {
    if (undo_.empty()) return false;
    redo_.push_back(snapshot(canvas));
    const std::string target = undo_.back();
    undo_.pop_back();
    restore(canvas, target);
    return true;
}

bool History::redo(Canvas& canvas) {
    if (redo_.empty()) return false;
    undo_.push_back(snapshot(canvas));
    const std::string target = redo_.back();
    redo_.pop_back();
    restore(canvas, target);
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
}

void History::restore(Canvas& canvas, const std::string& snapshot) const {
    const json::Value v = json::parse(snapshot);
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<std::shared_ptr<Connection>> connections;
    doc::contentFromJson(v, nodes, connections);
    canvas.canvasName = v.getString("canvas_name", canvas.canvasName);
    canvas.replaceContent(std::move(nodes), std::move(connections));
}

}  // namespace ic
