#include "storage/markdown_export.h"
#include "model/canvas.h"

#include <sstream>

namespace ic {

namespace {

std::string titleOf(const Node& n) {
    if (!n.name.empty()) return n.name;
    const std::string first = n.text.substr(0, n.text.find('\n'));
    return first.empty() ? "(untitled)" : first;
}

void writeNode(std::ostringstream& out, const Canvas& canvas, const Node& node, int level) {
    // Levels 0-3 are headings (## .. #####); anything deeper is a nested
    // list, since Markdown runs out of heading levels.
    constexpr int kHeadingLevels = 4;
    const std::string title = titleOf(node);
    const bool asHeading = level < kHeadingLevels;
    const std::string indent = asHeading ? "" : std::string(static_cast<size_t>(level - kHeadingLevels) * 2, ' ');

    if (asHeading) out << std::string(static_cast<size_t>(level) + 2, '#') << ' ' << title << "\n\n";
    else out << indent << "- **" << title << "**\n";

    // Without a name, the first text line already served as the title.
    std::string body = node.text;
    if (node.name.empty()) {
        const size_t nl = body.find('\n');
        body = nl == std::string::npos ? std::string() : body.substr(nl + 1);
    }
    if (!body.empty()) {
        std::istringstream lines(body);
        std::string line;
        while (std::getline(lines, line)) {
            if (asHeading) out << line << "  \n";
            else out << indent << "  " << line << "\n";
        }
        if (asHeading) out << "\n";
    }

    for (const auto& child : canvas.getChildren(node.uuid)) writeNode(out, canvas, *child, level + 1);
}

}  // namespace

std::string exportMarkdown(const Canvas& canvas) {
    std::ostringstream out;
    out << "# " << (canvas.canvasName.empty() ? "Untitled" : canvas.canvasName) << "\n\n";

    for (const auto& root : canvas.getChildren("")) writeNode(out, canvas, *root, 0);

    if (!canvas.allConnections().empty()) {
        out << "## Connections\n\n";
        for (const auto& [_, c] : canvas.allConnections()) {
            auto from = canvas.getNode(c->fromUuid);
            auto to = canvas.getNode(c->toUuid);
            if (!from || !to) continue;
            out << "- " << titleOf(*from) << (isArrow(c->type) ? " → " : " — ") << titleOf(*to);
            if (!c->name.empty()) out << ": *" << c->name << "*";
            out << "\n";
        }
        out << "\n";
    }
    return out.str();
}

}  // namespace ic
