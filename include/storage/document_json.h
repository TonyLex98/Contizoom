#ifndef IC_DOCUMENT_JSON_H
#define IC_DOCUMENT_JSON_H

// JSON mapping of the document model, shared by on-disk storage and the
// in-memory undo history so both always agree on what a document is.

#include "model/canvas.h"
#include "util/json.h"

#include <memory>
#include <vector>

namespace ic::doc {

json::Value nodeToJson(const Node& node);
std::shared_ptr<Node> nodeFromJson(const json::Value& v);  // nullptr if unusable

json::Value connectionToJson(const Connection& c);
std::shared_ptr<Connection> connectionFromJson(const json::Value& v);

// config.json: format, name, timestamps, viewport, connections.
json::Value configToJson(const Canvas& canvas);

// Nodes and connections only (no viewport): what undo/redo restores.
json::Value contentToJson(const Canvas& canvas);
void contentFromJson(const json::Value& v, std::vector<std::shared_ptr<Node>>& nodes,
                     std::vector<std::shared_ptr<Connection>>& connections);

}  // namespace ic::doc

#endif  // IC_DOCUMENT_JSON_H
