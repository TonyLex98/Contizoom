#ifndef IC_CELL_BUFFER_H
#define IC_CELL_BUFFER_H

// Off-screen grid of console cells. The renderer composes a whole frame
// here and the platform layer presents it in one call, so there's no
// flicker and the composition itself is portable and testable.

#include "util/utf8.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ic {

// Cell attributes use the classic 16-color console layout: low nibble is
// the foreground, high nibble the background (bit 3 = bright).
namespace color {
constexpr uint16_t Black = 0x0, Blue = 0x1, Green = 0x2, Cyan = 0x3, Red = 0x4, Magenta = 0x5, Yellow = 0x6,
                   Gray = 0x7, DarkGray = 0x8, BrightBlue = 0x9, BrightGreen = 0xA, BrightCyan = 0xB,
                   BrightRed = 0xC, BrightMagenta = 0xD, BrightYellow = 0xE, White = 0xF;

constexpr uint16_t make(uint16_t fg, uint16_t bg = Black) {
    return static_cast<uint16_t>((bg << 4) | fg);
}
constexpr uint16_t Default = make(Gray);
constexpr uint16_t Inverse = make(Black, White);
}  // namespace color

class CellBuffer {
public:
    struct Cell {
        wchar_t ch = L' ';
        uint16_t attr = color::Default;
    };

    void resize(int width, int height) {
        width_ = width > 0 ? width : 0;
        height_ = height > 0 ? height : 0;
        cells_.assign(static_cast<size_t>(width_) * height_, Cell{});
    }

    void clear() { cells_.assign(cells_.size(), Cell{}); }

    int width() const { return width_; }
    int height() const { return height_; }
    bool inside(int x, int y) const { return x >= 0 && x < width_ && y >= 0 && y < height_; }

    const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * width_ + x]; }
    const std::vector<Cell>& cells() const { return cells_; }

    void put(int x, int y, wchar_t ch, uint16_t attr) {
        if (!inside(x, y)) return;
        Cell& c = cells_[static_cast<size_t>(y) * width_ + x];
        c.ch = ch;
        c.attr = attr;
    }
    void setAttr(int x, int y, uint16_t attr) {
        if (inside(x, y)) cells_[static_cast<size_t>(y) * width_ + x].attr = attr;
    }
    wchar_t charAt(int x, int y) const { return inside(x, y) ? at(x, y).ch : L'\0'; }

    void fill(int x, int y, int w, int h, wchar_t ch, uint16_t attr) {
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx) put(xx, yy, ch, attr);
    }

    // Writes UTF-8 text starting at (x, y), at most `maxCells` cells.
    // Returns the number of cells written.
    int text(int x, int y, const std::string& s, uint16_t attr, int maxCells = 1 << 30) {
        int n = 0;
        for (size_t pos = 0; pos < s.size() && n < maxCells;) {
            const char32_t cp = utf8::decode(s, pos);
            if (cp == U'\n' || cp == U'\r') break;
            put(x + n, y, utf8::toCell(cp), attr);
            ++n;
        }
        return n;
    }

    // Plain-text dump (UTF-8, one line per row, trailing spaces trimmed),
    // used for snapshots and tests.
    std::string toText() const {
        std::string out;
        for (int y = 0; y < height_; ++y) {
            std::string row;
            for (int x = 0; x < width_; ++x) utf8::append(row, static_cast<char32_t>(at(x, y).ch));
            row.erase(row.find_last_not_of(' ') + 1);
            out += row;
            out += '\n';
        }
        return out;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Cell> cells_;
};

}  // namespace ic

#endif  // IC_CELL_BUFFER_H
