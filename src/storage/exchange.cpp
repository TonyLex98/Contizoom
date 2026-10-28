#include "storage/exchange.h"
#include "i18n/i18n.h"
#include "model/arrange.h"
#include "model/canvas.h"
#include "storage/document_json.h"
#include "util/json.h"
#include "util/path.h"
#include "util/xml.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace ic::exchange {

namespace {

std::string titleOf(const Node& n) {
    if (!n.name.empty()) return n.name;
    const std::string first = n.text.substr(0, n.text.find('\n'));
    return first.empty() ? std::string(tr(Msg::Untitled)) : first;
}

// The node's text without the line that already serves as its title.
std::string bodyOf(const Node& n) {
    if (!n.name.empty()) return n.text;
    const size_t nl = n.text.find('\n');
    return nl == std::string::npos ? std::string() : n.text.substr(nl + 1);
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\n', start);
        if (end == std::string::npos) end = s.size();
        std::string line = s.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Indentation in columns (a tab counts as 4).
int indentOf(const std::string& line) {
    int n = 0;
    for (const char c : line) {
        if (c == ' ') ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

// "Connections" in any of the UI languages: exported files must import
// whatever language they were written in.
bool isConnectionsHeading(const std::string& title) {
    for (int l = 0; l < static_cast<int>(i18n::Language::Count); ++l) {
        if (title == tr(Msg::Connections, static_cast<i18n::Language>(l))) return true;
    }
    return lower(title) == "connections";
}

}  // namespace

const std::vector<FormatInfo>& formats() {
    static const std::vector<FormatInfo> list = {
        {Format::Markdown, ".md"}, {Format::Opml, ".opml"}, {Format::Bundle, ".json"}, {Format::PlainText, ".txt"}};
    return list;
}

const char* extension(Format f) {
    for (const auto& info : formats()) {
        if (info.format == f) return info.extension;
    }
    return "";
}

std::optional<Format> detect(const std::string& pathStr, const std::string& content) {
    const std::string ext = lower(path::toUtf8(path::fromUtf8(pathStr).extension()));
    if (ext == ".md" || ext == ".markdown") return Format::Markdown;
    if (ext == ".opml") return Format::Opml;
    if (ext == ".json") return Format::Bundle;
    if (ext == ".txt") return Format::PlainText;

    // Unknown extension (.xml, none, ...): look at the content.
    const std::string head = trim(content.substr(0, std::min<size_t>(content.size(), 512)));
    if (head.rfind("{", 0) == 0) return Format::Bundle;
    if (head.rfind("<", 0) == 0 && lower(content.substr(0, 4096)).find("<opml") != std::string::npos) return Format::Opml;
    if (!head.empty() && head[0] == '#') return Format::Markdown;
    if (!head.empty()) return Format::PlainText;
    return std::nullopt;
}

// ====================================================================
// Export
// ====================================================================

namespace {

void markdownNode(std::string& out, const Canvas& canvas, const Node& node, int level) {
    // Levels 0-3 are headings (## .. #####); deeper ones nested list items,
    // since Markdown runs out of heading levels.
    constexpr int kHeadingLevels = 4;
    const bool asHeading = level < kHeadingLevels;
    const std::string indent = asHeading ? "" : std::string(static_cast<size_t>(level - kHeadingLevels) * 2, ' ');

    if (asHeading) out += std::string(static_cast<size_t>(level) + 2, '#') + " " + titleOf(node) + "\n\n";
    else out += indent + "- **" + titleOf(node) + "**\n";

    const std::string body = bodyOf(node);
    if (!body.empty()) {
        for (const auto& line : splitLines(body)) out += asHeading ? line + "  \n" : indent + "  " + line + "\n";
        if (asHeading) out += "\n";
    }
    for (const auto& child : canvas.getChildren(node.uuid)) markdownNode(out, canvas, *child, level + 1);
}

void opmlNode(std::string& out, const Canvas& canvas, const Node& node, int depth) {
    const std::string pad(static_cast<size_t>(depth) * 2 + 4, ' ');
    out += pad + "<outline text=\"" + xml::escape(titleOf(node)) + "\"";
    const std::string body = bodyOf(node);
    if (!body.empty()) out += " _note=\"" + xml::escape(body) + "\"";
    const auto children = canvas.getChildren(node.uuid);
    if (children.empty()) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    for (const auto& c : children) opmlNode(out, canvas, *c, depth + 1);
    out += pad + "</outline>\n";
}

void textNode(std::string& out, const Canvas& canvas, const Node& node, int depth) {
    out += std::string(static_cast<size_t>(depth) * 2, ' ') + titleOf(node) + "\n";
    for (const auto& c : canvas.getChildren(node.uuid)) textNode(out, canvas, *c, depth + 1);
}

std::string projectTitle(const Canvas& canvas) {
    return canvas.canvasName.empty() ? std::string(tr(Msg::DefaultProject)) : canvas.canvasName;
}

}  // namespace

std::string toMarkdown(const Canvas& canvas) {
    std::string out = "# " + projectTitle(canvas) + "\n\n";
    for (const auto& root : canvas.getChildren("")) markdownNode(out, canvas, *root, 0);

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

std::string toOpml(const Canvas& canvas) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<opml version=\"2.0\">\n  <head>\n    <title>" +
                      xml::escape(projectTitle(canvas)) + "</title>\n  </head>\n  <body>\n";
    for (const auto& root : canvas.getChildren("")) opmlNode(out, canvas, *root, 0);
    out += "  </body>\n</opml>\n";
    return out;
}

std::string toBundle(const Canvas& canvas) {
    json::Value v = json::Value::object();
    v.set("format", "infinite-canvas-bundle");
    v.set("version", 1);
    v.set("canvas_name", canvas.canvasName);
    v.set("created_at", canvas.createdAt);
    json::Value vp = json::Value::object();
    vp.set("centerX", canvas.viewport.centerX);
    vp.set("centerY", canvas.viewport.centerY);
    vp.set("scale", canvas.viewport.scale);
    v.set("viewport", vp);
    v.set("content", doc::contentToJson(canvas));
    return json::serialize(v);
}

std::string toPlainText(const Canvas& canvas) {
    std::string out;
    for (const auto& root : canvas.getChildren("")) textNode(out, canvas, *root, 0);
    return out;
}

std::string exportAs(const Canvas& canvas, Format format) {
    switch (format) {
        case Format::Markdown: return toMarkdown(canvas);
        case Format::Opml: return toOpml(canvas);
        case Format::Bundle: return toBundle(canvas);
        case Format::PlainText: return toPlainText(canvas);
    }
    return "";
}

// ====================================================================
// Import
// ====================================================================

namespace {

// Tracks the open items by level. Holds indices (not pointers), because
// adding a sibling may reallocate the vector the pointer pointed into.
class Builder {
public:
    explicit Builder(std::vector<OutlineItem>& roots) : roots_(roots) {}

    OutlineItem& add(int level, OutlineItem item) {
        while (!path_.empty() && path_.back().level >= level) path_.pop_back();
        std::vector<OutlineItem>& siblings = path_.empty() ? roots_ : resolve(path_.size())->children;
        siblings.push_back(std::move(item));
        path_.push_back({level, siblings.size() - 1});
        return siblings.back();
    }

    OutlineItem* current() { return path_.empty() ? nullptr : resolve(path_.size()); }

private:
    struct Step {
        int level;
        size_t index;
    };
    std::vector<OutlineItem>& roots_;
    std::vector<Step> path_;

    OutlineItem* resolve(size_t depth) {
        std::vector<OutlineItem>* level = &roots_;
        OutlineItem* item = nullptr;
        for (size_t i = 0; i < depth; ++i) {
            item = &(*level)[path_[i].index];
            level = &item->children;
        }
        return item;
    }
};

void appendText(OutlineItem* item, const std::string& line) {
    if (!item) return;
    if (!item->text.empty()) item->text += '\n';
    item->text += line;
}

// "**Title**" -> "Title"
std::string unbold(std::string s) {
    if (s.size() >= 4 && s.rfind("**", 0) == 0 && s.compare(s.size() - 2, 2, "**") == 0) s = s.substr(2, s.size() - 4);
    return s;
}

// Splits "- A → B: *label*" into a link.
bool parseLinkLine(const std::string& item, OutlineLink& link) {
    for (const auto& [sep, arrow] : {std::pair<const char*, bool>{" → ", true}, {" -> ", true}, {" — ", false}, {" - ", false}}) {
        const size_t at = item.find(sep);
        if (at == std::string::npos) continue;
        link.fromTitle = trim(item.substr(0, at));
        std::string rest = item.substr(at + std::string(sep).size());
        const size_t colon = rest.rfind(": ");
        if (colon != std::string::npos) {
            std::string label = trim(rest.substr(colon + 2));
            if (label.size() >= 2 && label.front() == '*' && label.back() == '*') label = label.substr(1, label.size() - 2);
            link.label = label;
            rest = rest.substr(0, colon);
        }
        link.toTitle = trim(rest);
        link.arrow = arrow;
        return !link.fromTitle.empty() && !link.toTitle.empty();
    }
    return false;
}

// A list marker ("- ", "* ", "+ ", "1. ", "1) ") at the start of `s`;
// returns its length or 0.
size_t listMarker(const std::string& s) {
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') return 2;
    if (s.rfind("• ", 0) == 0) return std::string("• ").size();
    size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i > 0 && i + 1 < s.size() && (s[i] == '.' || s[i] == ')') && s[i + 1] == ' ') return i + 2;
    return 0;
}

}  // namespace

bool fromMarkdown(const std::string& text, Outline& out, std::string& error) {
    out = Outline{};
    Builder builder(out.items);
    int headingLevel = -1;  // level of the last heading item, so lists nest below it
    bool inLinks = false;
    bool sawTitle = false;

    for (const auto& raw : splitLines(text)) {
        const std::string line = trim(raw);
        if (line.empty()) continue;

        if (line[0] == '#') {
            size_t hashes = 0;
            while (hashes < line.size() && line[hashes] == '#') ++hashes;
            if (hashes > 6 || hashes >= line.size() || line[hashes] != ' ') {
                appendText(builder.current(), line);
                continue;
            }
            const std::string title = trim(line.substr(hashes));
            if (hashes == 1 && !sawTitle && out.items.empty()) {
                out.title = title;  // "# Project"
                sawTitle = true;
                continue;
            }
            inLinks = hashes == 2 && isConnectionsHeading(title);
            if (inLinks) continue;
            // "##" is a root when the file has a "#" title, otherwise "#" is.
            headingLevel = static_cast<int>(hashes) - (sawTitle ? 2 : 1);
            builder.add(std::max(0, headingLevel), OutlineItem{title, "", {}});
            continue;
        }

        const size_t marker = listMarker(line);
        if (inLinks) {
            OutlineLink link;
            if (marker && parseLinkLine(line.substr(marker), link)) out.links.push_back(link);
            continue;
        }
        if (marker) {
            const int level = headingLevel + 1 + indentOf(raw) / 2;
            builder.add(level, OutlineItem{unbold(trim(line.substr(marker))), "", {}});
            continue;
        }
        appendText(builder.current(), line);
    }

    if (out.items.empty()) {
        error = "no headings or list items";
        return false;
    }
    return true;
}

namespace {

void opmlItems(const xml::Element& parent, std::vector<OutlineItem>& out) {
    for (const auto& child : parent.children) {
        if (child->name != "outline") continue;
        OutlineItem item;
        item.title = child->attrOr("text", child->attrOr("title"));
        item.text = child->attrOr("_note");
        opmlItems(*child, item.children);
        if (item.title.empty() && item.text.empty() && item.children.empty()) continue;
        out.push_back(std::move(item));
    }
}

}  // namespace

bool fromOpml(const std::string& text, Outline& out, std::string& error) {
    out = Outline{};
    std::unique_ptr<xml::Element> root;
    try {
        root = xml::parse(text);
    } catch (const xml::ParseError& e) {
        error = e.what();
        return false;
    }
    if (root->name != "opml") {
        error = "not an OPML document";
        return false;
    }
    if (const xml::Element* head = root->child("head")) {
        if (const xml::Element* title = head->child("title")) out.title = trim(title->text);
    }
    const xml::Element* body = root->child("body");
    if (!body) {
        error = "OPML without <body>";
        return false;
    }
    opmlItems(*body, out.items);
    if (out.items.empty()) {
        error = "empty outline";
        return false;
    }
    return true;
}

bool fromPlainText(const std::string& text, Outline& out, std::string& error) {
    out = Outline{};
    Builder builder(out.items);
    // Indentation steps vary between files; levels follow distinct indents.
    std::vector<int> indents;
    for (const auto& raw : splitLines(text)) {
        std::string line = trim(raw);
        if (line.empty()) continue;
        const int indent = indentOf(raw);
        while (!indents.empty() && indents.back() > indent) indents.pop_back();
        if (indents.empty() || indents.back() < indent) indents.push_back(indent);
        if (const size_t m = listMarker(line)) line = trim(line.substr(m));
        builder.add(static_cast<int>(indents.size()) - 1, OutlineItem{line, "", {}});
    }
    if (out.items.empty()) {
        error = "the file is empty";
        return false;
    }
    return true;
}

bool fromBundle(const std::string& text, Canvas& out, std::string& error) {
    json::Value v;
    try {
        v = json::parse(text);
    } catch (const json::ParseError& e) {
        error = e.what();
        return false;
    }
    if (v.getString("format") != "infinite-canvas-bundle") {
        error = "not an Infinite Canvas project file";
        return false;
    }
    if (v.getInt("version", 0) > 1) {
        error = "made by a newer version";
        return false;
    }
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<std::shared_ptr<Connection>> connections;
    if (const json::Value* content = v.find("content")) doc::contentFromJson(*content, nodes, connections);

    out.clear();
    out.canvasName = v.getString("canvas_name");
    out.createdAt = v.getUInt64("created_at");
    if (const json::Value* vp = v.find("viewport")) {
        out.viewport.centerX = vp->getNumber("centerX", 0);
        out.viewport.centerY = vp->getNumber("centerY", 0);
        const double scale = vp->getNumber("scale", 1.0);
        out.viewport.scale = scale >= Viewport::kMinScale && scale <= Viewport::kMaxScale ? scale : 1.0;
    }
    out.replaceContent(std::move(nodes), std::move(connections));
    return true;
}

void buildCanvas(const Outline& outline, Canvas& out) {
    out.clear();
    if (!outline.title.empty()) out.canvasName = outline.title;

    std::map<std::string, std::string> byTitle;  // first box with each title
    double order = 0;
    // Creation order sets the initial y, which arrangeTree() keeps as
    // reading order.
    struct Pending {
        const OutlineItem* item;
        std::string parent;
    };
    std::vector<Pending> stack;
    for (auto it = outline.items.rbegin(); it != outline.items.rend(); ++it) stack.push_back({&*it, ""});
    while (!stack.empty()) {
        const Pending p = stack.back();
        stack.pop_back();
        const std::string id = out.createNode(p.item->text, Rect{0, order++, 20, 4}, p.item->title, p.parent);
        byTitle.emplace(p.item->title, id);
        for (auto it = p.item->children.rbegin(); it != p.item->children.rend(); ++it) stack.push_back({&*it, id});
    }

    for (const auto& link : outline.links) {
        auto from = byTitle.find(link.fromTitle);
        auto to = byTitle.find(link.toTitle);
        if (from == byTitle.end() || to == byTitle.end()) continue;
        out.createConnection(from->second, to->second, link.label, link.arrow ? ConnType::Arrow : ConnType::Line);
    }
    arrangeTree(out);
}

bool importText(const std::string& path, const std::string& content, Canvas& out, std::string& error) {
    const auto format = detect(path, content);
    if (!format) {
        error = "unknown file format";
        return false;
    }
    if (*format == Format::Bundle) return fromBundle(content, out, error);

    Outline outline;
    bool ok = false;
    switch (*format) {
        case Format::Markdown: ok = fromMarkdown(content, outline, error); break;
        case Format::Opml: ok = fromOpml(content, outline, error); break;
        case Format::PlainText: ok = fromPlainText(content, outline, error); break;
        case Format::Bundle: break;
    }
    if (!ok) return false;
    if (outline.title.empty()) outline.title = path::toUtf8(path::fromUtf8(path).stem());
    buildCanvas(outline, out);
    return true;
}

}  // namespace ic::exchange
