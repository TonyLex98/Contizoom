#ifndef IC_MARKDOWN_EXPORT_H
#define IC_MARKDOWN_EXPORT_H

#include <string>

namespace ic {

class Canvas;

// The project as a Markdown outline: boxes become headings (deeper levels
// become nested list items), box text becomes paragraphs, and connections
// are listed at the end. Order follows the layout (top to bottom, left to
// right), so the document reads the way the canvas does.
std::string exportMarkdown(const Canvas& canvas);

}  // namespace ic

#endif  // IC_MARKDOWN_EXPORT_H
