#include "model/node.h"
#include "util/utf8.h"

#include <algorithm>

namespace ic {

void Node::autoResize(int charsPerLine) {
    constexpr double kMinWidth = 20.0;
    constexpr double kMinHeight = 4.0;
    constexpr double kLineHeight = 1.2;

    if (text.empty()) {
        rect.width = std::max(rect.width, kMinWidth);
        rect.height = std::max(rect.height, kMinHeight);
        return;
    }

    int lines = 1;
    int wrappedExtra = 0;
    int maxLen = 0;
    int currentLen = 0;
    auto endLine = [&] {
        maxLen = std::max(maxLen, currentLen);
        if (currentLen > charsPerLine) wrappedExtra += (currentLen - 1) / charsPerLine;
        currentLen = 0;
    };

    utf8::forEach(text, [&](char32_t cp) {
        if (cp == U'\n') {
            endLine();
            ++lines;
        } else if (cp != U'\r') {
            ++currentLen;
        }
    });
    endLine();

    const double neededW = std::max(kMinWidth, std::min(static_cast<double>(maxLen + 2), static_cast<double>(charsPerLine)));
    const double neededH = std::max(kMinHeight, static_cast<double>(lines + wrappedExtra + 1) * kLineHeight);
    rect.width = std::max(rect.width, neededW);
    rect.height = std::max(rect.height, neededH);
}

}  // namespace ic
