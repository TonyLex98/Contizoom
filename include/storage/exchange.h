#ifndef IC_EXCHANGE_H
#define IC_EXCHANGE_H

// Import and export to other formats.
//
//   Format      ext     import  export  keeps
//   Markdown    .md     yes     yes     hierarchy, text, connections
//   OPML        .opml   yes     yes     hierarchy, text (the outliner interchange format)
//   Bundle      .json   yes     yes     everything: an exact one-file copy of a project
//   Plain text  .txt    yes     yes     hierarchy (indented lines)
//
// Importers parse into an Outline (titles, text, children, links by title),
// which buildCanvas() turns into boxes laid out by arrangeTree(). The
// bundle is the exception: it restores positions exactly.

#include <optional>
#include <string>
#include <vector>

namespace ic {
class Canvas;
}

namespace ic::exchange {

enum class Format { Markdown, Opml, Bundle, PlainText };

struct FormatInfo {
    Format format;
    const char* extension;  // with the dot
};
const std::vector<FormatInfo>& formats();
const char* extension(Format f);

// By extension first, then by sniffing the content.
std::optional<Format> detect(const std::string& path, const std::string& content);

struct OutlineItem {
    std::string title;
    std::string text;
    std::vector<OutlineItem> children;
};

struct OutlineLink {
    std::string fromTitle, toTitle, label;
    bool arrow = true;
};

struct Outline {
    std::string title;
    std::vector<OutlineItem> items;
    std::vector<OutlineLink> links;
};

// ---- Export --------------------------------------------------------
std::string toMarkdown(const Canvas& canvas);
std::string toOpml(const Canvas& canvas);
std::string toBundle(const Canvas& canvas);
std::string toPlainText(const Canvas& canvas);
std::string exportAs(const Canvas& canvas, Format format);

// ---- Import --------------------------------------------------------
// Each returns false (with `error`) only if the input can't be read as
// that format at all.
bool fromMarkdown(const std::string& text, Outline& out, std::string& error);
bool fromOpml(const std::string& text, Outline& out, std::string& error);
bool fromPlainText(const std::string& text, Outline& out, std::string& error);
bool fromBundle(const std::string& text, Canvas& out, std::string& error);

// Builds boxes (laid out, connections resolved by title) into an empty canvas.
void buildCanvas(const Outline& outline, Canvas& out);

// Detects the format of `content` (read from `path`) and imports it.
bool importText(const std::string& path, const std::string& content, Canvas& out, std::string& error);

}  // namespace ic::exchange

#endif  // IC_EXCHANGE_H
