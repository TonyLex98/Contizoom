#ifndef IC_CELL_BUFFER_H
#define IC_CELL_BUFFER_H

// Off-screen grid of console cells. The renderer composes a whole frame
// here and the platform layer presents it in one call, so there's no
// flicker and the composition itself is portable and testable.
//
// Wide characters (CJK) take two cells: the first is marked Lead, the
// second Trail, both holding the character, which is what the console
// expects. Overwriting either half clears the other, so a half character
// is never left on screen.

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
    enum class Part : uint8_t { Single, Lead, Trail };

    struct Cell {
        wchar_t ch = L' ';
        uint16_t attr = color::Default;
        Part part = Part::Single;
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

    const Cell& at(int x, int y) const { return cells_[index(x, y)]; }
    const std::vector<Cell>& cells() const { return cells_; }

    // A single-width character.
    void put(int x, int y, wchar_t ch, uint16_t attr) {
        if (!inside(x, y)) return;
        breakWide(x, y);
        Cell& c = cells_[index(x, y)];
        c.ch = ch;
        c.attr = attr;
        c.part = Part::Single;
    }

    void setAttr(int x, int y, uint16_t attr) {
        if (!inside(x, y)) return;
        Cell& c = cells_[index(x, y)];
        c.attr = attr;
        // Keep both halves of a wide character the same color.
        if (c.part == Part::Lead && x + 1 < width_) cells_[index(x + 1, y)].attr = attr;
        if (c.part == Part::Trail && x > 0) cells_[index(x - 1, y)].attr = attr;
    }

    wchar_t charAt(int x, int y) const { return inside(x, y) ? at(x, y).ch : L'\0'; }

    void fill(int x, int y, int w, int h, wchar_t ch, uint16_t attr) {
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx) put(xx, yy, ch, attr);
    }

    // Writes UTF-8 text starting at (x, y), using at most `maxCells` cells
    // (a wide character that wouldn't fit entirely is not drawn). Returns
    // the number of cells used.
    int text(int x, int y, const std::string& s, uint16_t attr, int maxCells = 1 << 30) {
        int n = 0;
        for (size_t pos = 0; pos < s.size();) {
            const char32_t cp = utf8::decode(s, pos);
            if (cp == U'\n' || cp == U'\r') break;
            const int w = utf8::cellWidth(cp);
            if (w == 0) continue;
            if (n + w > maxCells) break;
            if (w == 2) putWide(x + n, y, utf8::toCell(cp), attr);
            else put(x + n, y, utf8::toCell(cp), attr);
            n += w;
        }
        return n;
    }

    // Plain-text dump (UTF-8, one line per row, trailing spaces trimmed),
    // used for snapshots and tests.
    std::string toText() const {
        std::string out;
        for (int y = 0; y < height_; ++y) {
            std::string row;
            for (int x = 0; x < width_; ++x) {
                const Cell& c = at(x, y);
                if (c.part != Part::Trail) utf8::append(row, static_cast<char32_t>(c.ch));
            }
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

    size_t index(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }

    // Clears the other half of a wide character occupying (x, y).
    void breakWide(int x, int y) {
        Cell& c = cells_[index(x, y)];
        if (c.part == Part::Lead && x + 1 < width_) cells_[index(x + 1, y)] = Cell{L' ', c.attr, Part::Single};
        if (c.part == Part::Trail && x > 0) cells_[index(x - 1, y)] = Cell{L' ', c.attr, Part::Single};
    }

    void putWide(int x, int y, wchar_t ch, uint16_t attr) {
        if (!inside(x, y) || !inside(x + 1, y)) {
            put(x, y, L' ', attr);  // no room for both halves
            return;
        }
        breakWide(x, y);
        breakWide(x + 1, y);
        cells_[index(x, y)] = Cell{ch, attr, Part::Lead};
        cells_[index(x + 1, y)] = Cell{ch, attr, Part::Trail};
    }
};

}  // namespace ic

#endif  // IC_CELL_BUFFER_H
