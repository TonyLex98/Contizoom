#include "storage/markdown_export.h"
#include "i18n/i18n.h"
#include "model/canvas.h"

namespace ic {

namespace {

std::string titleOf(const Node& n) {
    if (!n.name.empty()) return n.name;
    const std::string first = n.text.substr(0, n.text.find('\n'));
    return first.empty() ? std::string(tr(Msg::Untitled)) : first;
}

void writeNode(std::string& out, const Canvas& canvas, const Node& node, int level) {
    // Levels 0-3 are headings (## .. #####); anything deeper is a nested
    // list, since Markdown runs out of heading levels.
    constexpr int kHeadingLevels = 4;
    const bool asHeading = level < kHeadingLevels;
    const std::string indent = asHeading ? "" : std::string(static_cast<size_t>(level - kHeadingLevels) * 2, ' ');

    if (asHeading) out += std::string(static_cast<size_t>(level) + 2, '#') + " " + titleOf(node) + "\n\n";
    else out += indent + "- **" + titleOf(node) + "**\n";

    // Without a name, the first text line already served as the title.
    std::string body = node.text;
    if (node.name.empty()) {
        const size_t nl = body.find('\n');
        body = nl == std::string::npos ? std::string() : body.substr(nl + 1);
    }
    if (!body.empty()) {
        size_t start = 0;
        while (start <= body.size()) {
            size_t end = body.find('\n', start);
            if (end == std::string::npos) end = body.size();
            const std::string line = body.substr(start, end - start);
            out += asHeading ? line + "  \n" : indent + "  " + line + "\n";
            start = end + 1;
        }
        if (asHeading) out += "\n";
    }

    for (const auto& child : canvas.getChildren(node.uuid)) writeNode(out, canvas, *child, level + 1);
}

}  // namespace

std::string exportMarkdown(const Canvas& canvas) {
    std::string out = "# " + (canvas.canvasName.empty() ? std::string(tr(Msg::DefaultProject)) : canvas.canvasName) + "\n\n";

    for (const auto& root : canvas.getChildren("")) writeNode(out, canvas, *root, 0);

    if (!canvas.allConnections().empty()) {
        out += std::string("## ") + tr(Msg::Connections) + "\n\n";
        for (const auto& [_, c] : canvas.allConnections()) {
            auto from = canvas.getNode(c->fromUuid);
            auto to = canvas.getNode(c->toUuid);
            if (!from || !to) continue;
            out += "- " + titleOf(*from) + (isArrow(c->type) ? " → " : " — ") + titleOf(*to);
            if (!c->name.empty()) out += ": *" + c->name + "*";
            out += "\n";
        }
        out += "\n";
    }
    return out;
}

}  // namespace ic
