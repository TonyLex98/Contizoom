#include "model/node.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace ic {

std::optional<std::string> Node::resolvePath(const std::string& path,
                                             const std::map<std::string, std::shared_ptr<Node>>& allNodes) const {
    if (path.empty()) return std::nullopt;

    if (path[0] == '@') {
        std::string targetUuid = path.substr(1);
        auto it = allNodes.find(targetUuid);
        if (it != allNodes.end()) {
            return it->first;
        }
        return std::nullopt;
    }

    std::string currentUuid = uuid;
    std::string remaining = path;

    while (!remaining.empty()) {
        if (remaining.substr(0, 3) == "../") {
            auto it = allNodes.find(currentUuid);
            if (it != allNodes.end() && !it->second->parentId.empty()) {
                currentUuid = it->second->parentId;
            }
            remaining = remaining.substr(3);
        }
        else if (remaining.substr(0, 2) == "./") {
            remaining = remaining.substr(2);
            auto it = allNodes.find(currentUuid);
            if (it != allNodes.end()) {
                const auto& parent = allNodes.find(it->second->parentId);
                if (parent != allNodes.end()) {
                    for (const auto& childUuid : parent->second->childrenUuids) {
                        auto childIt = allNodes.find(childUuid);
                        if (childIt != allNodes.end() &&
                            childIt->second->tag == remaining) {
                            return childIt->first;
                        }
                    }
                }
            }
            return std::nullopt;
        }
        else {
            break;
        }
    }

    return currentUuid;
}

void Node::autoResize(int charsPerLine) {
    if (text.empty()) {
        rect.width = std::max(rect.width, 20.0);
        rect.height = std::max(rect.height, 4.0);
        return;
    }

    int lines = 1;
    int maxLen = 0;
    int currentLen = 0;

    for (char c : text) {
        if (c == '\n') {
            maxLen = std::max(maxLen, currentLen);
            lines++;
            currentLen = 0;
        }
        else {
            currentLen++;
        }
    }
    maxLen = std::max(maxLen, currentLen);

    int wrappedLines = lines;
    if (maxLen > charsPerLine) {
        wrappedLines += maxLen / charsPerLine;
    }

    double neededW = std::max(20.0, std::min(static_cast<double>(maxLen + 2), static_cast<double>(charsPerLine)));
    double neededH = std::max(4.0, static_cast<double>(wrappedLines + 1) * 1.2);
    rect.width = std::max(rect.width, neededW);
    rect.height = std::max(rect.height, neededH);
}

} // namespace ic
