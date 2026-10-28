#ifndef IC_UTF8_H
#define IC_UTF8_H

// Minimal UTF-8 helpers. All text in the model is stored as UTF-8; the
// console works in UTF-16 code units. Cursor positions in editors are
// counted in code points, never bytes, so multi-byte text (e.g. Cyrillic)
// edits and measures correctly.

#include <cstdint>
#include <string>

namespace ic::utf8 {

// Length in bytes of the UTF-8 sequence starting with lead byte `b`.
// Stray continuation bytes count as 1 so iteration always makes progress.
inline int seqLen(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

// Decodes the code point at `pos` and advances `pos` past it. Truncated or
// malformed sequences decode to U+FFFD instead of reading past the end.
inline char32_t decode(const std::string& s, size_t& pos) {
    const auto b0 = static_cast<unsigned char>(s[pos]);
    const int len = seqLen(b0);
    if (len == 1) {
        ++pos;
        return b0 < 0x80 ? b0 : 0xFFFD;
    }
    if (pos + len > s.size()) {
        pos = s.size();
        return 0xFFFD;
    }
    char32_t cp = b0 & (0xFF >> (len + 1));
    for (int i = 1; i < len; ++i) {
        const auto b = static_cast<unsigned char>(s[pos + i]);
        if ((b & 0xC0) != 0x80) {
            pos += i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    pos += len;
    return cp;
}

inline void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline std::string encode(char32_t cp) {
    std::string out;
    append(out, cp);
    return out;
}

// Number of code points in `s`.
inline int length(const std::string& s) {
    int count = 0;
    for (size_t pos = 0; pos < s.size(); ++count) pos += seqLen(static_cast<unsigned char>(s[pos]));
    return count;
}

// Byte offset of code point number `index` (clamped to the end of `s`).
inline size_t byteOffset(const std::string& s, int index) {
    size_t pos = 0;
    for (int i = 0; i < index && pos < s.size(); ++i) pos += seqLen(static_cast<unsigned char>(s[pos]));
    return pos < s.size() ? pos : s.size();
}

// Inserts the code point `cp` before code point number `index`.
inline void insertAt(std::string& s, int index, char32_t cp) {
    s.insert(byteOffset(s, index), encode(cp));
}

// Erases the code point at `index`; no-op past the end.
inline void eraseAt(std::string& s, int index) {
    const size_t pos = byteOffset(s, index);
    if (pos >= s.size()) return;
    s.erase(pos, seqLen(static_cast<unsigned char>(s[pos])));
}

// First `count` code points of `s`.
inline std::string prefix(const std::string& s, int count) {
    return s.substr(0, byteOffset(s, count));
}

// Calls f(codePoint) for every code point in `s`.
template <typename F>
void forEach(const std::string& s, F&& f) {
    for (size_t pos = 0; pos < s.size();) f(decode(s, pos));
}

// Console cells are UTF-16; code points outside the BMP can't fit in one
// cell, so they are shown as a replacement character.
inline wchar_t toCell(char32_t cp) {
    return cp < 0x10000 ? static_cast<wchar_t>(cp) : L'�';
}

// How many console cells a code point occupies: 2 for East Asian wide and
// fullwidth characters (CJK ideographs, kana, hangul, fullwidth forms),
// 0 for combining marks, 1 otherwise. A compact subset of Unicode's
// East_Asian_Width=W/F ranges, enough for real-world CJK text.
inline int cellWidth(char32_t cp) {
    if (cp == 0) return 0;
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F)) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
        (cp >= 0x2E80 && cp <= 0x303E) ||   // CJK radicals, punctuation
        (cp >= 0x3041 && cp <= 0x33FF) ||   // kana, CJK symbols
        (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK extension A
        (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK unified ideographs
        (cp >= 0xA000 && cp <= 0xA4CF) ||   // Yi
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility ideographs
        (cp >= 0xFE30 && cp <= 0xFE4F) ||   // CJK compatibility forms
        (cp >= 0xFF00 && cp <= 0xFF60) ||   // fullwidth forms
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||   // fullwidth signs
        (cp >= 0x20000 && cp <= 0x3FFFD))   // CJK extensions B+
        return 2;
    return 1;
}

// Display width of a string in console cells.
inline int width(const std::string& s) {
    int w = 0;
    forEach(s, [&](char32_t cp) { w += cellWidth(cp); });
    return w;
}

// Display width of the first `count` code points.
inline int widthOfPrefix(const std::string& s, int count) {
    return width(prefix(s, count));
}

// The longest prefix that fits in `cells` console cells.
inline std::string fitWidth(const std::string& s, int cells) {
    int w = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t next = pos;
        const int cw = cellWidth(decode(s, next));
        if (w + cw > cells) break;
        w += cw;
        pos = next;
    }
    return s.substr(0, pos);
}

}  // namespace ic::utf8

#endif  // IC_UTF8_H
