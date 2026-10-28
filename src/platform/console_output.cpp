#include "platform/console_output.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace ic {

ConsoleOutput::ConsoleOutput() : hOut_(GetStdHandle(STD_OUTPUT_HANDLE)) {}

void ConsoleOutput::present(const CellBuffer& buffer) {
    presentRegion(buffer, 0, 0);
}

void ConsoleOutput::presentRegion(const CellBuffer& buffer, int x, int y) {
    const int w = buffer.width(), h = buffer.height();
    if (w <= 0 || h <= 0) return;

    std::vector<CHAR_INFO> cells(static_cast<size_t>(w) * h);
    const auto& src = buffer.cells();
    for (size_t i = 0; i < cells.size(); ++i) {
        cells[i].Char.UnicodeChar = src[i].ch;
        cells[i].Attributes = src[i].attr;
        // Wide (CJK) characters span two cells; the console wants both
        // halves flagged so it draws one double-width glyph.
        if (src[i].part == CellBuffer::Part::Lead) cells[i].Attributes |= COMMON_LVB_LEADING_BYTE;
        if (src[i].part == CellBuffer::Part::Trail) cells[i].Attributes |= COMMON_LVB_TRAILING_BYTE;
    }
    const COORD size = {static_cast<SHORT>(w), static_cast<SHORT>(h)};
    const COORD origin = {0, 0};
    SMALL_RECT region = {static_cast<SHORT>(x), static_cast<SHORT>(y), static_cast<SHORT>(x + w - 1),
                         static_cast<SHORT>(y + h - 1)};
    WriteConsoleOutputW(static_cast<HANDLE>(hOut_), cells.data(), size, origin, &region);
}

}  // namespace ic
